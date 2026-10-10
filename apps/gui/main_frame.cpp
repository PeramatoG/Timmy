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
#include "benchmark_dialog.h"
#include "command_output_events.h"

#include "timmy/audio/audio_device_enumerator.h"
#include "timmy/core/asr_engine_factory.h"
#include "timmy/core/command_recognition_profile.h"
#include "timmy/core/resource_path_resolver.h"
#include "timmy/command/semantic_command.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/collpane.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stdpaths.h>
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
#include <limits>
#include <mutex>
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
            std::optional<core::RecognitionBenchmarkAttempt> benchmarkAttempt{};
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

    // Preserve presentation for recorded attempts even if Stop invalidates their
    // queued wx events. This frontend mailbox never changes Core result gating.
    class MainFrame::BenchmarkResults final
    {
    public:
        using Result = std::pair<core::RecognitionBenchmarkAttempt, core::RecognitionTranscript>;
        void add(const core::RecognitionBenchmarkAttempt& attempt,
            const core::RecognitionTranscript& transcript)
        {
            std::lock_guard lock(mutex_);
            results_.emplace_back(attempt, transcript);
        }
        std::vector<Result> take()
        {
            std::lock_guard lock(mutex_);
            std::vector<Result> results;
            results.swap(results_);
            return results;
        }
    private:
        std::mutex mutex_;
        std::vector<Result> results_;
    };

    class MainFrame::GuiRecognitionObserver final
        : public core::IRecognitionPipelineObserver
    {
    public:
        GuiRecognitionObserver(wxEvtHandler& target, std::uint64_t generation,
            const std::atomic<std::uint64_t>& armedOutputGeneration,
            std::shared_ptr<core::RecognitionBenchmarkSession> benchmark = {},
            std::shared_ptr<BenchmarkResults> benchmarkResults = {})
            : target_(target), generation_(generation),
              armedOutputGeneration_(armedOutputGeneration), benchmark_(std::move(benchmark)),
              benchmarkResults_(std::move(benchmarkResults))
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

        // Preserve worker timestamps before the GUI event queue introduces delay.
        void onRecognitionProgress(const asr::RecognitionProgress& progress) override
        { if (benchmark_) benchmark_->progress(progress); }

        // Queues raw and normalized UTF-8 transcript text for the GUI thread.
        void onRecognitionTranscript(const core::RecognitionTranscript& transcript) override
        {
            std::optional<core::RecognitionBenchmarkAttempt> attempt;
            if (benchmark_)
            {
                try
                {
                    attempt = benchmark_->record(transcript);
                    if (!attempt) return; // Review waits for explicit Next/Retry.
                    benchmarkResults_->add(*attempt, transcript);
                }
                catch (const std::exception& error)
                {
                    onRecognitionDiagnostic({asr::AsrDiagnosticCode::recognitionFailure,
                        std::string("Benchmark logging failed: ") + error.what()});
                    return;
                }
            }
            auto* event = new wxThreadEvent(wxEVT_THREAD, recognitionTranscriptEventId);
            event->SetPayload(TranscriptEventPayload{ generation_,
                benchmark_ ? 0 : armedOutputGeneration_.load(std::memory_order_acquire),
                transcript, attempt });
            wxQueueEvent(&target_, event);
        }

    private:
        wxEvtHandler& target_;
        std::uint64_t generation_;
        const std::atomic<std::uint64_t>& armedOutputGeneration_;
        const std::shared_ptr<core::RecognitionBenchmarkSession> benchmark_;
        const std::shared_ptr<BenchmarkResults> benchmarkResults_;
    };

    // Creates the native columns and initializes audio capture.
    MainFrame::MainFrame()
        : MainFrame(nullptr, {}, true)
    {
    }

    MainFrame::MainFrame(std::shared_ptr<asr::IAsrBackend> backend,
        CommandOutputSession::EmitterFactory emitterFactory, bool initializeAudio)
        : wxFrame(nullptr, wxID_ANY, "TIMMY", wxDefaultPosition, wxSize(1400, 850))
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
        recognitionSettings_ = asr::defaultSettings(
            core::asrSettingDescriptors(core::availableAsrEngines().front().id));
        auto* columns = new wxBoxSizer(wxHORIZONTAL);
        createAudioColumn(panel, columns);
        createRecognitionColumn(panel, columns);
        createOutputColumn(panel, columns);
        mainSizer->Add(columns, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        SetMinSize(wxSize(1100, 500));
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
        updateBenchmarkControls();
        Centre();
    }

    // Ordinary close waits asynchronously. Forced destruction still joins every
    // worker before destroying the event target or any referenced subsystem.
    MainFrame::~MainFrame()
    {
        meterTimer_.Stop();
        recognitionLifecycle_.join();
        if (benchmarkPreparation_.valid()) benchmarkPreparation_.wait();
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
    void MainFrame::createAudioColumn(wxWindow* parent, wxBoxSizer* columns)
    {
        auto* panel = new wxScrolledWindow(parent);
        panel->SetScrollRate(10, 10);
        panel->SetMinSize(wxSize(250, 200));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(panel, wxID_ANY, "Audio"), 0, wxALL, 8);
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
        levelGauge_->SetName("audio.level");
        levelGauge_->SetForegroundColour(wxColour(70, 180, 100));
        sizer->Add(levelGauge_, 0, wxEXPAND | wxALL, 12);
        levelText_ = new wxStaticText(panel, wxID_ANY, "Peak: 0.0000   RMS: 0.0000");
        sizer->Add(levelText_, 0, wxLEFT | wxRIGHT, 12);
        audioStatusText_ = new wxStaticText(panel, wxID_ANY,
            "Searching for capture devices...");
        sizer->Add(audioStatusText_, 0, wxALL, 12);
        panel->SetSizer(sizer);
        columns->Add(panel, 1, wxEXPAND | wxALL, 4);
    }

    // Builds the native controls for recognition resources and results.
    void MainFrame::createRecognitionColumn(wxWindow* parent, wxBoxSizer* columns)
    {
        auto* panel = new wxScrolledWindow(parent);
        panel->SetScrollRate(10, 10);
        panel->SetMinSize(wxSize(250, 200));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(panel, wxID_ANY, "Recognition"), 0, wxALL, 8);
        auto* selection = new wxStaticBoxSizer(wxVERTICAL, panel, "Recognition engine");
        auto* selectionOwner = selection->GetStaticBox();
        auto* resources = new wxFlexGridSizer(2, 4, 6);
        resources->AddGrowableCol(1);
        const auto addLabel = [&](const wxString& label) {
            resources->Add(new wxStaticText(selectionOwner, wxID_ANY, label), 0,
                wxALIGN_CENTER_VERTICAL);
        };
        auto addChoice = [&](const wxString& label, wxChoice*& choice) {
            addLabel(label);
            choice = new wxChoice(selectionOwner, wxID_ANY);
            resources->Add(choice, 1, wxEXPAND);
        };
        auto addPath = [&](const wxString& label, wxTextCtrl*& text, wxButton*& button,
                           void (MainFrame::*handler)(wxCommandEvent&)) {
            addLabel(label);
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            text = new wxTextCtrl(selectionOwner, wxID_ANY);
            row->Add(text, 1, wxEXPAND | wxRIGHT, 4);
            button = new wxButton(selectionOwner, wxID_ANY, "Browse...");
            button->Bind(wxEVT_BUTTON, handler, this);
            row->Add(button, 0);
            resources->Add(row, 1, wxEXPAND);
        };
        addChoice("Engine:", engineChoice_);
        addPath("ASR model:", modelPathText_, modelBrowseButton_, &MainFrame::onBrowseModel);
        addPath("VAD model:", vadPathText_, vadBrowseButton_, &MainFrame::onBrowseVadModel);
        addChoice("Language:", languageChoice_);
        selection->Add(resources, 0, wxEXPAND | wxALL, 4);
        auto* engineHint = new wxStaticText(selectionOwner, wxID_ANY,
            "Settings below belong to the selected engine.");
        selection->Add(engineHint, 0, wxALL, 4);
        sizer->Add(selection, 0, wxEXPAND | wxALL, 4);
        auto* session = new wxStaticBoxSizer(wxVERTICAL, panel, "Recognition session");
        auto* sessionOwner = session->GetStaticBox();
        recognitionButton_ = new wxButton(sessionOwner, wxID_ANY, "Start listening");
        recognitionButton_->Bind(wxEVT_BUTTON, &MainFrame::onToggleRecognition, this);
        auto* sessionRow = new wxBoxSizer(wxHORIZONTAL);
        sessionRow->Add(recognitionButton_, 0, wxRIGHT, 6);
        recognitionStatusText_ = new wxStaticText(sessionOwner, wxID_ANY, "Stopped.");
        recognitionStatusText_->SetMinSize(wxSize(1, -1));
        sessionRow->Add(recognitionStatusText_, 1, wxALIGN_CENTER_VERTICAL);
        session->Add(sessionRow, 0, wxEXPAND | wxALL, 4);
        rawTranscriptText_ = new wxStaticText(sessionOwner, wxID_ANY, "Raw transcript: -");
        session->Add(rawTranscriptText_, 0, wxEXPAND | wxALL, 4);
        auto* details = new wxCollapsiblePane(sessionOwner, wxID_ANY, "Transcript details",
            wxDefaultPosition, wxDefaultSize, wxCP_NO_TLW_RESIZE);
        auto* detailsOwner = details->GetPane();
        auto* detailsSizer = new wxBoxSizer(wxVERTICAL);
        normalizedTranscriptText_ = new wxStaticText(detailsOwner, wxID_ANY, "Normalized transcript: -");
        assemblyText_ = new wxStaticText(detailsOwner, wxID_ANY, "Assembly: -");
        semanticCommandText_ = new wxStaticText(detailsOwner, wxID_ANY, "Semantic command: -");
        for (auto* text : {normalizedTranscriptText_, assemblyText_, semanticCommandText_})
        {
            text->SetMinSize(wxSize(1, -1));
            detailsSizer->Add(text, 0, wxEXPAND | wxBOTTOM, 4);
        }
        detailsOwner->SetSizer(detailsSizer);
        session->Add(details, 0, wxEXPAND | wxALL, 4);
        details->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [panel](wxCollapsiblePaneEvent&) {
            panel->Layout();
            panel->FitInside();
        });
        sizer->Add(session, 0, wxEXPAND | wxALL, 4);
        auto* benchmark = new wxStaticBoxSizer(wxVERTICAL, panel, "Recognition benchmark");
        auto* owner = benchmark->GetStaticBox();
        benchmarkPathSummary_ = new wxStaticText(owner, wxID_ANY,
            "Guided live test. Select a JSONL file in the benchmark window.");
        benchmarkPathSummary_->SetMinSize(wxSize(1, -1));
        benchmark->Add(benchmarkPathSummary_, 0, wxEXPAND | wxALL, 4);
        benchmarkOpenButton_ = new wxButton(owner, wxID_ANY, "Open benchmark...");
        benchmarkOpenButton_->SetName("benchmark.open");
        benchmarkOpenButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { openBenchmark(); });
        benchmark->Add(benchmarkOpenButton_, 0, wxALL, 4);
        sizer->Add(benchmark, 0, wxEXPAND | wxALL, 4);
        wxStaticBoxSizer* sectionSizer = nullptr;
        wxFlexGridSizer* settingsGrid = nullptr;
        std::vector<wxFlexGridSizer*> settingsGrids;
        std::string section;
        for (const auto& descriptor : core::asrSettingDescriptors(core::availableAsrEngines().front().id))
        {
            if (section != descriptor.section)
            {
                section = descriptor.section;
                sectionSizer = new wxStaticBoxSizer(wxVERTICAL, panel,
                    sectionSizer ? wxString::FromUTF8(section) : wxString("Engine settings"));
                sectionSizer->GetStaticBox()->SetToolTip(wxString::FromUTF8(section));
                sizer->Add(sectionSizer, 0, wxEXPAND | wxALL, 4);
                settingsGrid = new wxFlexGridSizer(4, 3, 6);
                settingsGrid->AddGrowableCol(0);
                settingsGrid->AddGrowableCol(2);
                settingsGrids.push_back(settingsGrid);
                sectionSizer->Add(settingsGrid, 0, wxEXPAND | wxALL, 4);
            }
            auto* owner = sectionSizer->GetStaticBox();
            wxString label = wxString::FromUTF8(descriptor.label);
            label.Replace("Thread count (0 = automatic)", "Threads");
            label.Replace("Temperature increment", "Temp. increment");
            label.Replace("Compact vocabulary prompt", "Vocabulary prompt");
            label.Replace("Minimum", "Min.");
            label.Replace("Maximum", "Max.");
            auto* settingLabel = new wxStaticText(owner, wxID_ANY, label);
            settingLabel->SetToolTip(wxString::FromUTF8(descriptor.label));
            settingsGrid->Add(settingLabel, 0, wxALIGN_CENTER_VERTICAL);
            wxWindow* control = nullptr;
            const auto id = descriptor.id;
            if (std::holds_alternative<bool>(descriptor.defaultValue))
            {
                auto* check = new wxCheckBox(owner, wxID_ANY, "");
                check->Bind(wxEVT_CHECKBOX, [this, id, check](wxCommandEvent&) {
                    if (recognitionLifecycle_.canConfigure() && !closing_)
                        recognitionSettings_[id] = check->GetValue();
                });
                control = check;
            }
            else if (std::holds_alternative<int>(descriptor.defaultValue))
            {
                auto* spin = new wxSpinCtrl(owner, wxID_ANY);
                spin->SetRange(static_cast<int>(descriptor.minimum.value_or(std::numeric_limits<int>::min())),
                    static_cast<int>(descriptor.maximum.value_or(std::numeric_limits<int>::max())));
                spin->Bind(wxEVT_SPINCTRL, [this, id, spin](wxSpinEvent&) {
                    if (recognitionLifecycle_.canConfigure() && !closing_)
                        recognitionSettings_[id] = spin->GetValue();
                });
                control = spin;
            }
            else
            {
                auto* spin = new wxSpinCtrlDouble(owner, wxID_ANY);
                spin->SetRange(descriptor.minimum.value_or(std::numeric_limits<double>::lowest()),
                    descriptor.maximum.value_or(std::numeric_limits<double>::max()));
                spin->SetIncrement(0.1);
                spin->SetDigits(3);
                spin->Bind(wxEVT_SPINCTRLDOUBLE, [this, id, spin](wxSpinDoubleEvent&) {
                    if (recognitionLifecycle_.canConfigure() && !closing_)
                        recognitionSettings_[id] = spin->GetValue();
                });
                control = spin;
            }
            control->SetName(wxString::FromUTF8(id));
            tuningControls_.emplace(id, control);
            control->SetToolTip(wxString::FromUTF8(descriptor.label));
            if (!dynamic_cast<wxCheckBox*>(control)) control->SetMinSize(wxSize(120, -1));
            settingsGrid->Add(control, 0, wxALIGN_CENTER_VERTICAL);
        }
        // Pair short properties when there is space; keep one pair per row on
        // smaller windows rather than squeezing labels or numeric input.
        panel->Bind(wxEVT_SIZE, [panel, settingsGrids](wxSizeEvent& event) {
            const int columns = panel->GetClientSize().x >= 620 ? 4 : 2;
            for (auto* grid : settingsGrids)
            {
                if (grid->GetCols() == columns) continue;
                if (columns == 2) grid->RemoveGrowableCol(2);
                grid->SetCols(columns);
                if (columns == 4) grid->AddGrowableCol(2);
            }
            panel->Layout();
            panel->FitInside();
            event.Skip();
        });
        resetRecognitionButton_ = new wxButton(panel, wxID_ANY, "Reset to defaults");
        resetRecognitionButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            resetRecognitionSettings();
        });
        sizer->Add(resetRecognitionButton_, 0, wxALL, 8);
        refreshRecognitionSettings();
        panel->SetSizer(sizer);
        columns->Add(panel, 2, wxEXPAND | wxALL, 4);
    }

    // Edits frontend configuration; OSC mapping and execution remain in transport/Core.
    void MainFrame::createOutputColumn(wxWindow* parent, wxBoxSizer* columns)
    {
        const CommandOutputConfiguration defaults;
        auto* panel = new wxScrolledWindow(parent);
        panel->SetScrollRate(10, 10);
        panel->SetMinSize(wxSize(250, 200));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(new wxStaticText(panel, wxID_ANY, "Output"), 0, wxALL, 8);
        outputEnabledCheck_ = new wxCheckBox(panel, wxID_ANY, "Enable OSC command output");
        outputEnabledCheck_->SetValue(defaults.enabled);
        outputEnabledCheck_->SetToolTip(
            "Arm OSC while listening, or enable it for the next Start listening.");
        outputEnabledCheck_->Bind(wxEVT_CHECKBOX,
            &MainFrame::onOutputConfigurationChanged, this);
        sizer->Add(outputEnabledCheck_, 0, wxALL, 12);

        auto* grid = new wxFlexGridSizer(2, 4, 6);
        wxWindow* settingsOwner = panel;
        grid->AddGrowableCol(1);
        auto addLabel = [&](const wxString& label) {
            grid->Add(new wxStaticText(settingsOwner, wxID_ANY, label), 0,
                wxALIGN_CENTER_VERTICAL);
        };
        auto addText = [&](const wxString& label, wxTextCtrl*& text,
                           const std::string& value) {
            addLabel(label);
            text = new wxTextCtrl(settingsOwner, wxID_ANY, wxString::FromUTF8(value));
            grid->Add(text, 1, wxEXPAND);
        };
        auto* profileBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Output profile");
        auto* profileOwner = profileBox->GetStaticBox();
        auto* profileRow = new wxBoxSizer(wxHORIZONTAL);
        profileRow->Add(new wxStaticText(profileOwner, wxID_ANY, "Profile:"), 0,
            wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        outputProfileChoice_ = new wxChoice(profileOwner, wxID_ANY);
        outputProfileChoice_->Append("LT Hydra II");
        outputProfileChoice_->Append("Generic OSC");
        outputProfileChoice_->SetSelection(0);
        outputProfileChoice_->Bind(wxEVT_CHOICE,
            &MainFrame::onOutputConfigurationChanged, this);
        profileRow->Add(outputProfileChoice_, 1, wxEXPAND);
        profileBox->Add(profileRow, 0, wxEXPAND | wxALL, 4);
        profileBox->Add(new wxStaticText(profileOwner, wxID_ANY,
            "Settings below belong to this profile."), 0, wxALL, 4);
        sizer->Add(profileBox, 0, wxEXPAND | wxALL, 4);
        auto* settings = new wxStaticBoxSizer(wxVERTICAL, panel, "Profile settings");
        settingsOwner = settings->GetStaticBox();
        addText("Host:", outputHostText_, defaults.host);
        outputHostText_->SetHint("Console IP or hostname");
        addText("Port:", outputPortText_, defaults.port);
        addText("Key hold (ms):", hydraHoldText_, defaults.keyHoldMs);
        addText("Key gap (ms):", hydraGapText_, defaults.keyGapMs);
        const auto timingHint = wxString::Format("Maximum %u ms; default 30 ms",
            static_cast<unsigned int>(maximumHydraTimingMs));
        hydraHoldText_->SetToolTip(timingHint);
        hydraGapText_->SetToolTip(timingHint);
        addLabel("Pre-clear:");
        hydraPreClearCheck_ = new wxCheckBox(settingsOwner, wxID_ANY, "Clear before selection");
        hydraPreClearCheck_->SetValue(defaults.preClear);
        grid->Add(hydraPreClearCheck_, 0, wxALIGN_CENTER_VERTICAL);
        settings->Add(grid, 0, wxEXPAND | wxALL, 4);
        sizer->Add(settings, 0, wxEXPAND | wxALL, 4);

        executionStatusText_ = new wxStaticText(panel, wxID_ANY, "OSC output: Disarmed");
        executionResultText_ = new wxStaticText(panel, wxID_ANY, "Execution: -");
        sizer->Add(executionStatusText_, 0, wxEXPAND | wxALL, 12);
        sizer->Add(executionResultText_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        panel->SetSizer(sizer);
        columns->Add(panel, 1, wxEXPAND | wxALL, 4);
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

    void MainFrame::refreshRecognitionSettings()
    {
        for (const auto& [id, control] : tuningControls_)
        {
            const auto& value = recognitionSettings_.at(id);
            if (auto* check = dynamic_cast<wxCheckBox*>(control)) check->SetValue(std::get<bool>(value));
            else if (auto* spin = dynamic_cast<wxSpinCtrl*>(control)) spin->SetValue(std::get<int>(value));
            else if (auto* real = dynamic_cast<wxSpinCtrlDouble*>(control)) real->SetValue(std::get<double>(value));
        }
    }

    void MainFrame::captureRecognitionSettings()
    {
        auto values = recognitionSettings_;
        for (const auto& [id, control] : tuningControls_)
        {
            if (auto* check = dynamic_cast<wxCheckBox*>(control))
            {
                values[id] = check->GetValue();
            }
            else if (auto* spin = dynamic_cast<wxSpinCtrl*>(control))
            {
                long value = 0;
                if (!spin->GetTextValue().ToLong(&value)
                    || value < std::numeric_limits<int>::min()
                    || value > std::numeric_limits<int>::max())
                    throw std::invalid_argument("Invalid integer setting: " + id);
                values[id] = static_cast<int>(value);
            }
            else if (auto* real = dynamic_cast<wxSpinCtrlDouble*>(control))
            {
                double value = 0;
                if (!real->GetTextValue().ToDouble(&value))
                    throw std::invalid_argument("Invalid floating-point setting: " + id);
                values[id] = value;
            }
        }
        recognitionSettings_ = asr::validatedSettings(
            core::asrSettingDescriptors(core::availableAsrEngines().front().id), values);
    }

    void MainFrame::resetRecognitionSettings()
    {
        if (!recognitionLifecycle_.canConfigure() || closing_) return;
        recognitionSettings_ = asr::defaultSettings(
            core::asrSettingDescriptors(core::availableAsrEngines().front().id));
        refreshRecognitionSettings();
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
        const auto closingBenchmark = benchmarkSession_;
        if (!recognitionLifecycle_.beginStop([this, closeAudio, closingBenchmark] {
            std::string diagnostic;
            if (benchmarkPreparation_.valid())
            {
                try { benchmarkPreparation_.get(); }
                catch (const std::exception& error) { diagnostic = error.what(); }
            }
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
            if (closingBenchmark) closingBenchmark->close();
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
        if (benchmarkSession_)
        {
            drainBenchmarkResults();
            // Shutdown has joined all recognition callbacks before closing/reset.
            try { benchmarkSession_->close(); }
            catch (const std::exception& error) { stopDiagnostic_ = wxString::FromUTF8(error.what()); }
            benchmarkSession_.reset();
            benchmarkResults_.reset();
        }
        updateRecognitionControls();
        updateOutputStatus();
        const auto& diagnostic = recognitionLifecycle_.diagnostic();
        recognitionStatusText_->SetLabel(!diagnostic.empty()
            ? wxString::FromUTF8(diagnostic)
            : (stopDiagnostic_.empty() ? wxString("Stopped.") : stopDiagnostic_));
        if (benchmarkDialog_)
        {
            benchmarkDialog_->stopped(!diagnostic.empty() ? wxString::FromUTF8(diagnostic) : stopDiagnostic_);
            if (benchmarkCloseRequested_) { benchmarkDialog_->Hide(); benchmarkCloseRequested_ = false; }
        }
        stopDiagnostic_.clear();
    }

    // One policy freezes input/resources throughout listening and stopping,
    // regardless of whether command output is armed.
    void MainFrame::updateRecognitionControls()
    {
        const bool idle = recognitionLifecycle_.canConfigure() && !closing_ && !benchmarkSession_;
        const bool stopping = recognitionLifecycle_.stopping() || closing_;
        recognitionButton_->SetLabel(stopping ? "Stopping..."
            : (recognitionLifecycle_.listening() ? "Stop listening" : "Start listening"));
        recognitionButton_->Enable(!stopping && !benchmarkSession_);
        deviceChoice_->Enable(idle && !audioDevices_.empty());
        channelChoice_->Enable(idle && channelChoice_->GetCount() > 0);
        engineChoice_->Enable(idle);
        modelPathText_->Enable(idle);
        vadPathText_->Enable(idle);
        modelBrowseButton_->Enable(idle);
        vadBrowseButton_->Enable(idle);
        languageChoice_->Enable(idle);
        for (const auto& [id, control] : tuningControls_) control->Enable(idle);
        resetRecognitionButton_->Enable(idle);
        updateBenchmarkControls();
        updateOutputControls();
    }

    // Freezes output options for the whole recognition/execution lifecycle.
    void MainFrame::updateOutputControls()
    {
        const bool idle = recognitionLifecycle_.canConfigure() && !closing_ && !benchmarkSession_;
        const bool hydra = idle && outputProfileChoice_->GetSelection() == 0;
        outputEnabledCheck_->Enable(!recognitionLifecycle_.stopping() && !closing_ && !benchmarkSession_);
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
        if (benchmarkSession_ || benchmarkStartRequested_) return;
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
        if (benchmarkSession_ || benchmarkStartRequested_)
        {
            outputEnabledCheck_->SetValue(false);
            return;
        }
        if (recognitionLifecycle_.stopping() || closing_) return;
        if (recognitionLifecycle_.listening())
        {
            if (!benchmarkSession_ && outputEnabledCheck_->GetValue()) armOutput();
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

    void MainFrame::startBenchmark()
    {
        if (!benchmarkDialog_ || !recognitionLifecycle_.canStart() || benchmarkSession_ || closing_) return;
        if (benchmarkDialog_->resultPath().empty())
        {
            benchmarkDialog_->showError("Select a JSONL result file first.");
            return;
        }
        if (wxStringToPath(benchmarkDialog_->resultPath()).extension() != ".jsonl")
        {
            benchmarkDialog_->showError("Result file must have a .jsonl extension.");
            return;
        }
        armedOutputGeneration_.store(0, std::memory_order_release);
        ++outputGeneration_;
        outputSession_.disarm();
        outputEnabledCheck_->SetValue(false);
        benchmarkStartRequested_ = true;
        startRecognition();
        benchmarkStartRequested_ = false;
    }

    void MainFrame::prepareBenchmarkAttempt()
    {
        if (benchmarkPreparation_.valid()) return;
        const auto session = benchmarkSession_;
        auto* pipeline = recognitionPipeline_.get();
        benchmarkPreparation_ = std::async(std::launch::async, [session, pipeline] {
            return session->prepare(*pipeline);
        });
        updateBenchmarkControls();
    }

    void MainFrame::pollBenchmarkAttempt()
    {
        if (!benchmarkSession_ || recognitionLifecycle_.stopping()) return;
        try
        {
            if (benchmarkPreparation_.valid()
                && benchmarkPreparation_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                const auto result = benchmarkPreparation_.get();
                if (!result.succeeded) throw std::runtime_error(result.diagnostic
                    ? result.diagnostic->message : "Attempt preparation failed.");
            }
            if (const auto timedOut = benchmarkSession_->tick(*recognitionPipeline_))
                showBenchmarkAttempt(*timedOut);
            if (benchmarkSession_->attemptState() == core::BenchmarkAttemptState::result)
                recognitionPipeline_->disarmAttempt();
            updateBenchmarkControls();
        }
        catch (const std::exception& error)
        {
            stopDiagnostic_ = wxString::FromUTF8(error.what());
            stopRecognition();
        }
    }

    void MainFrame::showBenchmarkAttempt(const core::RecognitionBenchmarkAttempt& attempt)
    {
        benchmarkDialog_->showAttempt(attempt);
        updateBenchmarkControls();
    }

    void MainFrame::recordBenchmarkNoResult()
    {
        if (!benchmarkSession_ || !recognitionLifecycle_.listening() || closing_
            || !benchmarkSession_->awaitingResult()) return;
        try
        {
            const auto attempt = benchmarkSession_->recordNoResult(recognitionPipeline_->attemptDiagnostics());
            if (!attempt) return; // A final result may have won the worker race.
            rawTranscriptText_->SetLabel("Raw transcript: No ASR result");
            normalizedTranscriptText_->SetLabel("Normalized transcript: -");
            assemblyText_->SetLabel("Assembly: not attempted");
            semanticCommandText_->SetLabel("Semantic command: not attempted");
            showBenchmarkAttempt(*attempt);
        }
        catch (const std::exception& error)
        {
            stopDiagnostic_ = "Benchmark logging failed: " + wxString::FromUTF8(error.what());
            stopRecognition();
        }
    }

    void MainFrame::navigateBenchmark(bool retry)
    {
        if (!benchmarkSession_ || !recognitionLifecycle_.listening() || closing_
            || benchmarkSession_->awaitingResult()) return;
        if (!retry && benchmarkSession_->index() + 1 == core::liveBenchmarkCases().size()) return;
        if (retry) benchmarkSession_->retry();
        else if (!benchmarkSession_->next()) return;
        benchmarkDialog_->setCase(benchmarkSession_->index());
        prepareBenchmarkAttempt();
    }

    void MainFrame::openBenchmark()
    {
        if (closing_) return;
        benchmarkCloseRequested_ = false;
        if (!benchmarkDialog_)
        {
            benchmarkDialog_ = new BenchmarkDialog(this, {
                [this] { startBenchmark(); },
                [this] {
                    if (!benchmarkSession_ || recognitionLifecycle_.stopping() || closing_) return;
                    stopRecognition();
                },
                [this] { navigateBenchmark(false); },
                [this] { navigateBenchmark(true); },
                [this] { recordBenchmarkNoResult(); },
                [this] { closeBenchmark(); },
                [this] {
                    if (benchmarkSession_ && !recognitionLifecycle_.stopping() && !closing_)
                        benchmarkSession_->startAttempt();
                    updateBenchmarkControls();
                },
                [this](const wxString& path) {
                    benchmarkPathSummary_->SetLabel(path.empty() ? wxString("JSONL file: not selected") : "JSONL: " + wxFileName(path).GetFullName());
                    benchmarkPathSummary_->SetToolTip(path);
                    benchmarkPathSummary_->Wrap(std::max(100, benchmarkPathSummary_->GetParent()->GetClientSize().x - 16));
                    benchmarkPathSummary_->GetParent()->Layout();
                }
            });
        }
        updateBenchmarkControls();
        benchmarkDialog_->Show();
        benchmarkDialog_->Raise();
    }

    void MainFrame::closeBenchmark()
    {
        if (!benchmarkDialog_ || closing_) return;
        if (!benchmarkSession_) { benchmarkDialog_->Hide(); return; }
        benchmarkCloseRequested_ = true;
        stopRecognition();
    }

    void MainFrame::updateBenchmarkControls()
    {
        benchmarkOpenButton_->Enable(!closing_);
        if (!benchmarkDialog_) return;
        drainBenchmarkResults();
        const bool active = benchmarkSession_ != nullptr;
        benchmarkDialog_->updateControls(recognitionLifecycle_.canConfigure() && !closing_,
            active, recognitionLifecycle_.listening(), recognitionLifecycle_.stopping() || closing_,
            active && benchmarkSession_->awaitingResult(),
            active && benchmarkSession_->index() + 1 == core::liveBenchmarkCases().size(),
            active ? benchmarkSession_->attemptState() : core::BenchmarkAttemptState::closed);
    }

    void MainFrame::drainBenchmarkResults()
    {
        if (!benchmarkResults_ || !benchmarkDialog_) return;
        for (const auto& [attempt, transcript] : benchmarkResults_->take())
        {
            benchmarkDialog_->showDetails(transcript);
            benchmarkDialog_->showAttempt(attempt);
        }
    }

    // Starts or stops one generation of recognition from the selected channel.
    void MainFrame::onToggleRecognition(wxCommandEvent&)
    {
        if (benchmarkSession_ || recognitionLifecycle_.stopping() || closing_) return;
        if (recognitionLifecycle_.listening())
        {
            stopRecognition();
            return;
        }
        startRecognition();
    }

    void MainFrame::startRecognition()
    {
        if (!recognitionLifecycle_.canStart() || closing_) return;
        try
        {
            if (!benchmarkSession_) captureRecognitionSettings();
        }
        catch (const std::exception& error)
        {
            recognitionStatusText_->SetLabel(wxString::FromUTF8(error.what()));
            if (benchmarkStartRequested_ && benchmarkDialog_) benchmarkDialog_->showError(wxString::FromUTF8(error.what()));
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
            if (benchmarkSession_) resources.voiceActivityModelPath = benchmarkSession_->snapshot().vadModelPath;
            const auto& settings = benchmarkSession_ ? benchmarkSession_->snapshot().settings : recognitionSettings_;
            auto backend = backendOverride_ ? backendOverride_
                : core::createAsrBackend(core::availableAsrEngines().front().id, resources, settings);
            const auto languageProfile = benchmarkSession_ ? benchmarkSession_->snapshot().commandProfile
                : commandProfileForOutput(frozenOutputConfiguration_);
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
            if (benchmarkSession_) configuration = benchmarkSession_->snapshot().configuration;
            if (benchmarkStartRequested_)
            {
                benchmarkSession_ = std::make_shared<core::RecognitionBenchmarkSession>(
                    wxStringToPath(benchmarkDialog_->resultPath()),
                    core::RecognitionBenchmarkSnapshot{backendOverride_ ? "injected" : "whisper.cpp", configuration,
                        resources.voiceActivityModelPath, settings, languageProfile,
                        {deviceChoice_->GetStringSelection().ToStdString(wxConvUTF8), channelSelector_->channelIndex(),
                            channelChoice_->GetSelection() == wxNOT_FOUND ? std::nullopt
                                : std::optional<std::string>{channelChoice_->GetStringSelection().ToStdString(wxConvUTF8)}}});
                benchmarkStartRequested_ = false;
                benchmarkResults_ = std::make_shared<BenchmarkResults>();
                benchmarkDialog_->beginSession(benchmarkSession_->snapshot());
            }
            if (benchmarkSession_) benchmarkDialog_->setCase(benchmarkSession_->index());
            recognitionPipeline_ = std::make_unique<core::RecognitionPipeline>(
                channelSelector_, std::move(backend), languageProfile, benchmarkSession_ != nullptr, benchmarkSession_ != nullptr, audioEngine_.captureStatsSource());
            recognitionObserver_ = std::make_shared<GuiRecognitionObserver>(*this, generation,
                armedOutputGeneration_, benchmarkSession_, benchmarkResults_);
            recognitionPipeline_->setObserver(recognitionObserver_);
            updateRecognitionControls();
            const auto result = recognitionPipeline_->start(configuration);
            if (!result.succeeded)
            {
                stopDiagnostic_ = result.diagnostic
                    ? wxString::FromUTF8(result.diagnostic->message)
                    : wxString("Recognition could not start.");
                stopRecognition();
                return;
            }
            if (benchmarkSession_) prepareBenchmarkAttempt();
            else if (outputEnabledCheck_->GetValue()) armOutput();
            updateOutputStatus();
        }
        catch (const std::exception& error)
        {
            benchmarkStartRequested_ = false;
            stopDiagnostic_ = "Recognition could not start: "
                + wxString::FromUTF8(error.what());
            stopRecognition();
        }
        catch (...)
        {
            benchmarkStartRequested_ = false;
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
        if (benchmarkSession_)
        {
            stopDiagnostic_ = wxString::FromUTF8(payload.diagnostic.message);
            stopRecognition();
        }
    }

    // Displays current-session UTF-8 raw and normalized transcripts.
    void MainFrame::onRecognitionTranscript(wxThreadEvent& event)
    {
        const auto payload = event.GetPayload<TranscriptEventPayload>();
        if (!recognitionLifecycle_.accepts(payload.generation))
        {
            return;
        }
        if (benchmarkSession_ && payload.transcript.generation != benchmarkSession_->generation()) return;
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
        if (benchmarkSession_ && payload.benchmarkAttempt)
        {
            updateBenchmarkControls();
        }

        // Submission only enqueues the produced semantic command. The existing
        // executor validates the event and performs dispatch/OSC on its worker.
        // A transcript produced while disarmed stays observable even if its wx
        // event is processed after a later arm. Only the current arm may submit.
        if (!benchmarkSession_ && !payload.benchmarkAttempt
            && payload.outputGeneration != 0 && payload.outputGeneration == outputGeneration_)
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
        pollBenchmarkAttempt();
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
