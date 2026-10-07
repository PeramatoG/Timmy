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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
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
        asr::AsrOperationResult start(const asr::AsrConfiguration&) override
        {
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
    runtimeArmingAndAsyncShutdown();
    invalidArmKeepsRecognitionListening();
    wxTheApp->OnExit();
    wxEntryCleanup();
}
