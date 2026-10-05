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

#include <wx/frame.h>
#include <wx/timer.h>

#include <cstdint>
#include <memory>
#include <vector>

class wxChoice;
class wxCommandEvent;
class wxGauge;
class wxButton;
class wxNotebook;
class wxPanel;
class wxStaticText;
class wxTextCtrl;
class wxThreadEvent;
class wxTimerEvent;

namespace timmy::asr {

    struct AsrEngineInfo;

} // namespace timmy::asr

namespace timmy::core {

    class IRecognitionPipelineObserver;
    class RecognitionPipeline;

} // namespace timmy::core

namespace timmy::gui {

    class MainFrame final
        : public wxFrame
    {
    public:
        MainFrame();
        ~MainFrame() override;

    private:
        void populateAudioDevices();
        void startSelectedDevice();
        wxPanel* createAudioPage(wxNotebook& notebook);
        wxPanel* createRecognitionPage(wxNotebook& notebook);
        void populateRecognitionEngines();

        void onDeviceSelected(
            wxCommandEvent& event);

        void onChannelSelected(
            wxCommandEvent& event);

        void onMeterTimer(
            wxTimerEvent& event);

        void onListeningToggled(
            wxCommandEvent& event);

        void onRecognitionEngineSelected(
            wxCommandEvent& event);

        void onBrowseModel(
            wxCommandEvent& event);

        void onBrowseVadModel(
            wxCommandEvent& event);

        void onRecognitionState(
            wxThreadEvent& event);

        void onRecognitionTranscript(
            wxThreadEvent& event);

        void stopListening();

        wxChoice* deviceChoice_ = nullptr;
        wxChoice* channelChoice_ = nullptr;
        wxChoice* recognitionEngineChoice_ = nullptr;
        wxChoice* languageChoice_ = nullptr;

        wxGauge* levelGauge_ = nullptr;

        wxTextCtrl* modelPathText_ = nullptr;
        wxTextCtrl* vadModelPathText_ = nullptr;
        wxButton* listeningButton_ = nullptr;
        wxButton* modelBrowseButton_ = nullptr;
        wxButton* vadBrowseButton_ = nullptr;

        wxStaticText* levelText_ = nullptr;
        wxStaticText* statusText_ = nullptr;
        wxStaticText* recognitionStatusText_ = nullptr;
        wxStaticText* rawTranscriptText_ = nullptr;
        wxStaticText* normalizedTranscriptText_ = nullptr;

        std::vector<audio::AudioDeviceInfo>
            audioDevices_;

        std::vector<asr::AsrEngineInfo>
            recognitionEngines_;

        audio::AudioEngine audioEngine_;

        std::shared_ptr<audio::AudioChannelSelector>
            channelSelector_;

        std::shared_ptr<audio::AudioMeter>
            meter_;

        std::shared_ptr<core::IRecognitionPipelineObserver>
            recognitionObserver_;

        std::unique_ptr<core::RecognitionPipeline>
            recognitionPipeline_;

        std::uint64_t recognitionGeneration_ = 0;

        wxTimer meterTimer_;
    };

} // namespace timmy::gui
