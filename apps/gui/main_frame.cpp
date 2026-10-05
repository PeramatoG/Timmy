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

#include "gui_theme.h"

#include "timmy/audio/audio_device_enumerator.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/thread.h>
#include <wx/utils.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace timmy::gui {

    namespace {

        wxDEFINE_EVENT(
            recognitionStateEvent,
            wxThreadEvent);

        wxDEFINE_EVENT(
            recognitionTranscriptEvent,
            wxThreadEvent);

        struct RecognitionStatusEventData
        {
            std::uint64_t generation = 0;
            core::RecognitionPipelineStatus status;
        };

        struct RecognitionTranscriptEventData
        {
            std::uint64_t generation = 0;
            core::RecognitionTranscript transcript;
        };

        class GuiRecognitionObserver final
            : public core::IRecognitionPipelineObserver
        {
        public:
            GuiRecognitionObserver(
                wxEvtHandler& target,
                std::uint64_t generation)
                : target_(target),
                  generation_(generation)
            {
            }

            // Queues a pipeline state update for delivery on the wxWidgets thread.
            void onRecognitionStateChanged(
                const core::RecognitionPipelineStatus& status) override
            {
                auto* event =
                    new wxThreadEvent(recognitionStateEvent);
                event->SetPayload(
                    RecognitionStatusEventData{ generation_, status });
                wxQueueEvent(&target_, event);
            }

            // Queues transcript text for delivery on the wxWidgets thread.
            void onRecognitionTranscript(
                const core::RecognitionTranscript& transcript) override
            {
                auto* event =
                    new wxThreadEvent(recognitionTranscriptEvent);
                event->SetPayload(
                    RecognitionTranscriptEventData{
                        generation_,
                        transcript
                    });
                wxQueueEvent(&target_, event);
            }

        private:
            wxEvtHandler& target_;
            std::uint64_t generation_ = 0;
        };

        // Reads an environment override or returns the supplied development default.
        wxString environmentValue(
            const wxString& name,
            const wxString& fallback)
        {
            wxString value;
            return wxGetEnv(name, &value) ? value : fallback;
        }

        // Converts a wxWidgets path without losing non-ASCII path characters.
        std::filesystem::path filesystemPath(const wxString& value)
        {
#ifdef _WIN32
            return std::filesystem::path(value.ToStdWstring());
#else
            const auto utf8 = value.ToUTF8();
            const auto* begin =
                reinterpret_cast<const char8_t*>(utf8.data());
            return std::filesystem::path(
                std::u8string(begin, begin + utf8.length()));
#endif
        }

        // Opens a model file picker and assigns the selected path to a text control.
        void browseForModel(
            wxWindow& parent,
            wxTextCtrl& target,
            const wxString& title)
        {
            const wxFileName current(target.GetValue());
            wxFileDialog dialog(
                &parent,
                title,
                current.GetPath(),
                current.GetFullName(),
                "Model files (*.bin)|*.bin|All files (*.*)|*.*",
                wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dialog.ShowModal() == wxID_OK)
            {
                target.SetValue(dialog.GetPath());
            }
        }

    } // namespace

    // Builds the tabbed audio and recognition frontend.
    MainFrame::MainFrame()
        : wxFrame(
            nullptr,
            wxID_ANY,
            "TIMMY",
            wxDefaultPosition,
            wxSize(720, 620))
    {
        channelSelector_ =
            std::make_shared<audio::AudioChannelSelector>(0);
        meter_ =
            std::make_shared<audio::AudioMeter>();
        channelSelector_->addConsumer(meter_);
        audioEngine_.addConsumer(channelSelector_);

        auto* panel = new wxPanel(this);
        auto* mainSizer = new wxBoxSizer(wxVERTICAL);
        auto* title = new wxStaticText(
            panel,
            wxID_ANY,
            "TIMMY\nThe Imaginary Man, Minding You");
        mainSizer->Add(title, 0, wxALIGN_CENTER | wxALL, 16);

        auto* notebook = new wxNotebook(panel, wxID_ANY);
        notebook->AddPage(createAudioPage(*notebook), "Audio", true);
        notebook->AddPage(
            createRecognitionPage(*notebook),
            "Recognition",
            false);
        mainSizer->Add(
            notebook,
            1,
            wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
            16);
        panel->SetSizer(mainSizer);

        deviceChoice_->Bind(
            wxEVT_CHOICE,
            &MainFrame::onDeviceSelected,
            this);
        channelChoice_->Bind(
            wxEVT_CHOICE,
            &MainFrame::onChannelSelected,
            this);
        recognitionEngineChoice_->Bind(
            wxEVT_CHOICE,
            &MainFrame::onRecognitionEngineSelected,
            this);
        listeningButton_->Bind(
            wxEVT_BUTTON,
            &MainFrame::onListeningToggled,
            this);

        meterTimer_.SetOwner(this);
        Bind(wxEVT_TIMER, &MainFrame::onMeterTimer, this);
        Bind(
            recognitionStateEvent,
            &MainFrame::onRecognitionState,
            this);
        Bind(
            recognitionTranscriptEvent,
            &MainFrame::onRecognitionTranscript,
            this);

        populateRecognitionEngines();
        populateAudioDevices();
        applyAppearance(*this, Appearance::dark);
        meterTimer_.Start(50);
        Centre();
    }

    // Stops worker-owned recognition before destroying GUI event targets.
    MainFrame::~MainFrame()
    {
        meterTimer_.Stop();
        stopListening();
        audioEngine_.stop();
        DeletePendingEvents();
    }

    // Creates the page containing capture selection, metering, and audio status.
    wxPanel* MainFrame::createAudioPage(wxNotebook& notebook)
    {
        auto* page = new wxPanel(&notebook);
        auto* pageSizer = new wxBoxSizer(wxVERTICAL);

        auto* deviceRow = new wxBoxSizer(wxHORIZONTAL);
        deviceRow->Add(
            new wxStaticText(page, wxID_ANY, "Input device:"),
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);
        deviceChoice_ = new wxChoice(page, wxID_ANY);
        deviceRow->Add(deviceChoice_, 1, wxEXPAND);
        pageSizer->Add(
            deviceRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            20);

        auto* channelRow = new wxBoxSizer(wxHORIZONTAL);
        channelRow->Add(
            new wxStaticText(page, wxID_ANY, "Channel:"),
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);
        channelChoice_ = new wxChoice(page, wxID_ANY);
        channelRow->Add(channelChoice_, 1, wxEXPAND);
        pageSizer->Add(
            channelRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            20);

        pageSizer->Add(
            new wxStaticText(page, wxID_ANY, "Input level:"),
            0,
            wxLEFT | wxRIGHT | wxTOP,
            20);
        levelGauge_ = new wxGauge(
            page,
            wxID_ANY,
            1000,
            wxDefaultPosition,
            wxDefaultSize,
            wxGA_HORIZONTAL);
        pageSizer->Add(
            levelGauge_,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            20);
        levelText_ = new wxStaticText(
            page,
            wxID_ANY,
            "Peak: 0.0000   RMS: 0.0000");
        pageSizer->Add(
            levelText_,
            0,
            wxLEFT | wxRIGHT | wxTOP,
            12);

        statusText_ = new wxStaticText(
            page,
            wxID_ANY,
            "Searching for capture devices...");
        pageSizer->Add(
            statusText_,
            0,
            wxLEFT | wxRIGHT | wxTOP,
            20);
        pageSizer->AddStretchSpacer();
        page->SetSizer(pageSizer);
        return page;
    }

    // Creates the page containing backend selection and transcript observation.
    wxPanel* MainFrame::createRecognitionPage(wxNotebook& notebook)
    {
        auto* page = new wxPanel(&notebook);
        auto* pageSizer = new wxBoxSizer(wxVERTICAL);

        auto* engineRow = new wxBoxSizer(wxHORIZONTAL);
        engineRow->Add(
            new wxStaticText(page, wxID_ANY, "Recognition engine:"),
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);
        recognitionEngineChoice_ = new wxChoice(page, wxID_ANY);
        engineRow->Add(recognitionEngineChoice_, 1, wxEXPAND);
        pageSizer->Add(
            engineRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            20);

        auto* modelRow = new wxBoxSizer(wxHORIZONTAL);
        modelRow->Add(
            new wxStaticText(page, wxID_ANY, "Model:"),
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);
        modelPathText_ = new wxTextCtrl(
            page,
            wxID_ANY,
            environmentValue(
                "TIMMY_WHISPER_MODEL",
                "models/ggml-base-q5_1.bin"));
        modelRow->Add(modelPathText_, 1, wxEXPAND | wxRIGHT, 8);
        modelBrowseButton_ = new wxButton(
            page,
            wxID_ANY,
            "Browse...");
        modelBrowseButton_->Bind(
            wxEVT_BUTTON,
            &MainFrame::onBrowseModel,
            this);
        modelRow->Add(modelBrowseButton_, 0);
        pageSizer->Add(
            modelRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            16);

        auto* vadRow = new wxBoxSizer(wxHORIZONTAL);
        vadRow->Add(
            new wxStaticText(page, wxID_ANY, "VAD model:"),
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);
        vadModelPathText_ = new wxTextCtrl(
            page,
            wxID_ANY,
            environmentValue(
                "TIMMY_WHISPER_VAD_MODEL",
                "models/ggml-silero-v6.2.0.bin"));
        vadRow->Add(vadModelPathText_, 1, wxEXPAND | wxRIGHT, 8);
        vadBrowseButton_ = new wxButton(
            page,
            wxID_ANY,
            "Browse...");
        vadBrowseButton_->Bind(
            wxEVT_BUTTON,
            &MainFrame::onBrowseVadModel,
            this);
        vadRow->Add(vadBrowseButton_, 0);
        pageSizer->Add(
            vadRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            12);

        auto* languageRow = new wxBoxSizer(wxHORIZONTAL);
        languageRow->Add(
            new wxStaticText(page, wxID_ANY, "Language:"),
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);
        languageChoice_ = new wxChoice(page, wxID_ANY);
        languageChoice_->Append("Spanish (es)");
        languageChoice_->Append("Automatic detection");
        languageChoice_->Append("English (en)");
        languageChoice_->SetSelection(0);
        languageRow->Add(languageChoice_, 1, wxEXPAND);
        pageSizer->Add(
            languageRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            12);

        auto* controlRow = new wxBoxSizer(wxHORIZONTAL);
        listeningButton_ = new wxButton(
            page,
            wxID_ANY,
            "Start listening");
        recognitionStatusText_ = new wxStaticText(
            page,
            wxID_ANY,
            "Stopped");
        controlRow->Add(listeningButton_, 0, wxRIGHT, 12);
        controlRow->Add(
            recognitionStatusText_,
            1,
            wxALIGN_CENTER_VERTICAL);
        pageSizer->Add(
            controlRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            16);

        rawTranscriptText_ = new wxStaticText(
            page,
            wxID_ANY,
            "Raw transcript: -");
        rawTranscriptText_->Wrap(640);
        pageSizer->Add(
            rawTranscriptText_,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            24);
        normalizedTranscriptText_ = new wxStaticText(
            page,
            wxID_ANY,
            "Normalized transcript: -");
        normalizedTranscriptText_->Wrap(640);
        pageSizer->Add(
            normalizedTranscriptText_,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            12);

        pageSizer->AddStretchSpacer();
        page->SetSizer(pageSizer);
        return page;
    }

    // Populates the engine selector from the GUI-independent backend factory.
    void MainFrame::populateRecognitionEngines()
    {
        recognitionEngines_ = asr::availableAsrEngines();
        recognitionEngineChoice_->Clear();
        for (const auto& engine : recognitionEngines_)
        {
            recognitionEngineChoice_->Append(
                wxString::FromUTF8(engine.displayName));
        }

        if (!recognitionEngines_.empty())
        {
            recognitionEngineChoice_->SetSelection(0);
            wxCommandEvent event;
            onRecognitionEngineSelected(event);
        }
    }

    // Enumerates capture devices and selects the current system default.
    void MainFrame::populateAudioDevices()
    {
        deviceChoice_->Clear();
        channelChoice_->Clear();
        audioDevices_.clear();

        const auto result = audio::enumerateCaptureDevices();
        if (!result.ok())
        {
            statusText_->SetLabel("Audio device discovery failed.");
            deviceChoice_->Enable(false);
            channelChoice_->Enable(false);
            return;
        }

        audioDevices_ = result.devices;
        if (audioDevices_.empty())
        {
            statusText_->SetLabel("No capture devices found.");
            deviceChoice_->Enable(false);
            channelChoice_->Enable(false);
            return;
        }

        int defaultSelection = 0;
        for (std::size_t index = 0; index < audioDevices_.size(); ++index)
        {
            const auto& device = audioDevices_[index];
            deviceChoice_->Append(wxString::FromUTF8(device.name));
            if (device.isDefault)
            {
                defaultSelection = static_cast<int>(index);
            }
        }

        deviceChoice_->SetSelection(defaultSelection);
        deviceChoice_->Enable(true);
        startSelectedDevice();
    }

    // Starts capture from the device currently selected in the GUI.
    void MainFrame::startSelectedDevice()
    {
        stopListening();
        audioEngine_.stop();
        channelChoice_->Clear();
        channelChoice_->Enable(false);
        meter_->reset();
        levelGauge_->SetValue(0);
        levelText_->SetLabel("Peak: 0.0000   RMS: 0.0000");

        const int selection = deviceChoice_->GetSelection();
        if (selection == wxNOT_FOUND
            || selection < 0
            || static_cast<std::size_t>(selection) >= audioDevices_.size())
        {
            statusText_->SetLabel("No capture device selected.");
            return;
        }

        const auto& device =
            audioDevices_[static_cast<std::size_t>(selection)];
        audio::AudioCaptureConfig config;
        if (device.id.has_value())
        {
            config.deviceId = device.id;
        }
        else
        {
            config.runtimeDeviceIndex = device.runtimeIndex;
        }

        const auto startResult = audioEngine_.start(config);
        if (!startResult.ok())
        {
            statusText_->SetLabel(
                wxString::FromUTF8(startResult.diagnostic->message));
            return;
        }

        for (std::uint32_t channel = 0;
            channel < startResult.format.channelCount;
            ++channel)
        {
            channelChoice_->Append(wxString::Format(
                "Channel %u",
                static_cast<unsigned int>(channel + 1)));
        }

        if (startResult.format.channelCount > 0)
        {
            channelChoice_->SetSelection(0);
            channelChoice_->Enable(true);
            channelSelector_->setChannelIndex(0);
        }

        statusText_->SetLabel(wxString::Format(
            "Capturing at %u Hz, %u channel(s).",
            static_cast<unsigned int>(startResult.format.sampleRate),
            static_cast<unsigned int>(startResult.format.channelCount)));
    }

    // Restarts capture when the user chooses a different input device.
    void MainFrame::onDeviceSelected(wxCommandEvent&)
    {
        startSelectedDevice();
    }

    // Routes the selected input channel to metering and recognition consumers.
    void MainFrame::onChannelSelected(wxCommandEvent&)
    {
        const int selection = channelChoice_->GetSelection();
        if (selection == wxNOT_FOUND || selection < 0)
        {
            return;
        }

        channelSelector_->setChannelIndex(
            static_cast<std::size_t>(selection));
        meter_->reset();
        levelGauge_->SetValue(0);
        levelText_->SetLabel("Peak: 0.0000   RMS: 0.0000");
    }

    // Refreshes the input meter from its thread-safe audio snapshot.
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
        levelText_->SetLabel(wxString::Format(
            "Peak: %.4f   RMS: %.4f",
            static_cast<double>(level.peak),
            static_cast<double>(level.rms)));
    }

    // Starts or stops recognition while capture and metering continue independently.
    void MainFrame::onListeningToggled(wxCommandEvent&)
    {
        if (recognitionPipeline_)
        {
            stopListening();
            return;
        }

        if (!audioEngine_.isRunning())
        {
            recognitionStatusText_->SetLabel(
                "Error: audio capture is not running.");
            return;
        }

        const int engineSelection = recognitionEngineChoice_->GetSelection();
        if (engineSelection == wxNOT_FOUND
            || engineSelection < 0
            || static_cast<std::size_t>(engineSelection)
                >= recognitionEngines_.size())
        {
            recognitionStatusText_->SetLabel(
                "Error: select a recognition engine.");
            return;
        }

        const auto modelPath = filesystemPath(modelPathText_->GetValue());
        if (modelPath.empty())
        {
            recognitionStatusText_->SetLabel("Error: select an ASR model.");
            return;
        }

        asr::AsrBackendCreateOptions backendOptions;
        backendOptions.engine = recognitionEngines_[
            static_cast<std::size_t>(engineSelection)].engine;
        backendOptions.vadModelResourcePath =
            filesystemPath(vadModelPathText_->GetValue());

        std::shared_ptr<asr::IAsrBackend> backend;
        try
        {
            backend = asr::createAsrBackend(backendOptions);
        }
        catch (const std::exception& error)
        {
            recognitionStatusText_->SetLabel(
                "Error: " + wxString::FromUTF8(error.what()));
            return;
        }

        recognitionObserver_ =
            std::make_shared<GuiRecognitionObserver>(
                *this,
                ++recognitionGeneration_);
        recognitionPipeline_ =
            std::make_unique<core::RecognitionPipeline>(
                channelSelector_,
                std::move(backend));
        recognitionPipeline_->setObserver(recognitionObserver_);

        asr::AsrConfiguration configuration;
        configuration.modelResourcePath = modelPath;
        if (languageChoice_->GetSelection() == 0)
        {
            configuration.preferredLanguage = "es";
        }
        else if (languageChoice_->GetSelection() == 2)
        {
            configuration.preferredLanguage = "en";
        }
        configuration.recognitionHints =
            core::makeDefaultCommandRecognitionHints();

        recognitionEngineChoice_->Enable(false);
        modelPathText_->Enable(false);
        modelBrowseButton_->Enable(false);
        vadModelPathText_->Enable(false);
        vadBrowseButton_->Enable(false);
        languageChoice_->Enable(false);
        listeningButton_->Enable(false);
        recognitionStatusText_->SetLabel("Starting...");

        const auto result = recognitionPipeline_->start(configuration);
        if (!result.succeeded)
        {
            recognitionPipeline_->setObserver(nullptr);
            recognitionPipeline_.reset();
            recognitionObserver_.reset();
            recognitionEngineChoice_->Enable(true);
            modelPathText_->Enable(true);
            modelBrowseButton_->Enable(true);
            vadModelPathText_->Enable(true);
            vadBrowseButton_->Enable(true);
            languageChoice_->Enable(true);
            listeningButton_->Enable(true);
        }
    }

    // Updates controls that depend on the selected backend's capabilities.
    void MainFrame::onRecognitionEngineSelected(wxCommandEvent&)
    {
        const int selection = recognitionEngineChoice_->GetSelection();
        const bool usesVad = selection != wxNOT_FOUND
            && selection >= 0
            && static_cast<std::size_t>(selection) < recognitionEngines_.size()
            && recognitionEngines_[static_cast<std::size_t>(selection)]
                .usesVadModelResource;
        vadModelPathText_->Enable(usesVad);
        vadBrowseButton_->Enable(usesVad);
    }

    // Opens the primary ASR model file picker.
    void MainFrame::onBrowseModel(wxCommandEvent&)
    {
        browseForModel(*this, *modelPathText_, "Select recognition model");
    }

    // Opens the selected backend's VAD model file picker.
    void MainFrame::onBrowseVadModel(wxCommandEvent&)
    {
        browseForModel(*this, *vadModelPathText_, "Select VAD model");
    }

    // Applies a marshalled recognition state update to GUI controls.
    void MainFrame::onRecognitionState(wxThreadEvent& event)
    {
        const auto data =
            event.GetPayload<RecognitionStatusEventData>();
        if (data.generation != recognitionGeneration_)
        {
            return;
        }

        const auto& status = data.status;
        if (status.state == core::RecognitionPipelineState::listening)
        {
            recognitionStatusText_->SetLabel("Listening");
            listeningButton_->SetLabel("Stop listening");
            listeningButton_->Enable(true);
            return;
        }

        if (status.state == core::RecognitionPipelineState::error)
        {
            const wxString message = status.diagnostic.has_value()
                ? wxString::FromUTF8(status.diagnostic->message)
                : "Unknown recognition error.";
            recognitionStatusText_->SetLabel("Error: " + message);
            listeningButton_->Enable(true);
            return;
        }

        recognitionStatusText_->SetLabel("Stopped");
    }

    // Displays UTF-8 raw and normalized transcripts in separate fields.
    void MainFrame::onRecognitionTranscript(wxThreadEvent& event)
    {
        const auto data =
            event.GetPayload<RecognitionTranscriptEventData>();
        if (data.generation != recognitionGeneration_)
        {
            return;
        }

        const auto& transcript = data.transcript;
        rawTranscriptText_->SetLabel(
            "Raw transcript: "
            + wxString::FromUTF8(transcript.rawText));
        normalizedTranscriptText_->SetLabel(
            "Normalized transcript: "
            + wxString::FromUTF8(transcript.normalizedText));
        Layout();
    }

    // Stops and joins recognition work while leaving capture available for metering.
    void MainFrame::stopListening()
    {
        if (recognitionPipeline_)
        {
            ++recognitionGeneration_;
            recognitionPipeline_->setObserver(nullptr);
            recognitionPipeline_->stop();
            recognitionPipeline_.reset();
        }
        recognitionObserver_.reset();

        if (listeningButton_)
        {
            listeningButton_->SetLabel("Start listening");
            listeningButton_->Enable(true);
        }
        if (modelPathText_)
        {
            recognitionEngineChoice_->Enable(true);
            modelPathText_->Enable(true);
            modelBrowseButton_->Enable(true);
            languageChoice_->Enable(true);
            recognitionStatusText_->SetLabel("Stopped");
            wxCommandEvent event;
            onRecognitionEngineSelected(event);
        }
    }

} // namespace timmy::gui
