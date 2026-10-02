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

#include <memory>
#include <vector>

class wxChoice;
class wxCommandEvent;
class wxGauge;
class wxStaticText;
class wxTimerEvent;

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

        void onDeviceSelected(
            wxCommandEvent& event);

        void onChannelSelected(
            wxCommandEvent& event);

        void onMeterTimer(
            wxTimerEvent& event);

        wxChoice* deviceChoice_ = nullptr;
        wxChoice* channelChoice_ = nullptr;

        wxGauge* levelGauge_ = nullptr;

        wxStaticText* levelText_ = nullptr;
        wxStaticText* statusText_ = nullptr;

        std::vector<audio::AudioDeviceInfo>
            audioDevices_;

        audio::AudioEngine audioEngine_;

        std::shared_ptr<audio::AudioChannelSelector>
            channelSelector_;

        std::shared_ptr<audio::AudioMeter>
            meter_;

        wxTimer meterTimer_;
    };

} // namespace timmy::gui