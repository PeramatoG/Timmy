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

#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/dialog.h>
#include <wx/collpane.h>
#include <wx/gauge.h>
#include <wx/sizer.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace timmy;
using namespace std::chrono_literals;

namespace {

    void require(bool condition, const char* expression, int line)
    {
        if (condition) return;
        std::cerr << "Line " << line << ": " << expression << '\n';
        std::abort();
    }

#define REQUIRE(expression) require((expression), #expression, __LINE__)

    void pumpEvents()
    {
        wxTheApp->ProcessPendingEvents();
        wxTheApp->Yield(true);
        wxTheApp->ProcessIdle();
    }

    wxString displayedText(const wxStaticText& label)
    {
        auto text = label.GetLabel();
        text.Replace("\n", " ");
        return text;
    }

    template<class Predicate>
    void waitFor(Predicate ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!ready() && std::chrono::steady_clock::now() < deadline)
        {
            pumpEvents();
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE(ready());
    }

    template<class Control>
    void collect(wxWindow& parent, std::vector<Control*>& controls)
    {
        for (auto* child : parent.GetChildren())
        {
            if (child->GetName().StartsWith("benchmark.") && !dynamic_cast<wxStaticText*>(child)) continue;
            if (child->GetName().StartsWith("whisper.") || child->GetName().StartsWith("vad.")) continue;
            if (auto* button = dynamic_cast<wxButton*>(child);
                button && button->GetLabel() == "Reset to defaults") continue;
            if (auto* control = dynamic_cast<Control*>(child)) controls.push_back(control);
            collect(*child, controls);
        }
    }

    // Drive the controls' production handlers, without a real input device or
    // model. The same wx event queue carries every fake backend notification.
    void click(wxButton& button)
    {
        wxCommandEvent event(wxEVT_BUTTON, button.GetId());
        event.SetEventObject(&button);
        button.GetEventHandler()->ProcessEvent(event);
    }

    void toggle(wxCheckBox& checkbox, bool enabled)
    {
        checkbox.SetValue(enabled);
        wxCommandEvent event(wxEVT_CHECKBOX, checkbox.GetId());
        event.SetEventObject(&checkbox);
        event.SetInt(enabled);
        checkbox.GetEventHandler()->ProcessEvent(event);
    }

    class FakeBackend final : public asr::IAsrBackend
    {
    public:
        asr::AsrCapabilities capabilities() const override { return {}; }
        asr::AsrState state() const override { return state_.load(); }
        asr::AsrOperationResult start(const asr::AsrConfiguration& configuration) override
        {
            configurations.push_back(configuration);
            ++starts;
            state_ = asr::AsrState::running;
            return {true, std::nullopt};
        }
        asr::AsrOperationResult stop() override
        {
            std::unique_lock lock(mutex_);
            stopEntered = true;
            changed_.wait(lock, [&] { return !blockStop_; });
            state_ = asr::AsrState::stopped;
            return {true, std::nullopt};
        }
        void emit(std::string transcript)
        {
            publishResult({std::move(transcript), asr::AsrResultKind::final, {}});
        }
        void emitDiagnostic()
        {
            publishDiagnostic({asr::AsrDiagnosticCode::recognitionFailure,
                "obsolete recognition diagnostic"});
        }
        void blockStop()
        {
            std::lock_guard lock(mutex_);
            stopEntered = false;
            blockStop_ = true;
        }
        void releaseStop()
        {
            std::lock_guard lock(mutex_);
            blockStop_ = false;
            changed_.notify_all();
        }

        std::vector<asr::AsrConfiguration> configurations;
        std::atomic<int> starts{0};
        std::atomic<bool> stopEntered{false};

    private:
        void processAudioBlock(const audio::AudioBlockView&) override {}
        std::atomic<asr::AsrState> state_{asr::AsrState::stopped};
        std::mutex mutex_;
        std::condition_variable changed_;
        bool blockStop_ = false;
    };

    class BlockingEmitter final : public transport::ICommandEmitter
    {
    public:
        transport::EmissionResult emit(const command::SemanticCommand&) override
        {
            std::unique_lock lock(mutex_);
            ++calls;
            changed_.wait(lock, [&] { return !blocked_; });
            ++completed;
            return {transport::EmissionState::succeeded, {}};
        }
        void block()
        {
            std::lock_guard lock(mutex_);
            blocked_ = true;
        }
        void release()
        {
            std::lock_guard lock(mutex_);
            blocked_ = false;
            changed_.notify_all();
        }

        std::atomic<int> calls{0};
        std::atomic<int> completed{0};

    private:
        std::mutex mutex_;
        std::condition_variable changed_;
        bool blocked_ = false;
    };

