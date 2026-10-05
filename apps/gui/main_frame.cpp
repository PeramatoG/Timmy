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
#include "timmy/asr/whisper_asr_backend.h"
#include "timmy/core/recognition_pipeline.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/gauge.h>
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
#include <optional>
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

        struct RecognitionStateEventData
        {
            core::RecognitionPipelineState state;
            std::string detail;
        };

        class GuiRecognitionObserver final
            : public core::IRecognitionPipelineObserver
        {
        public:
            explicit GuiRecognitionObserver(wxEvtHandler& target)
                : target_(target)
            {
            }

            // Queues a pipeline state update for delivery on the wxWidgets thread.
            void onRecognitionStateChanged(
                core::RecognitionPipelineState state,
                const std::string& detail) override
            {
                auto* event =
                    new wxThreadEvent(recognitionStateEvent);
                event->SetPayload(
                    RecognitionStateEventData{ state, detail });
                wxQueueEvent(&target_, event);
            }

            // Queues transcript text for delivery on the wxWidgets thread.
            void onRecognitionTranscript(
                const core::RecognitionTranscript& transcript) override
            {
                auto* event =
                    new wxThreadEvent(recognitionTranscriptEvent);
                event->SetPayload(transcript);
                wxQueueEvent(&target_, event);
            }

        private:
            wxEvtHandler& target_;
        };

        // Reads an optional UTF-8 path from an environment variable.
        wxString environmentValue(const wxString& name)
        {
            wxString value;
            wxGetEnv(name, &value);
            return value;
        }

    } // namespace

    // Builds the audio controls and backend-neutral recognition status view.
    MainFrame::MainFrame()
        : wxFrame(
            nullptr,
            wxID_ANY,
            "TIMMY",
            wxDefaultPosition,
            wxSize(720, 680))
    {
        channelSelector_ =
            std::make_shared<
            audio::AudioChannelSelector>(0);

        meter_ =
            std::make_shared<
            audio::AudioMeter>();

        channelSelector_->addConsumer(
            meter_);

        audioEngine_.addConsumer(
            channelSelector_);

        auto* panel =
            new wxPanel(this);

        auto* mainSizer =
            new wxBoxSizer(wxVERTICAL);

        auto* title =
            new wxStaticText(
                panel,
                wxID_ANY,
                "TIMMY\nThe Imaginary Man, Minding You");

        mainSizer->Add(
            title,
            0,
            wxALIGN_CENTER | wxALL,
            16);

        // -------------------------------------------------------------------------
        // Input device
        // -------------------------------------------------------------------------

        auto* deviceRow =
            new wxBoxSizer(wxHORIZONTAL);

        auto* deviceLabel =
            new wxStaticText(
                panel,
                wxID_ANY,
                "Input device:");

        deviceChoice_ =
            new wxChoice(
                panel,
                wxID_ANY);

        deviceRow->Add(
            deviceLabel,
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);

        deviceRow->Add(
            deviceChoice_,
            1,
            wxEXPAND);

        mainSizer->Add(
            deviceRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            20);

        // -------------------------------------------------------------------------
        // Channel
        // -------------------------------------------------------------------------

        auto* channelRow =
            new wxBoxSizer(wxHORIZONTAL);

        auto* channelLabel =
            new wxStaticText(
                panel,
                wxID_ANY,
                "Channel:");

        channelChoice_ =
            new wxChoice(
                panel,
                wxID_ANY);

        channelRow->Add(
            channelLabel,
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);

        channelRow->Add(
            channelChoice_,
            1,
            wxEXPAND);

        mainSizer->Add(
            channelRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            20);

        // -------------------------------------------------------------------------
        // Input level
        // -------------------------------------------------------------------------

        auto* levelLabel =
            new wxStaticText(
                panel,
                wxID_ANY,
                "Input level:");

        mainSizer->Add(
            levelLabel,
            0,
            wxLEFT | wxRIGHT | wxTOP,
            20);

        levelGauge_ =
            new wxGauge(
                panel,
                wxID_ANY,
                1000,
                wxDefaultPosition,
                wxDefaultSize,
                wxGA_HORIZONTAL);

        mainSizer->Add(
            levelGauge_,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            20);

        levelText_ =
            new wxStaticText(
                panel,
                wxID_ANY,
                "Peak: 0.0000   RMS: 0.0000");

        mainSizer->Add(
            levelText_,
            0,
            wxLEFT | wxRIGHT | wxTOP,
            20);

        // -------------------------------------------------------------------------
        // Status
        // -------------------------------------------------------------------------

        statusText_ =
            new wxStaticText(
                panel,
                wxID_ANY,
                "Searching for capture devices...");

        mainSizer->Add(
            statusText_,
            0,
            wxLEFT | wxRIGHT | wxTOP,
            20);

        // -------------------------------------------------------------------------
        // Speech recognition
        // -------------------------------------------------------------------------

        auto* asrLabel =
            new wxStaticText(
                panel,
                wxID_ANY,
                "Speech recognition");

        mainSizer->Add(
            asrLabel,
            0,
            wxLEFT | wxRIGHT | wxTOP,
            20);

        auto* modelRow =
            new wxBoxSizer(wxHORIZONTAL);
        modelRow->Add(
            new wxStaticText(panel, wxID_ANY, "Model path:"),
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);
        modelPathText_ =
            new wxTextCtrl(
                panel,
                wxID_ANY,
                environmentValue("TIMMY_WHISPER_MODEL"));
        modelRow->Add(modelPathText_, 1, wxEXPAND);
        mainSizer->Add(
            modelRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            20);

        auto* vadRow =
            new wxBoxSizer(wxHORIZONTAL);
        vadRow->Add(
            new wxStaticText(panel, wxID_ANY, "VAD model path:"),
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);
        vadModelPathText_ =
            new wxTextCtrl(
                panel,
                wxID_ANY,
                environmentValue("TIMMY_WHISPER_VAD_MODEL"));
        vadRow->Add(vadModelPathText_, 1, wxEXPAND);
        mainSizer->Add(
            vadRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            12);

        auto* languageRow =
            new wxBoxSizer(wxHORIZONTAL);
        languageRow->Add(
            new wxStaticText(panel, wxID_ANY, "Language:"),
            0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT,
            8);
        languageText_ =
            new wxTextCtrl(panel, wxID_ANY);
        languageText_->SetHint("auto");
        languageRow->Add(languageText_, 1, wxEXPAND);
        mainSizer->Add(
            languageRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            12);

        auto* controlRow =
            new wxBoxSizer(wxHORIZONTAL);
        listeningButton_ =
            new wxButton(
                panel,
                wxID_ANY,
                "Start listening");
        recognitionStatusText_ =
            new wxStaticText(
                panel,
                wxID_ANY,
                "Stopped");
        controlRow->Add(listeningButton_, 0, wxRIGHT, 12);
        controlRow->Add(
            recognitionStatusText_,
            1,
            wxALIGN_CENTER_VERTICAL);
        mainSizer->Add(
            controlRow,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            12);

        rawTranscriptText_ =
            new wxStaticText(
                panel,
                wxID_ANY,
                "Raw transcript: —");
        rawTranscriptText_->Wrap(660);
        mainSizer->Add(
            rawTranscriptText_,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            20);

        normalizedTranscriptText_ =
            new wxStaticText(
                panel,
                wxID_ANY,
                "Normalized transcript: —");
        normalizedTranscriptText_->Wrap(660);
        mainSizer->Add(
            normalizedTranscriptText_,
            0,
            wxEXPAND | wxLEFT | wxRIGHT | wxTOP,
            12);

        mainSizer->AddStretchSpacer();

        panel->SetSizer(mainSizer);

        // -------------------------------------------------------------------------
        // Events
        // -------------------------------------------------------------------------

        deviceChoice_->Bind(
            wxEVT_CHOICE,
            &MainFrame::onDeviceSelected,
            this);

        channelChoice_->Bind(
            wxEVT_CHOICE,
            &MainFrame::onChannelSelected,
            this);

        meterTimer_.SetOwner(this);

        Bind(
            wxEVT_TIMER,
            &MainFrame::onMeterTimer,
            this);

        listeningButton_->Bind(
            wxEVT_BUTTON,
            &MainFrame::onListeningToggled,
            this);

        Bind(
            recognitionStateEvent,
            &MainFrame::onRecognitionState,
            this);

        Bind(
            recognitionTranscriptEvent,
            &MainFrame::onRecognitionTranscript,
            this);

        meterTimer_.Start(50);

        populateAudioDevices();

        Centre();
    }

    // Stops worker-owned recognition before destroying GUI event targets.
    MainFrame::~MainFrame()
    {
        meterTimer_.Stop();
        stopListening();
        audioEngine_.stop();
    }

    // Enumerates capture devices and selects the current system default.
    void MainFrame::populateAudioDevices()
    {
        deviceChoice_->Clear();
        channelChoice_->Clear();

        audioDevices_.clear();

        const auto result =
            audio::enumerateCaptureDevices();

        if (!result.ok())
        {
            statusText_->SetLabel(
                "Audio device discovery failed.");

            deviceChoice_->Enable(false);
            channelChoice_->Enable(false);

            return;
        }

        audioDevices_ =
            result.devices;

        if (audioDevices_.empty())
        {
            statusText_->SetLabel(
                "No capture devices found.");

            deviceChoice_->Enable(false);
            channelChoice_->Enable(false);

            return;
        }

        int defaultSelection = 0;

        for (std::size_t index = 0;
            index < audioDevices_.size();
            ++index)
        {
            const auto& device =
                audioDevices_[index];

            deviceChoice_->Append(
                wxString::FromUTF8(
                    device.name));

            if (device.isDefault)
            {
                defaultSelection =
                    static_cast<int>(index);
            }
        }

        deviceChoice_->SetSelection(
            defaultSelection);

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

        levelText_->SetLabel(
            "Peak: 0.0000   RMS: 0.0000");

        const int selection =
            deviceChoice_->GetSelection();

        if (selection == wxNOT_FOUND
            || selection < 0
            || static_cast<std::size_t>(
                selection)
            >= audioDevices_.size())
        {
            statusText_->SetLabel(
                "No capture device selected.");

            return;
        }

        const auto& device =
            audioDevices_[
                static_cast<std::size_t>(
                    selection)];

        audio::AudioCaptureConfig config;

        if (device.id.has_value())
        {
            config.deviceId = device.id;
        }
        else
        {
            config.runtimeDeviceIndex = device.runtimeIndex;
        }

        const auto startResult =
            audioEngine_.start(config);

        if (!startResult.ok())
        {
            statusText_->SetLabel(
                wxString::FromUTF8(
                    startResult.diagnostic->message));

            return;
        }

        for (std::uint32_t channel = 0;
            channel
            < startResult.format.channelCount;
            ++channel)
        {
            channelChoice_->Append(
                wxString::Format(
                    "Channel %u",
                    static_cast<unsigned int>(
                        channel + 1)));
        }

        if (startResult.format.channelCount > 0)
        {
            channelChoice_->SetSelection(0);
            channelChoice_->Enable(true);

            channelSelector_->setChannelIndex(0);
        }

        statusText_->SetLabel(
            wxString::Format(
                "Capturing at %u Hz, %u channel(s).",
                static_cast<unsigned int>(
                    startResult.format.sampleRate),
                static_cast<unsigned int>(
                    startResult.format.channelCount)));
    }

    // Restarts capture when the user chooses a different input device.
    void MainFrame::onDeviceSelected(
        wxCommandEvent&)
    {
        startSelectedDevice();
    }

    // Routes the selected input channel to metering and recognition consumers.
    void MainFrame::onChannelSelected(
        wxCommandEvent&)
    {
        const int selection =
            channelChoice_->GetSelection();

        if (selection == wxNOT_FOUND
            || selection < 0)
        {
            return;
        }

        channelSelector_->setChannelIndex(
            static_cast<std::size_t>(
                selection));

        meter_->reset();

        levelGauge_->SetValue(0);

        levelText_->SetLabel(
            "Peak: 0.0000   RMS: 0.0000");
    }

    // Refreshes the input meter from its thread-safe audio snapshot.
    void MainFrame::onMeterTimer(
        wxTimerEvent&)
    {
        const auto snapshot =
            meter_->snapshot();

        if (snapshot.channels.empty())
        {
            levelGauge_->SetValue(0);

            levelText_->SetLabel(
                "Peak: 0.0000   RMS: 0.0000");

            return;
        }

        const auto& level =
            snapshot.channels.front();

        const float peak =
            std::clamp(
                level.peak,
                0.0F,
                1.0F);

        const int gaugeValue =
            static_cast<int>(
                peak * 1000.0F);

        levelGauge_->SetValue(
            gaugeValue);

        levelText_->SetLabel(
            wxString::Format(
                "Peak: %.4f   RMS: %.4f",
                static_cast<double>(
                    level.peak),
                static_cast<double>(
                    level.rms)));
    }

    // Starts or stops recognition while capture and metering continue independently.
    void MainFrame::onListeningToggled(
        wxCommandEvent&)
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

        const std::string modelPath =
            modelPathText_->GetValue().ToStdString();
        if (modelPath.empty())
        {
            recognitionStatusText_->SetLabel(
                "Error: select a Whisper model.");
            return;
        }

        asr::WhisperAsrBackendOptions backendOptions;
        backendOptions.vadModelPath =
            std::filesystem::path(
                vadModelPathText_->GetValue().ToStdString());

        auto backend =
            std::make_shared<asr::WhisperAsrBackend>(backendOptions);
        recognitionObserver_ =
            std::make_shared<GuiRecognitionObserver>(*this);
        recognitionPipeline_ =
            std::make_unique<core::RecognitionPipeline>(
                channelSelector_,
                std::move(backend));
        recognitionPipeline_->setObserver(recognitionObserver_);

        asr::AsrConfiguration configuration;
        configuration.modelResourcePath = modelPath;
        const std::string language =
            languageText_->GetValue().ToStdString();
        if (!language.empty())
        {
            configuration.preferredLanguage = language;
        }
        configuration.recognitionHints =
            core::makeDefaultCommandRecognitionHints();

        modelPathText_->Enable(false);
        vadModelPathText_->Enable(false);
        languageText_->Enable(false);
        listeningButton_->Enable(false);
        recognitionStatusText_->SetLabel("Starting...");

        const auto result =
            recognitionPipeline_->start(configuration);
        if (!result.succeeded)
        {
            recognitionPipeline_->setObserver(nullptr);
            recognitionPipeline_.reset();
            recognitionObserver_.reset();
            modelPathText_->Enable(true);
            vadModelPathText_->Enable(true);
            languageText_->Enable(true);
            listeningButton_->Enable(true);
        }
    }

    // Applies a marshalled recognition state update to GUI controls.
    void MainFrame::onRecognitionState(
        wxThreadEvent& event)
    {
        const auto data =
            event.GetPayload<RecognitionStateEventData>();
        const wxString detail = wxString::FromUTF8(data.detail);

        if (data.state == core::RecognitionPipelineState::listening)
        {
            recognitionStatusText_->SetLabel("Listening");
            listeningButton_->SetLabel("Stop listening");
            listeningButton_->Enable(true);
            return;
        }

        if (data.state == core::RecognitionPipelineState::error)
        {
            recognitionStatusText_->SetLabel("Error: " + detail);
            listeningButton_->Enable(true);
            return;
        }

        recognitionStatusText_->SetLabel("Stopped");
    }

    // Displays raw and normalized transcripts in separate observable fields.
    void MainFrame::onRecognitionTranscript(
        wxThreadEvent& event)
    {
        const auto transcript =
            event.GetPayload<core::RecognitionTranscript>();
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
            modelPathText_->Enable(true);
            vadModelPathText_->Enable(true);
            languageText_->Enable(true);
            recognitionStatusText_->SetLabel("Stopped");
        }
    }

} // namespace timmy::gui
