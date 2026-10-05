/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "timmy/audio/audio_channel_selector.h"
#include "timmy/audio/audio_device_info.h"
#include "timmy/audio/audio_engine.h"
#include "timmy/audio/audio_meter.h"
#include "timmy/core/recognition_pipeline.h"

#include <wx/frame.h>
#include <wx/timer.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

class wxButton;
class wxChoice;
class wxCommandEvent;
class wxGauge;
class wxNotebook;
class wxStaticText;
class wxTextCtrl;
class wxThreadEvent;
class wxTimerEvent;

namespace timmy::gui {

    class MainFrame final : public wxFrame
    {
    public:
        MainFrame();
        ~MainFrame() override;

    private:
        class GuiRecognitionObserver;

        void createAudioPage(wxNotebook* notebook);
        void createRecognitionPage(wxNotebook* notebook);
        void populateAudioDevices();
        void populateRecognitionEngines();
        void resolveDefaultResources();
        void startSelectedDevice();
        void stopRecognition();
        void updateRecognitionControls(bool listening);

        void onDeviceSelected(wxCommandEvent& event);
        void onChannelSelected(wxCommandEvent& event);
        void onBrowseModel(wxCommandEvent& event);
        void onBrowseVadModel(wxCommandEvent& event);
        void onToggleRecognition(wxCommandEvent& event);
        void onRecognitionState(wxThreadEvent& event);
        void onRecognitionDiagnostic(wxThreadEvent& event);
        void onRecognitionTranscript(wxThreadEvent& event);
        void onMeterTimer(wxTimerEvent& event);

        wxChoice* deviceChoice_ = nullptr;
        wxChoice* channelChoice_ = nullptr;
        wxGauge* levelGauge_ = nullptr;
        wxStaticText* levelText_ = nullptr;
        wxStaticText* audioStatusText_ = nullptr;
        wxChoice* engineChoice_ = nullptr;
        wxTextCtrl* modelPathText_ = nullptr;
        wxTextCtrl* vadPathText_ = nullptr;
        wxChoice* languageChoice_ = nullptr;
        wxButton* recognitionButton_ = nullptr;
        wxStaticText* recognitionStatusText_ = nullptr;
        wxStaticText* rawTranscriptText_ = nullptr;
        wxStaticText* normalizedTranscriptText_ = nullptr;
        wxStaticText* assemblyText_ = nullptr;
        wxStaticText* semanticCommandText_ = nullptr;

        std::vector<audio::AudioDeviceInfo> audioDevices_;
        audio::AudioEngine audioEngine_;
        std::shared_ptr<audio::AudioChannelSelector> channelSelector_;
        std::shared_ptr<audio::AudioMeter> meter_;
        std::unique_ptr<core::RecognitionPipeline> recognitionPipeline_;
        std::shared_ptr<GuiRecognitionObserver> recognitionObserver_;
        std::uint64_t recognitionGeneration_ = 0;
        wxTimer meterTimer_;
    };

} // namespace timmy::gui