    struct FrameFixture
    {
        FrameFixture()
        {
            frame = new gui::MainFrame(backend, [this](auto, auto) {
                ++factories;
                return emitter;
            }, false);
            frame->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent& event) {
                if (event.GetEventObject() == frame) destroyed = true;
                event.Skip();
            });
            collect(*frame, choices);
            collect(*frame, texts);
            collect(*frame, buttons);
            collect(*frame, checkboxes);
            collect(*frame, labels);
            REQUIRE(choices.size() == 5 && texts.size() == 6);
            REQUIRE(buttons.size() == 3 && checkboxes.size() == 2);
            // Simulate selectable audio inputs without opening any microphone.
            choices[0]->Append("Deterministic input");
            choices[0]->SetSelection(0);
            choices[0]->Enable();
            choices[1]->Append("Channel 1");
            choices[1]->SetSelection(0);
            choices[1]->Enable();
            frame->Show();
            pumpEvents();
            for (const auto& descriptor : core::asrSettingDescriptors(core::AsrEngineId::whisperCpp))
            {
                auto* control = wxWindow::FindWindowByName(wxString::FromUTF8(descriptor.id), frame);
                REQUIRE(control != nullptr && control->IsEnabled());
                tuning.push_back(control);
            }
            REQUIRE(tuning.size() == 14);
        }
        ~FrameFixture()
        {
            backend->releaseStop();
            emitter->release();
            if (!destroyed)
            {
                frame->Close();
                waitFor([&] { return destroyed; });
            }
        }
        wxStaticText& label(const wxString& prefix)
        {
            if (prefix == "Last result:") {
                auto* result = dynamic_cast<wxStaticText*>(wxWindow::FindWindowByName("benchmark.result", frame));
                REQUIRE(result != nullptr);
                return *result;
            }
            for (auto* label : labels)
                if (label->GetLabel().StartsWith(prefix)) return *label;
            REQUIRE(false);
            std::abort();
        }
        wxButton& recognition() { return *buttons[2]; }
        wxCheckBox& output() { return *checkboxes[0]; }
        wxTextCtrl& host() { return *texts[2]; }
        void listeningControls()
        {
            REQUIRE(recognition().IsEnabled());
            REQUIRE(recognition().GetLabel() == "Stop listening");
            for (auto* choice : choices) REQUIRE(!choice->IsEnabled());
            for (auto* text : texts) REQUIRE(!text->IsEnabled());
            REQUIRE(!buttons[0]->IsEnabled() && !buttons[1]->IsEnabled());
            REQUIRE(!checkboxes[1]->IsEnabled());
            REQUIRE(output().IsEnabled());
            for (auto* control : tuning) REQUIRE(!control->IsEnabled());
            REQUIRE(!reset().IsEnabled());
        }
        wxButton& reset()
        {
            auto* button = dynamic_cast<wxButton*>(wxWindow::FindWindowByLabel("Reset to defaults", frame));
            REQUIRE(button != nullptr);
            return *button;
        }
        void responsive()
        {
            bool dispatched = false;
            frame->CallAfter([&] { dispatched = true; });
            waitFor([&] { return dispatched; });
            REQUIRE(!destroyed);
        }

        std::shared_ptr<FakeBackend> backend = std::make_shared<FakeBackend>();
        std::shared_ptr<BlockingEmitter> emitter = std::make_shared<BlockingEmitter>();
        gui::MainFrame* frame = nullptr;
        bool destroyed = false;
        int factories = 0;
        std::vector<wxWindow*> tuning;
        std::vector<wxChoice*> choices;
        std::vector<wxTextCtrl*> texts;
        std::vector<wxButton*> buttons;
        std::vector<wxCheckBox*> checkboxes;
        std::vector<wxStaticText*> labels;
    };

    void runtimeArmingAndAsyncShutdown()
    {
        FrameFixture fixture;
        REQUIRE(!fixture.output().GetValue());
        REQUIRE(fixture.output().IsEnabled());
        REQUIRE(fixture.label("OSC output:").GetLabel() == "OSC output: Disarmed");
        for (auto* choice : fixture.choices) REQUIRE(choice->IsEnabled());
        for (auto* text : fixture.texts) REQUIRE(text->IsEnabled());
        fixture.host().ChangeValue("192.0.2.5");
        click(fixture.recognition());
        fixture.listeningControls();
        pumpEvents();

        // A produced command is visible with output OFF. Arming before the
        // queued wx transcript arrives cannot turn it into a dispatch.
        fixture.backend->emit("Channel 5 At 50");
        toggle(fixture.output(), true);
        fixture.listeningControls();
        pumpEvents();
        REQUIRE(fixture.label("Raw transcript:").GetLabel() == "Raw transcript: Channel 5 At 50");
        REQUIRE(fixture.label("Semantic command:").GetLabel().Contains("produced"));
        REQUIRE(displayedText(fixture.label("Semantic command:")).Contains("Validation: accepted"));
        REQUIRE(displayedText(fixture.label("Semantic command:")).Contains("Rendered command: CH 5 AT 5 0"));
        const auto& stages = fixture.label("Semantic command:");
        REQUIRE(stages.GetBestSize().GetWidth() + stages.GetPosition().x
            <= stages.GetParent()->GetClientSize().GetWidth());
        REQUIRE(fixture.emitter->calls == 0 && fixture.factories == 1);
        REQUIRE(fixture.label("OSC output:").GetLabel() == "OSC output: Armed");

        fixture.emitter->block();
        fixture.backend->emit("Channel 6 At 60");
        waitFor([&] { return fixture.emitter->calls == 1; });
        toggle(fixture.output(), false);
        fixture.listeningControls();
        fixture.responsive();
        REQUIRE(fixture.emitter->completed == 0);
        REQUIRE(fixture.label("OSC output:").GetLabel() == "OSC output: Disarmed");
        fixture.backend->emit("Channel 7 At 70");
        pumpEvents();
        REQUIRE(fixture.label("Raw transcript:").GetLabel() == "Raw transcript: Channel 7 At 70");
        REQUIRE(fixture.emitter->calls == 1);
        fixture.emitter->release();
        waitFor([&] { return fixture.label("Execution:").GetLabel().Contains("Succeeded"); });
        REQUIRE(displayedText(fixture.label("Execution:")).Contains("Rendered command: CH 6 AT 6 0"));
        REQUIRE(fixture.emitter->completed == 1);

        toggle(fixture.output(), true);
        fixture.backend->emit("Channel 8 At 80");
        waitFor([&] { return fixture.emitter->completed == 2; });
        REQUIRE(fixture.factories == 1);
        pumpEvents();
        const auto lastRaw = fixture.label("Raw transcript:").GetLabel();
        fixture.backend->blockStop();
        fixture.backend->emit("Channel 9 At 90");
        fixture.backend->emitDiagnostic();
        click(fixture.recognition());
        REQUIRE(fixture.recognition().GetLabel() == "Stopping...");
        REQUIRE(!fixture.recognition().IsEnabled() && !fixture.output().IsEnabled());
        click(fixture.recognition());
        REQUIRE(fixture.backend->starts == 1);
        waitFor([&] { return fixture.backend->stopEntered.load(); });
        fixture.responsive();
        REQUIRE(fixture.recognition().GetLabel() == "Stopping...");
        REQUIRE(fixture.label("Raw transcript:").GetLabel() == lastRaw);
        for (auto* choice : fixture.choices) REQUIRE(!choice->IsEnabled());
        for (auto* text : fixture.texts) REQUIRE(!text->IsEnabled());
        fixture.backend->releaseStop();
        waitFor([&] { return fixture.recognition().IsEnabled(); });
        REQUIRE(fixture.recognition().GetLabel() == "Start listening");
        REQUIRE(fixture.label("Raw transcript:").GetLabel() == lastRaw);
        REQUIRE(fixture.host().IsEnabled());

        // Closing has the same nonblocking contract: callbacks and queued wx
        // work retain a live frame until shutdown ownership has been released.
        toggle(fixture.output(), false);
        click(fixture.recognition());
        // Leave this generation's starting/listening state events queued. Close
        // must invalidate them before the event loop processes the backlog.
        fixture.backend->blockStop();
        fixture.frame->Close();
        waitFor([&] { return fixture.backend->stopEntered.load(); });
        REQUIRE(!fixture.destroyed);
        REQUIRE(fixture.recognition().GetLabel() == "Stopping...");
        fixture.responsive();
        fixture.backend->releaseStop();
        waitFor([&] { return fixture.destroyed; });
        pumpEvents();
    }

    void invalidArmKeepsRecognitionListening()
    {
        FrameFixture fixture;
        click(fixture.recognition());
        pumpEvents();
        toggle(fixture.output(), true);
        fixture.listeningControls();
        REQUIRE(!fixture.output().GetValue());
        REQUIRE(fixture.factories == 0);
        REQUIRE(fixture.label("Execution:").GetLabel().Contains("Failed"));
        REQUIRE(fixture.label("OSC output:").GetLabel() == "OSC output: Disarmed");
        fixture.backend->emit("Channel 2 At 20");
        pumpEvents();
        REQUIRE(fixture.label("Raw transcript:").GetLabel() == "Raw transcript: Channel 2 At 20");
        REQUIRE(fixture.label("Semantic command:").GetLabel().Contains("produced"));
        REQUIRE(fixture.backend->starts == 1 && fixture.emitter->calls == 0);
    }

    void unsupportedRenderingRemainsObservable()
    {
        FrameFixture fixture;
        fixture.host().ChangeValue("192.0.2.5");
        click(fixture.recognition());
        toggle(fixture.output(), true);
        pumpEvents();

        // All is a valid semantic selection, but Hydra has no verified key
        // mapping. Keep the parsed intent and rejection visible without output.
        fixture.backend->emit("fuera todo");
        pumpEvents();
        const auto semantic = displayedText(fixture.label("Semantic command:"));
        REQUIRE(semantic.Contains("SetIntensity(Channel(All), 0%)"));
        REQUIRE(semantic.Contains("Validation: accepted"));
        REQUIRE(semantic.Contains("Rendering:") && semantic.Contains("Hydra II"));
        REQUIRE(!semantic.Contains("Rendered command:"));
        REQUIRE(fixture.label("Execution:").GetLabel().Contains("Failed"));
        REQUIRE(fixture.emitter->calls == 0);
        REQUIRE(fixture.output().GetValue() && fixture.backend->starts == 1);
        fixture.listeningControls();

        // An unsupported rendering must not disable subsequent valid commands.
        fixture.backend->emit("enciende canal 1");
        waitFor([&] { return fixture.label("Execution:").GetLabel().Contains("Succeeded"); });
        REQUIRE(fixture.emitter->calls == 1);
        REQUIRE(displayedText(fixture.label("Semantic command:")).Contains("Rendered command: CH 1 AT AT"));
        REQUIRE(displayedText(fixture.label("Execution:")).Contains("Rendered command: CH 1 AT AT"));
    }

    void linguisticContextResetsAcrossArms()
    {
        FrameFixture fixture;
        fixture.host().ChangeValue("192.0.2.5");
        fixture.emitter->release();
        click(fixture.recognition());
        toggle(fixture.output(), true);
        fixture.backend->emit("Select channels 1 through 10");
        waitFor([&] { return fixture.emitter->completed == 1; });
        fixture.backend->emit("add 4");
        waitFor([&] { return fixture.emitter->completed == 2; });

        toggle(fixture.output(), false);
        fixture.backend->emit("remove 10");
        pumpEvents();
        REQUIRE(fixture.label("Assembly:").GetLabel().Contains("rejected"));
        REQUIRE(fixture.emitter->calls == 2);

        // Text remains observable while disarmed. A fresh arm resets even a
        // target explicitly mentioned during that observation-only interval.
        fixture.backend->emit("Select group 3");
        pumpEvents();
        REQUIRE(fixture.label("Semantic command:").GetLabel().Contains("produced"));
        toggle(fixture.output(), true);
        fixture.backend->emit("add 4");
        pumpEvents();
        REQUIRE(fixture.label("Assembly:").GetLabel().Contains("rejected"));
        REQUIRE(fixture.emitter->calls == 2);

        fixture.backend->emit("Select fixture 7");
        waitFor([&] { return fixture.emitter->completed == 3; });
        fixture.backend->emit("remove 7");
        waitFor([&] { return fixture.emitter->completed == 4; });
        REQUIRE(fixture.factories == 1);
    }

    void recognitionTuningAndColumns()
    {
        FrameFixture fixture;
        std::vector<wxNotebook*> notebooks;
        collect(*fixture.frame, notebooks);
        REQUIRE(notebooks.empty());
        std::vector<wxScrolledWindow*> columns;
        collect(*fixture.frame, columns);
        REQUIRE(columns.size() == 3);
        for (auto* column : columns) REQUIRE(column->IsShownOnScreen());
        REQUIRE(columns[0]->GetPosition().x < columns[1]->GetPosition().x);
        REQUIRE(columns[1]->GetPosition().x < columns[2]->GetPosition().x);
        auto* launcher = wxWindow::FindWindowByName("benchmark.open", fixture.frame);
        REQUIRE(launcher != nullptr);
        REQUIRE(launcher->GetScreenPosition().y + launcher->GetSize().y
            <= columns[1]->GetScreenPosition().y + columns[1]->GetClientSize().y);
        auto checkDefaults = [&] {
            for (const auto& descriptor : core::asrSettingDescriptors(core::AsrEngineId::whisperCpp))
            {
                auto* control = wxWindow::FindWindowByName(wxString::FromUTF8(descriptor.id), fixture.frame);
                if (auto* check = dynamic_cast<wxCheckBox*>(control))
                    REQUIRE(check->GetValue() == std::get<bool>(descriptor.defaultValue));
                else if (auto* spin = dynamic_cast<wxSpinCtrl*>(control))
                    REQUIRE(spin->GetValue() == std::get<int>(descriptor.defaultValue));
                else if (auto* real = dynamic_cast<wxSpinCtrlDouble*>(control))
                    REQUIRE(real->GetValue() == std::get<double>(descriptor.defaultValue));
                else REQUIRE(false);
            }
        };
        checkDefaults();
        auto* settingLabel = wxWindow::FindWindowByLabel("Threads", fixture.frame);
        REQUIRE(settingLabel != nullptr);
        auto* propertyGrid = dynamic_cast<wxFlexGridSizer*>(settingLabel->GetContainingSizer());
        REQUIRE(propertyGrid != nullptr && propertyGrid->GetCols() == 4);
        REQUIRE(wxWindow::FindWindowByLabel("Engine settings", fixture.frame) != nullptr);
        REQUIRE(wxWindow::FindWindowByLabel("Output profile", fixture.frame) != nullptr);
        REQUIRE(wxWindow::FindWindowByLabel("Profile settings", fixture.frame) != nullptr);
        auto* meter = dynamic_cast<wxGauge*>(wxWindow::FindWindowByName("audio.level", fixture.frame));
        REQUIRE(meter != nullptr && meter->GetForegroundColour().Green() > meter->GetForegroundColour().Red());
        auto* threads = dynamic_cast<wxSpinCtrl*>(wxWindow::FindWindowByName("whisper.threads", fixture.frame));
        REQUIRE(threads != nullptr);
        const auto model = fixture.texts[0]->GetValue();
        const auto vad = fixture.texts[1]->GetValue();
        const auto language = fixture.choices[3]->GetSelection();
        const auto host = fixture.host().GetValue();
        for (const auto& descriptor : core::asrSettingDescriptors(core::AsrEngineId::whisperCpp))
        {
            auto* control = wxWindow::FindWindowByName(wxString::FromUTF8(descriptor.id), fixture.frame);
            if (auto* check = dynamic_cast<wxCheckBox*>(control))
                toggle(*check, !std::get<bool>(descriptor.defaultValue));
            else if (auto* spin = dynamic_cast<wxSpinCtrl*>(control))
            {
                spin->SetValue(std::get<int>(descriptor.defaultValue) + 1);
                wxSpinEvent changed(wxEVT_SPINCTRL, spin->GetId());
                changed.SetEventObject(spin);
                spin->GetEventHandler()->ProcessEvent(changed);
            }
            else if (auto* real = dynamic_cast<wxSpinCtrlDouble*>(control))
            {
                real->SetValue(std::get<double>(descriptor.defaultValue) / 2);
                wxSpinDoubleEvent changed(wxEVT_SPINCTRLDOUBLE, real->GetId());
                changed.SetEventObject(real);
                real->GetEventHandler()->ProcessEvent(changed);
            }
        }
        click(fixture.reset());
        REQUIRE(fixture.texts[0]->GetValue() == model && fixture.texts[1]->GetValue() == vad);
        REQUIRE(fixture.choices[3]->GetSelection() == language && fixture.host().GetValue() == host);
        checkDefaults();
        fixture.frame->SetSize(1100, 500);
        pumpEvents();
        REQUIRE(propertyGrid->GetCols() == 2);
        for (auto* column : columns) REQUIRE(column->IsShownOnScreen());
        click(fixture.recognition());
        waitFor([&] { return fixture.backend->starts == 1; });
        fixture.listeningControls();
        // Even a synthetic event cannot reset settings during a session.
        threads->SetValue(7);
        click(fixture.reset());
        REQUIRE(threads->GetValue() == 7);
        fixture.backend->blockStop();
        click(fixture.recognition());
        waitFor([&] { return fixture.backend->stopEntered.load(); });
        for (auto* control : fixture.tuning) REQUIRE(!control->IsEnabled());
        REQUIRE(!fixture.reset().IsEnabled());
        fixture.backend->releaseStop();
        waitFor([&] { return fixture.recognition().GetLabel() == "Start listening"; });
        for (auto* control : fixture.tuning) REQUIRE(control->IsEnabled());
        REQUIRE(fixture.reset().IsEnabled());
        click(fixture.reset());
        checkDefaults();
        click(fixture.recognition());
        waitFor([&] { return fixture.backend->starts == 2; });
        fixture.listeningControls();
    }

    template<class Control>
    Control& benchmarkControl(FrameFixture& fixture, const char* name)
    {
        if (!wxWindow::FindWindowByName("benchmark.window", fixture.frame))
        {
            auto* launcher = dynamic_cast<wxButton*>(wxWindow::FindWindowByName("benchmark.open", fixture.frame));
            REQUIRE(launcher != nullptr);
            click(*launcher);
        }
        auto* control = dynamic_cast<Control*>(wxWindow::FindWindowByName(name, fixture.frame));
        REQUIRE(control != nullptr);
        return *control;
    }
    std::vector<std::string> resultLines(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line)) lines.push_back(line);
        return lines;
    }
    void guidedBenchmarkControlsAndIsolation()
    {
        FrameFixture fixture;
        const auto path = std::filesystem::temp_directory_path() / "timmy-gui-benchmark.jsonl";
        std::filesystem::remove(path);
        auto& start = benchmarkControl<wxButton>(fixture, "benchmark.start");
        auto& stop = benchmarkControl<wxButton>(fixture, "benchmark.stop");
        auto& next = benchmarkControl<wxButton>(fixture, "benchmark.next");
        auto& retry = benchmarkControl<wxButton>(fixture, "benchmark.retry");
        auto& file = benchmarkControl<wxTextCtrl>(fixture, "benchmark.path");
        REQUIRE(start.IsEnabled() && !stop.IsEnabled() && !next.IsEnabled() && !retry.IsEnabled());
        click(start); // Missing result path must not start recognition.
        REQUIRE(fixture.backend->starts == 0);
        file.ChangeValue(wxString::FromUTF8(path.string()));
        fixture.host().ChangeValue("192.0.2.5");
        toggle(fixture.output(), true); // Benchmark start must override pre-arming.
        click(start);
        REQUIRE(fixture.backend->starts == 1);
        pumpEvents();
        REQUIRE(!fixture.recognition().IsEnabled() && stop.IsEnabled());
        REQUIRE(!start.IsEnabled() && !file.IsEnabled());
        REQUIRE(!fixture.output().IsEnabled() && !fixture.output().GetValue());
        REQUIRE(fixture.factories == 0 && fixture.emitter->calls == 0);
        REQUIRE(!next.IsEnabled() && !retry.IsEnabled());
        for (auto* control : fixture.tuning) REQUIRE(!control->IsEnabled());
        for (auto* text : fixture.texts) REQUIRE(!text->IsEnabled());
        toggle(fixture.output(), true); // Synthetic events cannot bypass the guard.
        click(fixture.recognition());
        REQUIRE(!fixture.output().GetValue() && fixture.factories == 0 && fixture.backend->starts == 1);
        const auto frozen = fixture.backend->configurations.front();
        fixture.texts[0]->ChangeValue("synthetic-changed-model.bin");
        fixture.choices[3]->SetSelection(2);
        auto* threads = dynamic_cast<wxSpinCtrl*>(wxWindow::FindWindowByName("whisper.threads", fixture.frame));
        REQUIRE(threads != nullptr);
        threads->SetValue(7);
        click(fixture.reset());
        fixture.backend->emit("Channel 1 At 50");
        REQUIRE(resultLines(path).size() == 1); // Logging precedes the wx event.
        fixture.backend->emit("Channel 5 At 50"); // Review does not consume a case.
        pumpEvents();
        REQUIRE(resultLines(path).size() == 1 && next.IsEnabled() && retry.IsEnabled());
        REQUIRE(fixture.label("Last result:").GetLabel().Contains("es-num-1 attempt 1 PASS"));
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.state").GetLabel() == "Waiting for operator decision");
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.instruction").GetLabel().Contains("Next or Retry"));
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.counts").GetLabel() == "PASS: 1   FAIL: 0   No result: 0");
        const auto passColour = fixture.label("Last result:").GetForegroundColour();
        REQUIRE(passColour.Green() > passColour.Red());
        const auto frozenSummary = benchmarkControl<wxStaticText>(fixture, "benchmark.summary").GetLabel();
        REQUIRE(frozenSummary.Contains("Frozen engine: injected") && frozenSummary.Contains("Language: es"));
        REQUIRE(benchmarkControl<wxTextCtrl>(fixture, "benchmark.detailsText").GetValue().Contains("Raw transcript: Channel 1 At 50"));
        fixture.backend->blockStop();
        click(retry);
        waitFor([&] { return fixture.backend->stopEntered.load(); });
        fixture.responsive();
        REQUIRE(!retry.IsEnabled() && !next.IsEnabled() && !fixture.output().IsEnabled());
        fixture.backend->emit("Channel 5 At 50"); // Previous generation remains gated.
        fixture.backend->releaseStop();
        waitFor([&] { return fixture.backend->starts == 2; });
        REQUIRE(fixture.backend->configurations.back().modelResourcePath == frozen.modelResourcePath);
        REQUIRE(fixture.backend->configurations.back().preferredLanguage == frozen.preferredLanguage);
        REQUIRE(!next.IsEnabled() && !retry.IsEnabled());
        fixture.backend->emit("Channel 5 At 50");
        pumpEvents();
        REQUIRE(fixture.label("Last result:").GetLabel().Contains("es-num-1 attempt 2 FAIL"));
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.summary").GetLabel() == frozenSummary);
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.counts").GetLabel() == "PASS: 1   FAIL: 1   No result: 0");
        const auto failColour = fixture.label("Last result:").GetForegroundColour();
        REQUIRE(failColour.Red() > failColour.Green());
        auto lines = resultLines(path);
        REQUIRE(lines.size() == 2);
        REQUIRE(lines.back().find("\"attempt_number\":2") != std::string::npos);
        REQUIRE(lines.back().find("\"whisper.threads\":0") != std::string::npos);
        REQUIRE(lines.back().find("synthetic-changed-model") == std::string::npos);
        REQUIRE(lines.back().find("\"selected_language\":\"es\"") != std::string::npos);
        const auto sessionField = lines.front().substr(lines.front().find("\"session_id\":"));
        const auto sessionId = sessionField.substr(0, sessionField.find(","));
        for (std::size_t index = 1; index < core::liveBenchmarkCases().size(); ++index)
        {
            click(next);
            waitFor([&] { return fixture.backend->starts == static_cast<int>(index + 2); });
            fixture.backend->emit(core::liveBenchmarkCases()[index].phrase);
            pumpEvents();
            REQUIRE(fixture.label("Last result:").GetLabel().Contains("PASS"));
            lines = resultLines(path);
            REQUIRE(lines.back().find(sessionId) != std::string::npos);
            REQUIRE(lines.back().find("\"attempt_number\":1") != std::string::npos);
            REQUIRE(fixture.emitter->calls == 0 && fixture.factories == 0);
        }
        REQUIRE(!next.IsEnabled() && retry.IsEnabled());
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.state").GetLabel() == "Finished");
        REQUIRE(resultLines(path).size() == core::liveBenchmarkCases().size() + 1);
        click(stop);
        waitFor([&] { return start.IsEnabled(); });
        REQUIRE(fixture.recognition().IsEnabled() && file.IsEnabled() && fixture.output().IsEnabled());
        REQUIRE(resultLines(path).size() == core::liveBenchmarkCases().size() + 1);
        // Ordinary recognition and output can be started after benchmark cleanup.
        click(fixture.recognition());
        toggle(fixture.output(), true);
        fixture.backend->emit("Channel 1 At 50");
        waitFor([&] { return fixture.emitter->completed == 1; });
        REQUIRE(fixture.factories == 1 && resultLines(path).size() == core::liveBenchmarkCases().size() + 1);
        std::filesystem::remove(path);
    }

    void benchmarkStopPreservesUnprocessedResultAndRecoversFailure()
    {
        FrameFixture fixture;
        auto& start = benchmarkControl<wxButton>(fixture, "benchmark.start");
        auto& stop = benchmarkControl<wxButton>(fixture, "benchmark.stop");
        auto& file = benchmarkControl<wxTextCtrl>(fixture, "benchmark.path");
        const auto path = std::filesystem::temp_directory_path() / "timmy-gui-stop-benchmark.jsonl";
        std::filesystem::remove(path);
        file.ChangeValue(wxString::FromUTF8((path / "missing.jsonl").string()));
        click(start);
        waitFor([&] { return start.IsEnabled(); });
        REQUIRE(fixture.backend->starts == 0 && fixture.factories == 0);
        file.ChangeValue(wxString::FromUTF8(path.string()));
        click(start);
        fixture.backend->emit("Channel 1 At 50");
        // Stop before pumping the transcript event: worker logging still survives.
        click(stop);
        waitFor([&] { return start.IsEnabled(); });
        REQUIRE(resultLines(path).size() == 1);
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.counts").GetLabel() == "PASS: 1   FAIL: 0   No result: 0");
        REQUIRE(fixture.label("Last result:").GetLabel().Contains("attempt 1 PASS"));
        click(start);
        fixture.backend->emit("Channel 1 At 50");
        pumpEvents();
        REQUIRE(resultLines(path).size() == 2); // A new session appends safely.
        REQUIRE(fixture.label("Last result:").GetLabel().Contains("attempt 1 PASS"));
        fixture.backend->blockStop();
        fixture.frame->Close();
        waitFor([&] { return fixture.backend->stopEntered.load(); });
        fixture.responsive();
        REQUIRE(!fixture.destroyed);
        fixture.backend->releaseStop();
        waitFor([&] { return fixture.destroyed; });
        REQUIRE(resultLines(path).size() == 2 && fixture.emitter->calls == 0);
        std::filesystem::remove(path);
    }

    void benchmarkNoResultAndFrozenAudioSource()
    {
        FrameFixture fixture;
        const auto path = std::filesystem::temp_directory_path() / "timmy-gui-no-result.jsonl";
        std::filesystem::remove(path);
        auto& start = benchmarkControl<wxButton>(fixture, "benchmark.start");
        auto& stop = benchmarkControl<wxButton>(fixture, "benchmark.stop");
        auto& next = benchmarkControl<wxButton>(fixture, "benchmark.next");
        auto& retry = benchmarkControl<wxButton>(fixture, "benchmark.retry");
        auto& noResult = benchmarkControl<wxButton>(fixture, "benchmark.noResult");
        auto& file = benchmarkControl<wxTextCtrl>(fixture, "benchmark.path");
        file.ChangeValue(wxString::FromUTF8(path.string()));
        REQUIRE(!noResult.IsEnabled());
        click(noResult); // Synthetic idle actions cannot create attempts.
        REQUIRE(!std::filesystem::exists(path));
        fixture.choices[0]->SetString(0, "Input \"USB\"");
        fixture.choices[1]->Append("Channel 2");
        fixture.choices[1]->SetSelection(1);
        wxCommandEvent channelChanged(wxEVT_CHOICE, fixture.choices[1]->GetId());
        channelChanged.SetEventObject(fixture.choices[1]);
        fixture.choices[1]->GetEventHandler()->ProcessEvent(channelChanged);
        click(start);
        REQUIRE(noResult.IsEnabled() && !next.IsEnabled() && !retry.IsEnabled());
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.state").GetLabel().Contains("Listening"));
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.instruction").GetLabel().Contains("Speak the displayed phrase once"));
        REQUIRE(!fixture.choices[0]->IsEnabled() && !fixture.choices[1]->IsEnabled());
        fixture.choices[0]->SetString(0, "synthetic-changed-device");
        fixture.choices[1]->SetString(1, "synthetic-changed-channel");
        // No final recognition arrives; an explicit operator outcome releases navigation.
        click(noResult);
        REQUIRE(fixture.label("Last result:").GetLabel().Contains("attempt 1 FAIL (No ASR result)"));
        REQUIRE(fixture.label("Raw transcript:").GetLabel().Contains("No ASR result"));
        REQUIRE(!noResult.IsEnabled() && next.IsEnabled() && retry.IsEnabled());
        REQUIRE(resultLines(path).size() == 1);
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.counts").GetLabel() == "PASS: 0   FAIL: 1   No result: 1");
        click(noResult);
        fixture.backend->emit("Channel 1 At 50"); // Late results cannot overwrite the manual outcome.
        pumpEvents();
        REQUIRE(resultLines(path).size() == 1);
        REQUIRE(fixture.label("Last result:").GetLabel().Contains("FAIL (No ASR result)"));
        fixture.backend->blockStop();
        click(retry);
        waitFor([&] { return fixture.backend->stopEntered.load(); });
        REQUIRE(!noResult.IsEnabled() && !next.IsEnabled() && !retry.IsEnabled());
        click(noResult); // Synthetic actions during shutdown cannot log an attempt.
        REQUIRE(resultLines(path).size() == 1);
        fixture.backend->releaseStop();
        waitFor([&] { return fixture.backend->starts == 2; });
        REQUIRE(noResult.IsEnabled() && !next.IsEnabled());
        fixture.backend->emit("Channel 1 At 50");
        click(noResult); // Final result recorded on the worker wins before its wx event is processed.
        pumpEvents();
        REQUIRE(fixture.label("Last result:").GetLabel().Contains("attempt 2 PASS"));
        REQUIRE(!fixture.label("Last result:").GetLabel().Contains("No ASR result"));
        REQUIRE(resultLines(path).size() == 2);
        for (std::size_t index = 1; index < core::liveBenchmarkCases().size(); ++index)
        {
            click(next);
            waitFor([&] { return fixture.backend->starts == static_cast<int>(index + 2); });
            REQUIRE(noResult.IsEnabled());
            click(noResult);
            const bool positive = core::liveBenchmarkCases()[index].expected.has_value();
            REQUIRE(fixture.label("Last result:").GetLabel().Contains(positive ? "FAIL (No ASR result)" : "PASS (No ASR result)"));
            REQUIRE(!noResult.IsEnabled() && retry.IsEnabled());
            const auto line = resultLines(path).back();
            REQUIRE(line.find("\"recognition_outcome\":\"no_result\"") != std::string::npos);
            REQUIRE(line.find("\"raw_transcript\":null") != std::string::npos);
            REQUIRE(line.find("\"channel_index\":1") != std::string::npos);
            REQUIRE(line.find("\"channel_display_name\":\"Channel 2\"") != std::string::npos);
            REQUIRE(line.find("synthetic-changed") == std::string::npos);
            REQUIRE(line.find("USB") != std::string::npos);
            REQUIRE(fixture.factories == 0 && fixture.emitter->calls == 0);
        }
        REQUIRE(!next.IsEnabled());
        click(retry);
        waitFor([&] { return fixture.backend->starts == static_cast<int>(core::liveBenchmarkCases().size() + 2); });
        click(noResult);
        REQUIRE(fixture.label("Last result:").GetLabel().Contains("attempt 2 PASS (No ASR result)"));
        REQUIRE(resultLines(path).size() == core::liveBenchmarkCases().size() + 2);
        click(stop);
        waitFor([&] { return start.IsEnabled(); });
        REQUIRE(!noResult.IsEnabled());
        // The benchmark-only action remains inert during ordinary armed listening.
        fixture.host().ChangeValue("192.0.2.5");
        click(fixture.recognition());
        toggle(fixture.output(), true);
        click(noResult);
        fixture.backend->emit("Channel 1 At 50");
        waitFor([&] { return fixture.emitter->completed == 1; });
        REQUIRE(fixture.factories == 1 && resultLines(path).size() == core::liveBenchmarkCases().size() + 2);
        std::filesystem::remove(path);
    }

    void dedicatedBenchmarkOpenCloseAndLayout()
    {
        FrameFixture fixture;
        auto* open = dynamic_cast<wxButton*>(wxWindow::FindWindowByName("benchmark.open", fixture.frame));
        REQUIRE(open != nullptr);
        REQUIRE(wxWindow::FindWindowByName("benchmark.start", fixture.frame) == nullptr);
        click(*open);
        auto& dialog = benchmarkControl<wxDialog>(fixture, "benchmark.window");
        pumpEvents();
        REQUIRE(dialog.IsShown());
        auto& start = benchmarkControl<wxButton>(fixture, "benchmark.start");
        REQUIRE(wxGetTopLevelParent(&start) == &dialog);
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.state").GetLabel() == "Ready to start");
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.corpus").GetLabel().Contains(core::liveBenchmarkCorpusVersion));
        auto& details = benchmarkControl<wxCollapsiblePane>(fixture, "benchmark.details");
        REQUIRE(details.IsCollapsed());
        const auto path = std::filesystem::temp_directory_path() / "timmy-gui-dialog-close.jsonl";
        std::filesystem::remove(path);
        auto& file = benchmarkControl<wxTextCtrl>(fixture, "benchmark.path");
        file.SetValue(wxString::FromUTF8(path.string()));
        click(start);
        pumpEvents();
        details.Collapse(false);
        wxCollapsiblePaneEvent expanded(&details, details.GetId(), false);
        details.GetEventHandler()->ProcessEvent(expanded);
        dialog.SetSize(520, 480);
        pumpEvents();
        for (const auto* name : {"benchmark.start", "benchmark.stop", "benchmark.next",
                "benchmark.retry", "benchmark.noResult", "benchmark.close"})
        {
            auto& button = benchmarkControl<wxButton>(fixture, name);
            REQUIRE(button.IsShownOnScreen());
            REQUIRE(button.GetRect().GetRight() <= dialog.GetClientSize().x);
            REQUIRE(button.GetRect().GetBottom() <= dialog.GetClientSize().y);
        }
        fixture.backend->blockStop();
        dialog.Close(); // Title-bar close waits for the same asynchronous shutdown.
        waitFor([&] { return fixture.backend->stopEntered.load(); });
        REQUIRE(dialog.IsShown() && !start.IsEnabled());
        REQUIRE(!fixture.texts[0]->IsEnabled() && !fixture.host().IsEnabled());
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.state").GetLabel().Contains("Stopping"));
        fixture.responsive();
        fixture.backend->releaseStop();
        waitFor([&] { return !dialog.IsShown(); });
        REQUIRE(fixture.texts[0]->IsEnabled() && fixture.host().IsEnabled());
        REQUIRE(fixture.factories == 0 && fixture.emitter->calls == 0);
        click(*open);
        REQUIRE(wxWindow::FindWindowByName("benchmark.window", fixture.frame) == &dialog);
        REQUIRE(dialog.IsShown() && file.GetValue() == wxString::FromUTF8(path.string()));
        REQUIRE(benchmarkControl<wxStaticText>(fixture, "benchmark.state").GetLabel() == "Stopped");
        click(benchmarkControl<wxButton>(fixture, "benchmark.close"));
        REQUIRE(!dialog.IsShown());
        std::filesystem::remove(path);
    }

    class TestApp final : public wxApp
    {
    public:
        bool OnInit() override
        {
            SetExitOnFrameDelete(false);
            return true;
        }
    };

} // namespace

wxIMPLEMENT_APP_NO_MAIN(TestApp);

int main(int argc, char** argv)
{
    REQUIRE(wxEntryStart(argc, argv));
    REQUIRE(wxTheApp->CallOnInit());
    dedicatedBenchmarkOpenCloseAndLayout();
    benchmarkNoResultAndFrozenAudioSource();
    guidedBenchmarkControlsAndIsolation();
    benchmarkStopPreservesUnprocessedResultAndRecoversFailure();
    recognitionTuningAndColumns();
    runtimeArmingAndAsyncShutdown();
    invalidArmKeepsRecognitionListening();
    unsupportedRenderingRemainsObservable();
    linguisticContextResetsAcrossArms();
    wxTheApp->OnExit();
    wxEntryCleanup();
}
