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

#include "command_output_session.h"
#include "recognition_lifecycle.h"

#include "timmy/audio/audio_channel_selector.h"
#include "timmy/audio/audio_device_info.h"
#include "timmy/audio/audio_engine.h"
#include "timmy/audio/audio_meter.h"
#include "timmy/core/recognition_pipeline.h"
#include "timmy/core/asr_engine_factory.h"
#include "timmy/core/recognition_benchmark.h"

#include <wx/frame.h>
#include <wx/timer.h>

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <vector>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxCloseEvent;
class wxCommandEvent;
class wxGauge;
class wxWindow;
class wxBoxSizer;
class wxStaticText;
class wxTextCtrl;
class wxThreadEvent;
class wxTimerEvent;

namespace timmy::gui {

    class BenchmarkDialog;

    class MainFrame final : public wxFrame
    {
    public:
        MainFrame();
        // Frontends supplying PCM/backends can skip physical capture bootstrap.
        MainFrame(std::shared_ptr<asr::IAsrBackend> backend,
            CommandOutputSession::EmitterFactory emitterFactory, bool initializeAudio = true);
        ~MainFrame() override;

    private:
        class GuiRecognitionObserver;
        class BenchmarkResults;

        void createAudioColumn(wxWindow* parent, wxBoxSizer* columns);
        void createRecognitionColumn(wxWindow* parent, wxBoxSizer* columns);
        void createOutputColumn(wxWindow* parent, wxBoxSizer* columns);
        void populateAudioDevices();
        void populateRecognitionEngines();
        void resolveDefaultResources();
        void startSelectedDevice();
        void stopRecognition();
        void startRecognition();
        void startBenchmark();
        void openBenchmark();
        void closeBenchmark();
        void navigateBenchmark(bool retry);
        void recordBenchmarkNoResult();
        void showBenchmarkAttempt(const core::RecognitionBenchmarkAttempt& attempt);
        void updateBenchmarkControls();
        void drainBenchmarkResults();
        void pollShutdown();
        void updateRecognitionControls();
        void updateOutputControls();
        void updateOutputStatus();
        void armOutput();
        CommandOutputConfiguration outputConfiguration() const;
        void showExecutionResult(const core::RecognitionExecutionResult& result);

        void onDeviceSelected(wxCommandEvent& event);
        void onChannelSelected(wxCommandEvent& event);
        void onBrowseModel(wxCommandEvent& event);
        void onBrowseVadModel(wxCommandEvent& event);
        void onToggleRecognition(wxCommandEvent& event);
        void onRecognitionState(wxThreadEvent& event);
        void onRecognitionDiagnostic(wxThreadEvent& event);
        void onRecognitionTranscript(wxThreadEvent& event);
        void onOutputConfigurationChanged(wxCommandEvent& event);
        void onExecutionResult(wxThreadEvent& event);
        void onClose(wxCloseEvent& event);
        void onMeterTimer(wxTimerEvent& event);

        void resetRecognitionSettings();
        void refreshRecognitionSettings();
        void captureRecognitionSettings();
        asr::SettingValues recognitionSettings_;
        std::map<std::string, wxWindow*> tuningControls_;
        wxButton* resetRecognitionButton_ = nullptr;

        wxChoice* deviceChoice_ = nullptr;
        wxChoice* channelChoice_ = nullptr;
        wxGauge* levelGauge_ = nullptr;
        wxStaticText* levelText_ = nullptr;
        wxStaticText* audioStatusText_ = nullptr;
        wxChoice* engineChoice_ = nullptr;
        wxTextCtrl* modelPathText_ = nullptr;
        wxTextCtrl* vadPathText_ = nullptr;
        wxButton* modelBrowseButton_ = nullptr;
        wxButton* vadBrowseButton_ = nullptr;
        wxChoice* languageChoice_ = nullptr;
        wxButton* recognitionButton_ = nullptr;
        BenchmarkDialog* benchmarkDialog_ = nullptr;
        wxButton* benchmarkOpenButton_ = nullptr;
        wxStaticText* benchmarkPathSummary_ = nullptr;
        bool benchmarkCloseRequested_ = false;
        std::shared_ptr<core::RecognitionBenchmarkSession> benchmarkSession_;
        std::shared_ptr<BenchmarkResults> benchmarkResults_;
        enum class BenchmarkNavigation { none, next, retry };
        BenchmarkNavigation benchmarkNavigation_{BenchmarkNavigation::none};
        bool benchmarkStartRequested_{false};
        wxStaticText* recognitionStatusText_ = nullptr;
        wxStaticText* rawTranscriptText_ = nullptr;
        wxStaticText* normalizedTranscriptText_ = nullptr;
        wxStaticText* assemblyText_ = nullptr;
        wxStaticText* semanticCommandText_ = nullptr;
        wxCheckBox* outputEnabledCheck_ = nullptr;
        wxChoice* outputProfileChoice_ = nullptr;
        wxTextCtrl* outputHostText_ = nullptr;
        wxTextCtrl* outputPortText_ = nullptr;
        wxTextCtrl* hydraHoldText_ = nullptr;
        wxTextCtrl* hydraGapText_ = nullptr;
        wxCheckBox* hydraPreClearCheck_ = nullptr;
        wxStaticText* executionStatusText_ = nullptr;
        wxStaticText* executionResultText_ = nullptr;

        std::vector<audio::AudioDeviceInfo> audioDevices_;
        audio::AudioEngine audioEngine_;
        std::shared_ptr<audio::AudioChannelSelector> channelSelector_;
        std::shared_ptr<audio::AudioMeter> meter_;
        std::shared_ptr<asr::IAsrBackend> backendOverride_;
        std::unique_ptr<core::RecognitionPipeline> recognitionPipeline_;
        std::shared_ptr<GuiRecognitionObserver> recognitionObserver_;
        CommandOutputSession outputSession_;
        RecognitionLifecycle recognitionLifecycle_;
        CommandOutputConfiguration frozenOutputConfiguration_;
        std::uint64_t outputGeneration_ = 0;
        std::atomic<std::uint64_t> armedOutputGeneration_{0};
        bool closing_ = false;
        bool audioShutdownScheduled_ = false;
        wxString stopDiagnostic_;
        wxTimer meterTimer_;
    };

} // namespace timmy::gui
