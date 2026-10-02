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

#include <wx/choice.h>
#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace timmy::gui {

    MainFrame::MainFrame()
        : wxFrame(
            nullptr,
            wxID_ANY,
            "TIMMY",
            wxDefaultPosition,
            wxSize(640, 420))
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

        meterTimer_.Start(50);

        populateAudioDevices();

        Centre();
    }

    MainFrame::~MainFrame()
    {
        meterTimer_.Stop();
        audioEngine_.stop();
    }

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

    void MainFrame::startSelectedDevice()
    {
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

        config.runtimeDeviceIndex =
            device.runtimeIndex;

        const auto startResult =
            audioEngine_.start(config);

        if (!startResult.ok())
        {
            statusText_->SetLabel(
                wxString::FromUTF8(
                    startResult.errorMessage));

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

    void MainFrame::onDeviceSelected(
        wxCommandEvent&)
    {
        startSelectedDevice();
    }

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

} // namespace timmy::gui