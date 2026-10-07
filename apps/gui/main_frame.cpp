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
#include "command_output_events.h"

#include "timmy/audio/audio_device_enumerator.h"
#include "timmy/core/asr_engine_factory.h"
#include "timmy/core/command_recognition_profile.h"
#include "timmy/core/resource_path_resolver.h"
#include "timmy/command/semantic_command.h"

#include <wx/button.h>
#include <wx/checkbox.h>
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
#include <exception>
#include <optional>
#include <stdexcept>
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
            std::uint64_t outputGeneration;
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
        GuiRecognitionObserver(wxEvtHandler& target, std::uint64_t generation,
            const std::atomic<std::uint64_t>& armedOutputGeneration)
            : target_(target), generation_(generation),
              armedOutputGeneration_(armedOutputGeneration)
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
            event->SetPayload(TranscriptEventPayload{ generation_,
                armedOutputGeneration_.load(std::memory_order_acquire), transcript });
            wxQueueEvent(&target_, event);
        }

    private:
        wxEvtHandler& target_;
        std::uint64_t generation_;
        const std::atomic<std::uint64_t>& armedOutputGeneration_;
    };

    // Creates the native notebook pages and initializes audio capture.
    MainFrame::MainFrame()
        : MainFrame(nullptr, {}, true)
    {
    }

    MainFrame::MainFrame(std::shared_ptr<asr::IAsrBackend> backend,
        CommandOutputSession::EmitterFactory emitterFactory, bool initializeAudio)
        : wxFrame(nullptr, wxID_ANY, "TIMMY", wxDefaultPosition, wxSize(720, 520))
        , backendOverride_(std::move(backend)), outputSession_(std::move(emitterFactory))
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
        createOutputPage(notebook);
        mainSizer->Add(notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        panel->SetSizer(mainSizer);

        deviceChoice_->Bind(wxEVT_CHOICE, &MainFrame::onDeviceSelected, this);
        channelChoice_->Bind(wxEVT_CHOICE, &MainFrame::onChannelSelected, this);
        Bind(wxEVT_THREAD, &MainFrame::onRecognitionState, this, recognitionStateEventId);
        Bind(wxEVT_THREAD, &MainFrame::onRecognitionDiagnostic, this,
            recognitionDiagnosticEventId);
        Bind(wxEVT_THREAD, &MainFrame::onRecognitionTranscript, this,
            recognitionTranscriptEventId);
        Bind(wxEVT_THREAD, &MainFrame::onExecutionResult, this, commandOutputEventId);
        Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
        meterTimer_.SetOwner(this);
        Bind(wxEVT_TIMER, &MainFrame::onMeterTimer, this);
        meterTimer_.Start(50);

        populateRecognitionEngines();
        resolveDefaultResources();
        if (initializeAudio) populateAudioDevices();
        Centre();
    }

    // Ordinary close waits asynchronously. Forced destruction still joins every
    // worker before destroying the event target or any referenced subsystem.
    MainFrame::~MainFrame()
    {
        meterTimer_.Stop();
        recognitionLifecycle_.join();
        if (recognitionPipeline_)
        {
            recognitionPipeline_->setObserver(nullptr);
            recognitionPipeline_->stop();
            recognitionPipeline_.reset();
        }
        outputSession_.stop();
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
        auto addPath = [&](const wxString& label, wxTextCtrl*& text, wxButton*& button,
                           void (MainFrame::*handler)(wxCommandEvent&)) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            row->Add(new wxStaticText(panel, wxID_ANY, label), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
            text = new wxTextCtrl(panel, wxID_ANY);
            row->Add(text, 1, wxEXPAND | wxRIGHT, 8);
            button = new wxButton(panel, wxID_ANY, "Browse...");
            button->Bind(wxEVT_BUTTON, handler, this);
            row->Add(button, 0);
            sizer->Add(row, 0, wxEXPAND | wxALL, 8);
        };
        addChoice("Recognition engine:", engineChoice_);
        addPath("ASR model:", modelPathText_, modelBrowseButton_, &MainFrame::onBrowseModel);
        addPath("VAD model:", vadPathText_, vadBrowseButton_, &MainFrame::onBrowseVadModel);
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
        sizer->Add(rawTranscriptText_, 0, wxEXPAND | wxALL, 8);
        sizer->Add(normalizedTranscriptText_, 0, wxEXPAND | wxALL, 8);
        sizer->Add(assemblyText_, 0, wxEXPAND | wxALL, 8);
        sizer->Add(semanticCommandText_, 0, wxEXPAND | wxALL, 8);
        panel->SetSizer(sizer);
        notebook->AddPage(panel, "Recognition", false);
    }

    // Edits frontend configuration; OSC mapping and execution remain in transport/Core.
    void MainFrame::createOutputPage(wxNotebook* notebook)
    {
        const CommandOutputConfiguration defaults;
        auto* panel = new wxPanel(notebook);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        outputEnabledCheck_ = new wxCheckBox(panel, wxID_ANY, "Enable OSC command output");
        outputEnabledCheck_->SetValue(defaults.enabled);
        outputEnabledCheck_->SetToolTip(
            "Arm OSC while listening, or enable it for the next Start listening.");
        outputEnabledCheck_->Bind(wxEVT_CHECKBOX,
            &MainFrame::onOutputConfigurationChanged, this);
        sizer->Add(outputEnabledCheck_, 0, wxALL, 12);

        auto* grid = new wxFlexGridSizer(2, 8, 12);
        grid->AddGrowableCol(1);
        auto addLabel = [&](const wxString& label) {
            grid->Add(new wxStaticText(panel, wxID_ANY, label), 0,
                wxALIGN_CENTER_VERTICAL);
        };
        auto addText = [&](const wxString& label, wxTextCtrl*& text,
                           const std::string& value) {
            addLabel(label);
            text = new wxTextCtrl(panel, wxID_ANY, wxString::FromUTF8(value));
            grid->Add(text, 1, wxEXPAND);
        };
        addLabel("Profile:");
        outputProfileChoice_ = new wxChoice(panel, wxID_ANY);
        outputProfileChoice_->Append("LT Hydra II");
        outputProfileChoice_->Append("Generic OSC");
        outputProfileChoice_->SetSelection(0);
        outputProfileChoice_->Bind(wxEVT_CHOICE,
            &MainFrame::onOutputConfigurationChanged, this);
        grid->Add(outputProfileChoice_, 1, wxEXPAND);
        addText("Host:", outputHostText_, defaults.host);
        outputHostText_->SetHint("Console IP or hostname");
        addText("Port:", outputPortText_, defaults.port);
        addText("Hydra key hold (ms):", hydraHoldText_, defaults.keyHoldMs);
        addText("Hydra key gap (ms):", hydraGapText_, defaults.keyGapMs);
        const auto timingHint = wxString::Format("Maximum %u ms; default 30 ms",
            static_cast<unsigned int>(maximumHydraTimingMs));
        hydraHoldText_->SetToolTip(timingHint);
        hydraGapText_->SetToolTip(timingHint);
        addLabel("Hydra pre-clear:");
        hydraPreClearCheck_ = new wxCheckBox(panel, wxID_ANY, "Clear before selection commands");
        hydraPreClearCheck_->SetValue(defaults.preClear);
        grid->Add(hydraPreClearCheck_, 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        executionStatusText_ = new wxStaticText(panel, wxID_ANY, "OSC output: Disarmed");
        executionResultText_ = new wxStaticText(panel, wxID_ANY, "Execution: -");
        sizer->Add(executionStatusText_, 0, wxEXPAND | wxALL, 12);
        sizer->Add(executionResultText_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        panel->SetSizer(sizer);
        notebook->AddPage(panel, "Output", false);
        updateOutputControls();
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
        if (!recognitionLifecycle_.canConfigure() || closing_) return;
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

    // Invalidate events and close output acceptance immediately. Blocking joins
    // belong to one owned lifecycle worker; wx continues pumping until poll().
    void MainFrame::stopRecognition()
    {
        if (recognitionLifecycle_.stopping()) return;
        armedOutputGeneration_.store(0, std::memory_order_release);
        ++outputGeneration_;
        outputSession_.disarm();
        outputEnabledCheck_->SetValue(false);
        const bool closeAudio = closing_;
        if (!recognitionLifecycle_.beginStop([this, closeAudio] {
            std::string diagnostic;
            if (recognitionPipeline_)
            {
                recognitionPipeline_->setObserver(nullptr);
                const auto result = recognitionPipeline_->stop();
                if (!result.succeeded)
                    diagnostic = result.diagnostic ? result.diagnostic->message
                        : "Recognition could not stop cleanly.";
                recognitionPipeline_.reset();
            }
            outputSession_.stop();
            if (closeAudio) audioEngine_.stop();
            if (!diagnostic.empty()) throw std::runtime_error(diagnostic);
        }))
        {
            closing_ = false;
            updateRecognitionControls();
            updateOutputStatus();
            recognitionStatusText_->SetLabel(wxString::FromUTF8(
                recognitionLifecycle_.diagnostic()));
            return;
        }
        recognitionObserver_.reset();
        audioShutdownScheduled_ = closeAudio;
        recognitionStatusText_->SetLabel("Stopping...");
        updateRecognitionControls();
        updateOutputStatus();
    }

    void MainFrame::pollShutdown()
    {
        if (!recognitionLifecycle_.poll()) return;
        if (closing_)
        {
            // Close can arrive during an already running normal Stop. Schedule
            // audio cleanup separately after that worker has finished.
            if (!audioShutdownScheduled_)
            {
                stopRecognition();
                return;
            }
            Destroy();
            return;
        }
        updateRecognitionControls();
        updateOutputStatus();
        const auto& diagnostic = recognitionLifecycle_.diagnostic();
        recognitionStatusText_->SetLabel(!diagnostic.empty()
            ? wxString::FromUTF8(diagnostic)
            : (stopDiagnostic_.empty() ? wxString("Stopped.") : stopDiagnostic_));
        stopDiagnostic_.clear();
    }

    // One policy freezes input/resources throughout listening and stopping,
    // regardless of whether command output is armed.
    void MainFrame::updateRecognitionControls()
    {
        const bool idle = recognitionLifecycle_.canConfigure() && !closing_;
        const bool stopping = recognitionLifecycle_.stopping() || closing_;
        recognitionButton_->SetLabel(stopping ? "Stopping..."
            : (recognitionLifecycle_.listening() ? "Stop listening" : "Start listening"));
        recognitionButton_->Enable(!stopping);
        deviceChoice_->Enable(idle && !audioDevices_.empty());
        channelChoice_->Enable(idle && channelChoice_->GetCount() > 0);
        engineChoice_->Enable(idle);
        modelPathText_->Enable(idle);
        vadPathText_->Enable(idle);
        modelBrowseButton_->Enable(idle);
        vadBrowseButton_->Enable(idle);
        languageChoice_->Enable(idle);
        updateOutputControls();
    }

    // Freezes output options for the whole recognition/execution lifecycle.
    void MainFrame::updateOutputControls()
    {
        const bool idle = recognitionLifecycle_.canConfigure() && !closing_;
        const bool hydra = idle && outputProfileChoice_->GetSelection() == 0;
        outputEnabledCheck_->Enable(!recognitionLifecycle_.stopping() && !closing_);
        outputProfileChoice_->Enable(idle);
        outputHostText_->Enable(idle);
        outputPortText_->Enable(idle);
        hydraHoldText_->Enable(hydra);
        hydraGapText_->Enable(hydra);
        hydraPreClearCheck_->Enable(hydra);
    }

    void MainFrame::updateOutputStatus()
    {
        executionStatusText_->SetLabel(outputSession_.active()
            && !recognitionLifecycle_.stopping() && !closing_
            ? "OSC output: Armed" : "OSC output: Disarmed");
    }

    void MainFrame::armOutput()
    {
        if (recognitionPipeline_) recognitionPipeline_->discardPendingAssembly();
        auto configuration = frozenOutputConfiguration_;
        configuration.enabled = true;
        const auto generation = ++outputGeneration_;
        const auto result = outputSession_.start(configuration,
            commandOutputObserver(*this, generation));
        if (result.succeeded)
        {
            armedOutputGeneration_.store(generation, std::memory_order_release);
        }
        else
        {
            armedOutputGeneration_.store(0, std::memory_order_release);
            outputSession_.disarm();
            outputEnabledCheck_->SetValue(false);
            showExecutionResult({core::RecognitionExecutionState::failed, result.diagnostic});
        }
        updateOutputStatus();
    }

    CommandOutputConfiguration MainFrame::outputConfiguration() const
    {
        CommandOutputConfiguration configuration;
        configuration.enabled = outputEnabledCheck_->GetValue();
        configuration.profile = outputProfileChoice_->GetSelection() == 0
            ? OutputProfile::hydra2 : OutputProfile::genericOsc;
        configuration.host = outputHostText_->GetValue().ToStdString(wxConvUTF8);
        configuration.port = outputPortText_->GetValue().ToStdString(wxConvUTF8);
        configuration.keyHoldMs = hydraHoldText_->GetValue().ToStdString(wxConvUTF8);
        configuration.keyGapMs = hydraGapText_->GetValue().ToStdString(wxConvUTF8);
        configuration.preClear = hydraPreClearCheck_->GetValue();
        return configuration;
    }

    void MainFrame::onOutputConfigurationChanged(wxCommandEvent&)
    {
        if (recognitionLifecycle_.stopping() || closing_) return;
        if (recognitionLifecycle_.listening())
        {
            if (outputEnabledCheck_->GetValue()) armOutput();
            else
            {
                armedOutputGeneration_.store(0, std::memory_order_release);
                outputSession_.disarm();
                if (recognitionPipeline_) recognitionPipeline_->discardPendingAssembly();
            }
        }
        updateOutputControls();
        updateOutputStatus();
    }

    // Restarts audio capture for a newly selected physical device.
    void MainFrame::onDeviceSelected(wxCommandEvent&)
    {
        if (!recognitionLifecycle_.canConfigure() || closing_) return;
        startSelectedDevice();
    }

    // Routes capture to the newly selected channel without reopening the device.
    void MainFrame::onChannelSelected(wxCommandEvent&)
    {
        if (!recognitionLifecycle_.canConfigure() || closing_) return;
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
        if (!recognitionLifecycle_.canConfigure() || closing_) return;
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
        if (!recognitionLifecycle_.canConfigure() || closing_) return;
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
        if (recognitionLifecycle_.stopping() || closing_) return;
        if (recognitionLifecycle_.listening())
        {
            stopRecognition();
            return;
        }

        if (!recognitionLifecycle_.beginListening()) return;
        const auto generation = recognitionLifecycle_.generation();
        frozenOutputConfiguration_ = outputConfiguration();
        updateRecognitionControls();
        recognitionStatusText_->SetLabel("Starting recognition...");
        try
        {
            core::AsrEngineResources resources{
                wxStringToPath(vadPathText_->GetValue())
            };
            auto backend = backendOverride_ ? backendOverride_
                : core::createAsrBackend(core::AsrEngineId::whisperCpp, resources);
            const auto languageProfile = commandProfileForOutput(frozenOutputConfiguration_);
            recognitionPipeline_ = std::make_unique<core::RecognitionPipeline>(
                channelSelector_, std::move(backend), languageProfile);
            recognitionObserver_ = std::make_shared<GuiRecognitionObserver>(*this, generation,
                armedOutputGeneration_);
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
            configuration.recognitionHints = core::commandRecognitionHints(languageProfile);
            const auto result = recognitionPipeline_->start(configuration);
            if (!result.succeeded)
            {
                stopDiagnostic_ = result.diagnostic
                    ? wxString::FromUTF8(result.diagnostic->message)
                    : wxString("Recognition could not start.");
                stopRecognition();
                return;
            }
            if (outputEnabledCheck_->GetValue()) armOutput();
            updateOutputStatus();
        }
        catch (const std::exception& error)
        {
            stopDiagnostic_ = "Recognition could not start: "
                + wxString::FromUTF8(error.what());
            stopRecognition();
        }
        catch (...)
        {
            stopDiagnostic_ = "Recognition could not start: unknown initialization failure.";
            stopRecognition();
        }
    }

    // Applies a current-session state event on the wxWidgets thread.
    void MainFrame::onRecognitionState(wxThreadEvent& event)
    {
        const auto payload = event.GetPayload<StateEventPayload>();
        if (!recognitionLifecycle_.accepts(payload.generation))
        {
            return;
        }
        recognitionStatusText_->SetLabel(stateText(payload.state));
    }

    // Applies a current-session structured diagnostic on the wxWidgets thread.
    void MainFrame::onRecognitionDiagnostic(wxThreadEvent& event)
    {
        const auto payload = event.GetPayload<DiagnosticEventPayload>();
        if (!recognitionLifecycle_.accepts(payload.generation))
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
        if (!recognitionLifecycle_.accepts(payload.generation))
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
            assembly += wxString::FromUTF8(" \xE2\x80\x94 ")
                + wxString::FromUTF8(payload.transcript.assemblyText);
        }
        assemblyText_->SetLabel(assembly);
        wxString semantic = "Semantic command: "
            + semanticStateText(payload.transcript.semanticState);
        if (payload.transcript.semanticCommand)
        {
            semantic += wxString::FromUTF8(" \xE2\x80\x94 ") + wxString::FromUTF8(
                command::describe(*payload.transcript.semanticCommand));
        }
        if (payload.transcript.semanticCommand) {
            semantic += " | Validation: ";
            semantic += payload.transcript.validation.accepted ? "accepted" : "rejected";
            if (!payload.transcript.validation.diagnostic.empty())
                semantic += " (" + wxString::FromUTF8(payload.transcript.validation.diagnostic) + ")";
            if (!payload.transcript.rendering.diagnostic.empty())
                semantic += " | Rendering: " + wxString::FromUTF8(payload.transcript.rendering.diagnostic);
            if (payload.transcript.rendering.text)
                semantic += " | Rendered command: " + wxString::FromUTF8(*payload.transcript.rendering.text);
        }
        semanticCommandText_->SetLabel(semantic);
        const int resultWidth = std::max(100,
            semanticCommandText_->GetParent()->GetClientSize().GetWidth() - 16);
        for (auto* label : {rawTranscriptText_, normalizedTranscriptText_,
                assemblyText_, semanticCommandText_})
            label->Wrap(resultWidth);
        semanticCommandText_->GetParent()->Layout();

        // Submission only enqueues the produced semantic command. The existing
        // executor validates the event and performs dispatch/OSC on its worker.
        // A transcript produced while disarmed stays observable even if its wx
        // event is processed after a later arm. Only the current arm may submit.
        if (payload.outputGeneration != 0 && payload.outputGeneration == outputGeneration_)
        {
            showExecutionResult(outputSession_.submit(payload.transcript));
        }
    }

    void MainFrame::showExecutionResult(const core::RecognitionExecutionResult& result)
    {
        wxString text;
        switch (result.state)
        {
        case core::RecognitionExecutionState::ignored: return;
        case core::RecognitionExecutionState::queued: text = "Queued"; break;
        case core::RecognitionExecutionState::succeeded: text = "Succeeded"; break;
        case core::RecognitionExecutionState::failed: text = "Failed"; break;
        }
        if (!result.diagnostic.empty())
        {
            text += wxString::FromUTF8(" \xE2\x80\x94 ")
                + wxString::FromUTF8(result.diagnostic);
        }
        if (!result.renderedCommand.empty())
            text += " | Rendered command: " + wxString::FromUTF8(result.renderedCommand);
        executionResultText_->SetLabel("Execution: " + text);
        executionResultText_->Wrap(std::max(100, GetClientSize().GetWidth() - 64));
        executionResultText_->GetParent()->Layout();
    }

    // Applies copied execution results only for the current listening session.
    void MainFrame::onExecutionResult(wxThreadEvent& event)
    {
        const auto payload = event.GetPayload<CommandOutputEventPayload>();
        if (recognitionLifecycle_.listening() && payload.generation == outputGeneration_
            && !closing_)
        {
            showExecutionResult(payload.result);
        }
    }

    // Keep the wx event target alive until every recognition/output callback
    // source and audio worker has finished. A later timer tick performs Destroy.
    void MainFrame::onClose(wxCloseEvent& event)
    {
        if (!event.CanVeto())
        {
            // Forced system close falls back to the destructor's ownership join.
            event.Skip();
            return;
        }
        event.Veto();
        closing_ = true;
        stopRecognition();
        updateRecognitionControls();
    }

    // Refreshes the level meter from its thread-safe audio snapshot.
    void MainFrame::onMeterTimer(wxTimerEvent&)
    {
        pollShutdown();
        if (closing_) return;
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
