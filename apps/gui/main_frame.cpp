/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "main_frame.h"

#include "timmy/audio/audio_device_enumerator.h"
#include "timmy/core/asr_engine_factory.h"
#include "timmy/core/command_recognition_profile.h"
#include "timmy/core/resource_path_resolver.h"
#include "timmy/command/semantic_command.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/gauge.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/thread.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace timmy::gui {

    namespace {

        constexpr int recognitionStateEventId = wxID_HIGHEST + 100;
        constexpr int recognitionDiagnosticEventId = wxID_HIGHEST + 101;
        constexpr int recognitionTranscriptEventId = wxID_HIGHEST + 102;

        struct StateEventPayload
        {
            std::uint64_t generation;
            core::RecognitionPipelineState state;
        };

        struct DiagnosticEventPayload
        {
            std::uint64_t generation;
            asr::AsrDiagnostic diagnostic;
        };

        struct TranscriptEventPayload
        {
            std::uint64_t generation;
            core::RecognitionTranscript transcript;
        };

        // Converts a filesystem path without narrowing Windows Unicode text.
        wxString pathToWxString(const std::filesystem::path& path)
        {
#ifdef _WIN32
            return wxString(path.wstring());
#else
            return wxString::FromUTF8(path.string());
#endif
        }

        // Converts native wxWidgets path text without an ANSI round trip.
        std::filesystem::path wxStringToPath(const wxString& value)
        {
#ifdef _WIN32
            return std::filesystem::path(value.ToStdWstring());
#else
            return std::filesystem::path(value.ToStdString(wxConvUTF8));
#endif
        }

        // Returns the display text for a backend-neutral pipeline state.
        wxString stateText(core::RecognitionPipelineState state)
        {
            switch (state)
            {
            case core::RecognitionPipelineState::stopped: return "Stopped.";
            case core::RecognitionPipelineState::starting: return "Starting recognition...";
            case core::RecognitionPipelineState::listening: return "Listening.";
            case core::RecognitionPipelineState::stopping: return "Stopping recognition...";
            case core::RecognitionPipelineState::failed: return "Recognition failed.";
            }
            return "Unknown recognition state.";
        }

        // Returns display text for the shared command assembly state.
        wxString assemblyStateText(core::RecognitionAssemblyState state)
        {
            switch (state)
            {
            case core::RecognitionAssemblyState::pending: return "pending";
            case core::RecognitionAssemblyState::complete: return "complete";
            case core::RecognitionAssemblyState::rejected: return "rejected";
            }
            return "unknown";
        }

        // Returns display text for the shared semantic parse state.
        wxString semanticStateText(core::RecognitionSemanticState state)
        {
            switch (state)
            {
            case core::RecognitionSemanticState::notAttempted: return "not attempted";
            case core::RecognitionSemanticState::produced: return "produced";
            case core::RecognitionSemanticState::unsupportedOrInvalid:
                return "unsupported or invalid";
            }
            return "unknown";
        }

    } // namespace

    class MainFrame::GuiRecognitionObserver final
        : public core::IRecognitionPipelineObserver
    {
    public:
        GuiRecognitionObserver(wxEvtHandler& target, std::uint64_t generation)
            : target_(target), generation_(generation)
        {
        }

        // Queues state changes for processing exclusively on the GUI thread.
        void onRecognitionStateChanged(core::RecognitionPipelineState state) override
        {
            auto* event = new wxThreadEvent(wxEVT_THREAD, recognitionStateEventId);
            event->SetPayload(StateEventPayload{ generation_, state });
            wxQueueEvent(&target_, event);
        }

        // Queues complete structured diagnostics for the GUI thread.
        void onRecognitionDiagnostic(const asr::AsrDiagnostic& diagnostic) override
        {
            auto* event = new wxThreadEvent(wxEVT_THREAD, recognitionDiagnosticEventId);
            event->SetPayload(DiagnosticEventPayload{ generation_, diagnostic });
            wxQueueEvent(&target_, event);
        }

        // Queues raw and normalized UTF-8 transcript text for the GUI thread.
        void onRecognitionTranscript(const core::RecognitionTranscript& transcript) override
        {
            auto* event = new wxThreadEvent(wxEVT_THREAD, recognitionTranscriptEventId);
            event->SetPayload(TranscriptEventPayload{ generation_, transcript });
            wxQueueEvent(&target_, event);
        }

    private:
        wxEvtHandler& target_;
        std::uint64_t generation_;
    };

    // Creates the native two-page GUI and initializes audio capture.
    MainFrame::MainFrame()
        : wxFrame(nullptr, wxID_ANY, "TIMMY", wxDefaultPosition, wxSize(720, 520))
    {
        channelSelector_ = std::make_shared<audio::AudioChannelSelector>(0);
        meter_ = std::make_shared<audio::AudioMeter>();
        channelSelector_->addConsumer(meter_);
        audioEngine_.addConsumer(channelSelector_);

        auto* panel = new wxPanel(this);
        auto* mainSizer = new wxBoxSizer(wxVERTICAL);
        auto* title = new wxStaticText(panel, wxID_ANY,
            "TIMMY\nThe Imaginary Man, Minding You");
        mainSizer->Add(title, 0, wxALIGN_CENTER | wxALL, 12);
        auto* notebook = new wxNotebook(panel, wxID_ANY);
        createAudioPage(notebook);
        createRecognitionPage(notebook);
        mainSizer->Add(notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        panel->SetSizer(mainSizer);

        deviceChoice_->Bind(wxEVT_CHOICE, &MainFrame::onDeviceSelected, this);
        channelChoice_->Bind(wxEVT_CHOICE, &MainFrame::onChannelSelected, this);
        Bind(wxEVT_THREAD, &MainFrame::onRecognitionState, this, recognitionStateEventId);
        Bind(wxEVT_THREAD, &MainFrame::onRecognitionDiagnostic, this,
            recognitionDiagnosticEventId);
        Bind(wxEVT_THREAD, &MainFrame::onRecognitionTranscript, this,
            recognitionTranscriptEventId);
        meterTimer_.SetOwner(this);
        Bind(wxEVT_TIMER, &MainFrame::onMeterTimer, this);
        meterTimer_.Start(50);

        populateRecognitionEngines();
        resolveDefaultResources();
        populateAudioDevices();
        Centre();
    }

    // Stops and joins recognition before any GUI event target is destroyed.
    MainFrame::~MainFrame()
    {
        meterTimer_.Stop();
        stopRecognition();
        audioEngine_.stop();
        DeletePendingEvents();
    }

    // Builds the native controls for audio device selection and metering.
    void MainFrame::createAudioPage(wxNotebook* notebook)
    {
        auto* panel = new wxPanel(notebook);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        auto addChoice = [&](const wxString& label, wxChoice*& choice) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            row->Add(new wxStaticText(panel, wxID_ANY, label), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
            choice = new wxChoice(panel, wxID_ANY);
            row->Add(choice, 1, wxEXPAND);
            sizer->Add(row, 0, wxEXPAND | wxALL, 12);
        };
        addChoice("Input device:", deviceChoice_);
        addChoice("Channel:", channelChoice_);
        sizer->Add(new wxStaticText(panel, wxID_ANY, "Input level:"), 0,
            wxLEFT | wxRIGHT | wxTOP, 12);
        levelGauge_ = new wxGauge(panel, wxID_ANY, 1000);
        sizer->Add(levelGauge_, 0, wxEXPAND | wxALL, 12);
        levelText_ = new wxStaticText(panel, wxID_ANY, "Peak: 0.0000   RMS: 0.0000");
        sizer->Add(levelText_, 0, wxLEFT | wxRIGHT, 12);
        audioStatusText_ = new wxStaticText(panel, wxID_ANY,
            "Searching for capture devices...");
        sizer->Add(audioStatusText_, 0, wxALL, 12);
        panel->SetSizer(sizer);
        notebook->AddPage(panel, "Audio", true);
    }

    // Builds the native controls for recognition resources and results.
    void MainFrame::createRecognitionPage(wxNotebook* notebook)
    {
        auto* panel = new wxPanel(notebook);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        auto addChoice = [&](const wxString& label, wxChoice*& choice) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            row->Add(new wxStaticText(panel, wxID_ANY, label), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
            choice = new wxChoice(panel, wxID_ANY);
            row->Add(choice, 1, wxEXPAND);
            sizer->Add(row, 0, wxEXPAND | wxALL, 8);
        };
        auto addPath = [&](const wxString& label, wxTextCtrl*& text,
                           void (MainFrame::*handler)(wxCommandEvent&)) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            row->Add(new wxStaticText(panel, wxID_ANY, label), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
            text = new wxTextCtrl(panel, wxID_ANY);
            row->Add(text, 1, wxEXPAND | wxRIGHT, 8);
            auto* button = new wxButton(panel, wxID_ANY, "Browse...");
            button->Bind(wxEVT_BUTTON, handler, this);
            row->Add(button, 0);
            sizer->Add(row, 0, wxEXPAND | wxALL, 8);
        };
        addChoice("Recognition engine:", engineChoice_);
        addPath("ASR model:", modelPathText_, &MainFrame::onBrowseModel);
        addPath("VAD model:", vadPathText_, &MainFrame::onBrowseVadModel);
        addChoice("Language:", languageChoice_);
        recognitionButton_ = new wxButton(panel, wxID_ANY, "Start listening");
        recognitionButton_->Bind(wxEVT_BUTTON, &MainFrame::onToggleRecognition, this);
        sizer->Add(recognitionButton_, 0, wxALL, 8);
        recognitionStatusText_ = new wxStaticText(panel, wxID_ANY, "Stopped.");
        rawTranscriptText_ = new wxStaticText(panel, wxID_ANY, "Raw transcript: -");
        normalizedTranscriptText_ = new wxStaticText(panel, wxID_ANY,
            "Normalized transcript: -");
        assemblyText_ = new wxStaticText(panel, wxID_ANY, "Assembly: -");
        semanticCommandText_ = new wxStaticText(panel, wxID_ANY,
            "Semantic command: -");
        sizer->Add(recognitionStatusText_, 0, wxALL, 8);
        sizer->Add(rawTranscriptText_, 0, wxALL, 8);
        sizer->Add(normalizedTranscriptText_, 0, wxALL, 8);
        sizer->Add(assemblyText_, 0, wxALL, 8);
        sizer->Add(semanticCommandText_, 0, wxALL, 8);
        panel->SetSizer(sizer);
        notebook->AddPage(panel, "Recognition", false);
    }

    // Fills the GUI selector from the backend-neutral engine registry.
    void MainFrame::populateRecognitionEngines()
    {
        for (const auto& engine : core::availableAsrEngines())
        {
            engineChoice_->Append(wxString::FromUTF8(engine.displayName.data(), engine.displayName.size()));
        }
        engineChoice_->SetSelection(0);
        languageChoice_->Append("Spanish (es)");
        languageChoice_->Append("Automatic");
        languageChoice_->Append("English (en)");
        languageChoice_->SetSelection(0);
    }

    // Resolves model defaults from environment, supplied paths, or executable parents.
    void MainFrame::resolveDefaultResources()
    {
        const auto executable = wxStringToPath(wxStandardPaths::Get().GetExecutablePath());
        const auto model = core::resolveResourcePath({
            "TIMMY_WHISPER_MODEL", "models/ggml-base-q5_1.bin", executable,
            "models/ggml-base-q5_1.bin" });
        const auto vad = core::resolveResourcePath({
            "TIMMY_WHISPER_VAD_MODEL", "models/ggml-silero-v6.2.0.bin", executable,
            "models/ggml-silero-v6.2.0.bin" });
        modelPathText_->SetValue(pathToWxString(model.path));
        vadPathText_->SetValue(pathToWxString(vad.path));
        if (!model.found)
        {
            recognitionStatusText_->SetLabel(wxString::FromUTF8(model.diagnostic));
        }
        else if (!vad.found)
        {
            recognitionStatusText_->SetLabel(wxString::FromUTF8(vad.diagnostic));
        }
    }

    // Discovers capture devices and starts the platform default selection.
    void MainFrame::populateAudioDevices()
    {
        deviceChoice_->Clear();
        channelChoice_->Clear();
        audioDevices_.clear();
        const auto result = audio::enumerateCaptureDevices();
        if (!result.ok())
        {
            audioStatusText_->SetLabel("Audio device discovery failed.");
            deviceChoice_->Enable(false);
            channelChoice_->Enable(false);
            return;
        }
        audioDevices_ = result.devices;
        if (audioDevices_.empty())
        {
            audioStatusText_->SetLabel("No capture devices found.");
            deviceChoice_->Enable(false);
            channelChoice_->Enable(false);
            return;
        }
        int defaultSelection = 0;
        for (std::size_t index = 0; index < audioDevices_.size(); ++index)
        {
            deviceChoice_->Append(wxString::FromUTF8(audioDevices_[index].name));
            if (audioDevices_[index].isDefault)
            {
                defaultSelection = static_cast<int>(index);
            }
        }
        deviceChoice_->SetSelection(defaultSelection);
        deviceChoice_->Enable(true);
        startSelectedDevice();
    }

    // Starts capture from the selected device after recognition has stopped.
    void MainFrame::startSelectedDevice()
    {
        stopRecognition();
        audioEngine_.stop();
        channelChoice_->Clear();
        channelChoice_->Enable(false);
        meter_->reset();
        levelGauge_->SetValue(0);
        levelText_->SetLabel("Peak: 0.0000   RMS: 0.0000");
        const int selection = deviceChoice_->GetSelection();
        if (selection == wxNOT_FOUND || selection < 0
            || static_cast<std::size_t>(selection) >= audioDevices_.size())
        {
            audioStatusText_->SetLabel("No capture device selected.");
            return;
        }
        const auto& device = audioDevices_[static_cast<std::size_t>(selection)];
        audio::AudioCaptureConfig config;
        if (device.id)
        {
            config.deviceId = device.id;
        }
        else
        {
            config.runtimeDeviceIndex = device.runtimeIndex;
        }
        const auto result = audioEngine_.start(config);
        if (!result.ok())
        {
            audioStatusText_->SetLabel(wxString::FromUTF8(result.diagnostic->message));
            return;
        }
        for (std::uint32_t channel = 0; channel < result.format.channelCount; ++channel)
        {
            channelChoice_->Append(wxString::Format("Channel %u",
                static_cast<unsigned int>(channel + 1)));
        }
        if (result.format.channelCount > 0)
        {
            channelChoice_->SetSelection(0);
            channelChoice_->Enable(true);
            channelSelector_->setChannelIndex(0);
        }
        audioStatusText_->SetLabel(wxString::Format("Capturing at %u Hz, %u channel(s).",
            static_cast<unsigned int>(result.format.sampleRate),
            static_cast<unsigned int>(result.format.channelCount)));
    }

    // Stops recognition, invalidates queued events, and releases its backend.
    void MainFrame::stopRecognition()
    {
        ++recognitionGeneration_;
        if (recognitionPipeline_)
        {
            recognitionPipeline_->setObserver(nullptr);
            recognitionPipeline_->stop();
            recognitionPipeline_.reset();
        }
        recognitionObserver_.reset();
        if (recognitionButton_)
        {
            updateRecognitionControls(false);
        }
    }

    // Updates controls that differ between stopped and listening states.
    void MainFrame::updateRecognitionControls(bool listening)
    {
        recognitionButton_->SetLabel(listening ? "Stop listening" : "Start listening");
        engineChoice_->Enable(!listening);
        modelPathText_->Enable(!listening);
        vadPathText_->Enable(!listening);
        languageChoice_->Enable(!listening);
    }

    // Restarts audio capture for a newly selected physical device.
    void MainFrame::onDeviceSelected(wxCommandEvent&)
    {
        startSelectedDevice();
    }

    // Routes capture to the newly selected channel without reopening the device.
    void MainFrame::onChannelSelected(wxCommandEvent&)
    {
        const int selection = channelChoice_->GetSelection();
        if (selection == wxNOT_FOUND || selection < 0)
        {
            return;
        }
        channelSelector_->setChannelIndex(static_cast<std::size_t>(selection));
        meter_->reset();
        levelGauge_->SetValue(0);
        levelText_->SetLabel("Peak: 0.0000   RMS: 0.0000");
    }

    // Opens a native file picker for the main ASR model.
    void MainFrame::onBrowseModel(wxCommandEvent&)
    {
        wxFileDialog dialog(this, "Select ASR model", wxEmptyString,
            wxEmptyString, "Model files (*.bin)|*.bin|All files (*.*)|*.*",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK)
        {
            modelPathText_->SetValue(dialog.GetPath());
        }
    }

    // Opens a native file picker for the Whisper voice-activity model.
    void MainFrame::onBrowseVadModel(wxCommandEvent&)
    {
        wxFileDialog dialog(this, "Select VAD model", wxEmptyString,
            wxEmptyString, "Model files (*.bin)|*.bin|All files (*.*)|*.*",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK)
        {
            vadPathText_->SetValue(dialog.GetPath());
        }
    }

    // Starts or stops one generation of recognition from the selected channel.
    void MainFrame::onToggleRecognition(wxCommandEvent&)
    {
        if (recognitionPipeline_)
        {
            stopRecognition();
            recognitionStatusText_->SetLabel("Stopped.");
            return;
        }

        const auto generation = ++recognitionGeneration_;
        core::AsrEngineResources resources{
            wxStringToPath(vadPathText_->GetValue())
        };
        auto backend = core::createAsrBackend(core::AsrEngineId::whisperCpp, resources);
        recognitionPipeline_ = std::make_unique<core::RecognitionPipeline>(
            channelSelector_, std::move(backend));
        recognitionObserver_ = std::make_shared<GuiRecognitionObserver>(*this, generation);
        recognitionPipeline_->setObserver(recognitionObserver_);

        asr::AsrConfiguration configuration;
        configuration.modelResourcePath = wxStringToPath(modelPathText_->GetValue());
        const int language = languageChoice_->GetSelection();
        if (language == 0)
        {
            configuration.preferredLanguage = "es";
        }
        else if (language == 2)
        {
            configuration.preferredLanguage = "en";
        }
        configuration.recognitionHints = core::commandRecognitionHints();
        const auto result = recognitionPipeline_->start(configuration);
        if (!result.succeeded)
        {
            const wxString diagnostic = result.diagnostic
                ? wxString::FromUTF8(result.diagnostic->message)
                : wxString("Recognition could not start.");
            stopRecognition();
            recognitionStatusText_->SetLabel(diagnostic);
            return;
        }
        updateRecognitionControls(true);
    }

    // Applies a current-session state event on the wxWidgets thread.
    void MainFrame::onRecognitionState(wxThreadEvent& event)
    {
        const auto payload = event.GetPayload<StateEventPayload>();
        if (payload.generation != recognitionGeneration_)
        {
            return;
        }
        recognitionStatusText_->SetLabel(stateText(payload.state));
    }

    // Applies a current-session structured diagnostic on the wxWidgets thread.
    void MainFrame::onRecognitionDiagnostic(wxThreadEvent& event)
    {
        const auto payload = event.GetPayload<DiagnosticEventPayload>();
        if (payload.generation != recognitionGeneration_)
        {
            return;
        }
        recognitionStatusText_->SetLabel(wxString::Format("Recognition error [%d]: %s",
            static_cast<int>(payload.diagnostic.code),
            wxString::FromUTF8(payload.diagnostic.message)));
    }

    // Displays current-session UTF-8 raw and normalized transcripts.
    void MainFrame::onRecognitionTranscript(wxThreadEvent& event)
    {
        const auto payload = event.GetPayload<TranscriptEventPayload>();
        if (payload.generation != recognitionGeneration_)
        {
            return;
        }
        rawTranscriptText_->SetLabel("Raw transcript: "
            + wxString::FromUTF8(payload.transcript.rawTranscript));
        normalizedTranscriptText_->SetLabel("Normalized transcript: "
            + wxString::FromUTF8(payload.transcript.normalizedTranscript));
        wxString assembly = "Assembly: "
            + assemblyStateText(payload.transcript.assemblyState);
        if (!payload.transcript.assemblyText.empty())
        {
            assembly += " — " + wxString::FromUTF8(payload.transcript.assemblyText);
        }
        assemblyText_->SetLabel(assembly);
        wxString semantic = "Semantic command: "
            + semanticStateText(payload.transcript.semanticState);
        if (payload.transcript.semanticCommand)
        {
            semantic += " — " + wxString::FromUTF8(
                command::describe(*payload.transcript.semanticCommand));
        }
        semanticCommandText_->SetLabel(semantic);
    }

    // Refreshes the level meter from its thread-safe audio snapshot.
    void MainFrame::onMeterTimer(wxTimerEvent&)
    {
        const auto snapshot = meter_->snapshot();
        if (snapshot.channels.empty())
        {
            levelGauge_->SetValue(0);
            levelText_->SetLabel("Peak: 0.0000   RMS: 0.0000");
            return;
        }
        const auto& level = snapshot.channels.front();
        const float peak = std::clamp(level.peak, 0.0F, 1.0F);
        levelGauge_->SetValue(static_cast<int>(peak * 1000.0F));
        levelText_->SetLabel(wxString::Format("Peak: %.4f   RMS: %.4f",
            static_cast<double>(level.peak), static_cast<double>(level.rms)));
    }

} // namespace timmy::gui
