/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "timmy/core/recognition_benchmark.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace timmy;
using namespace std::chrono_literals;
namespace {
#define CHECK(...) do { if (!(__VA_ARGS__)) throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while (false)
using State = core::BenchmarkAttemptState;
using Stage = asr::RecognitionProgressState;
using Outcome = core::BenchmarkRecognitionOutcome;
using Clock = core::RecognitionBenchmarkSession::Clock;
class Backend final : public asr::IAsrBackend
{
public:
    asr::AsrCapabilities capabilities() const override { return {}; }
    asr::AsrState state() const override { return state_; }
    asr::AsrOperationResult start(const asr::AsrConfiguration&) override
    { ++starts; state_ = asr::AsrState::running; return {true, {}}; }
    asr::AsrOperationResult stop() override
    { ++stops; state_ = asr::AsrState::stopped; return {true, {}}; }
    asr::AsrOperationResult resetAttempt(asr::AttemptGeneration next) override
    { ++resets; generation = next; pendingSamples = 0; return {true, {}}; }
    asr::AttemptDiagnostics attemptDiagnostics() const override
    {
        asr::AttemptDiagnostics d;
        d.droppedAudioBlocks = 2; d.droppedVadFrames = 3; d.droppedInferenceJobs = 4;
        return d;
    }
    void result(std::string text, asr::AttemptGeneration tag)
    { publishResult({std::move(text), asr::AsrResultKind::final, {}, {}, tag}); }
    void progress(Stage stage, asr::AttemptGeneration tag, Clock::time_point at)
    { publishProgress({stage, tag, at, 2, 3, 4}); }
    int starts = 0, stops = 0, resets = 0;
    std::uint64_t pendingSamples = 0;
    asr::AttemptGeneration generation = 0;
private:
    void processAudioBlock(const audio::AudioBlockView& block) override
    { pendingSamples += block.frameCount; }
    asr::AsrState state_ = asr::AsrState::stopped;
};
class Observer final : public core::IRecognitionPipelineObserver
{
public:
    explicit Observer(core::RecognitionBenchmarkSession& session) : session_(session) {}
    void onRecognitionStateChanged(core::RecognitionPipelineState) override {}
    void onRecognitionDiagnostic(const asr::AsrDiagnostic&) override { ++errors; }
    void onRecognitionProgress(const asr::RecognitionProgress& progress) override
    { session_.progress(progress); }
    void onRecognitionTranscript(const core::RecognitionTranscript& transcript) override
    { ++transcripts; if (auto recorded = session_.record(transcript)) last = recorded; }
    std::optional<core::RecognitionBenchmarkAttempt> last;
    int transcripts = 0, errors = 0;
private:
    core::RecognitionBenchmarkSession& session_;
};
struct Fixture
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "timmy-attempt-lifecycle.jsonl";
    core::RecognitionBenchmarkSession session{path, {}};
    std::shared_ptr<Backend> backend = std::make_shared<Backend>();
    std::shared_ptr<audio::AudioChannelSelector> selector = std::make_shared<audio::AudioChannelSelector>(0);
    core::RecognitionPipeline pipeline{selector, backend, {}, true, true};
    std::shared_ptr<Observer> observer = std::make_shared<Observer>(session);
    Fixture() { pipeline.setObserver(observer); CHECK(pipeline.start({}).succeeded); }
    ~Fixture() { pipeline.stop(); session.close(); std::filesystem::remove(path); }
    void audio()
    {
        const float samples[]{0.5F, -0.5F, 0.5F, -0.5F};
        selector->onAudioBlock({samples, 4, {16000, 1}});
    }
    void prepare() { CHECK(session.prepare(pipeline).succeeded); CHECK(session.attemptState() == State::ready); }
    void arm(Clock::time_point now)
    {
        CHECK(session.startAttempt(now));
        CHECK(session.attemptState() == State::countdown);
        audio(); CHECK(backend->pendingSamples == 0);
        CHECK(!session.tick(pipeline, now + 999ms));
        CHECK(session.attemptState() == State::countdown);
        CHECK(!session.tick(pipeline, now + 1s));
        CHECK(session.attemptState() == State::waitingSpeech);
    }
};
void lifecycleAndIsolation()
{
    Fixture f;
    auto now = Clock::now();
    f.audio(); CHECK(f.backend->pendingSamples == 0);
    f.prepare();
    const auto first = f.session.generation();
    f.backend->result("Channel 1 At 50", first); CHECK(f.observer->transcripts == 0);
    CHECK(!f.session.recordNoResult());
    f.arm(now);
    f.audio(); CHECK(f.backend->pendingSamples == 4);
    f.backend->progress(Stage::speechStarted, first, now + 1250ms);
    CHECK(f.session.attemptState() == State::speechDetected);
    CHECK(!f.session.tick(f.pipeline, now + 1600ms));
    CHECK(f.session.attemptState() == State::waitingEnd);
    // This deadline is not an endpoint/inference timeout after speech starts.
    CHECK(!f.session.tick(f.pipeline, now + 60s));
    f.backend->progress(Stage::utteranceFinalized, first, now + 2s);
    CHECK(f.session.attemptState() == State::processing);
    f.backend->progress(Stage::inferenceStarted, first, now + 2100ms);
    f.backend->progress(Stage::inferenceCompletedWithText, first, now + 2600ms);
    f.backend->result("Channel 1 At 50", first);
    CHECK(f.session.attemptState() == State::result);
    CHECK(f.observer->last && f.observer->last->passed);
    const auto& d = f.observer->last->diagnostics;
    CHECK(d.audioSamples == 4 && std::abs(d.audioDurationSeconds - 4.0 / 16000) < 1e-9);
    CHECK(d.inputPeak == 0.5 && d.inputRms == 0.5 && d.speechDetected);
    CHECK(d.speechStartSeconds == 0.25 && d.utteranceDurationSeconds == 0.75);
    CHECK(d.inferenceDurationSeconds == 0.5);
    CHECK(d.droppedAudioBlocks == 2 && d.droppedVadFrames == 3 && d.droppedInferenceJobs == 4);
    CHECK(!f.session.recordNoResult());
    f.session.retry(); f.prepare();
    CHECK(f.backend->starts == 1 && f.backend->stops == 0 && f.backend->resets == 2);
    CHECK(f.pipeline.attemptDiagnostics().audioSamples == 0);
    CHECK(f.session.generation() != first);
    now += 100s;
    f.arm(now);
    f.backend->result("Channel 1 At 50", first);
    f.backend->progress(Stage::speechStarted, first, now + 1100ms);
    CHECK(f.session.attemptState() == State::waitingSpeech && f.observer->transcripts == 1);
    const auto timeout = f.session.tick(f.pipeline, now + 6s);
    CHECK(timeout && timeout->outcome == Outcome::noSpeech && !timeout->passed);
    CHECK(!timeout->diagnostics.speechDetected && timeout->diagnostics.audioSamples == 0);
    CHECK(timeout->diagnostics.droppedVadFrames == 3);
    f.session.retry(); f.prepare(); now += 100s; f.arm(now);
    f.audio(); // Audio arrived, but no speech-start event occurred.
    const auto silence = f.session.tick(f.pipeline, now + 6s);
    CHECK(silence && silence->outcome == Outcome::noSpeech);
    CHECK(silence->diagnostics.audioSamples == 4 && !silence->diagnostics.speechDetected);
    CHECK(f.session.next()); f.prepare();
    CHECK(f.backend->starts == 1 && f.backend->resets == 4);
    now += 100s; f.arm(now);
    f.audio();
    f.backend->progress(Stage::speechStarted, f.session.generation(), now + 1200ms);
    f.backend->progress(Stage::utteranceFinalized, f.session.generation(), now + 2s);
    f.backend->progress(Stage::inferenceStarted, f.session.generation(), now + 2s);
    f.backend->progress(Stage::inferenceCompletedNoText, f.session.generation(), now + 3s);
    f.backend->result("", f.session.generation());
    CHECK(f.observer->last->outcome == Outcome::emptyAsrResult && !f.observer->last->passed);
    CHECK(f.observer->last->diagnostics.speechDetected && f.observer->last->diagnostics.audioSamples == 4);
    // Exercise every remaining case without restarting the model/backend.
    for (std::size_t i = 2; i < core::liveBenchmarkCases().size(); ++i)
    {
        CHECK(f.session.next()); f.prepare(); now += 100s; f.arm(now);
        const auto manual = f.session.recordNoResult(f.pipeline.attemptDiagnostics());
        CHECK(manual && manual->outcome == Outcome::operatorNoResult);
        CHECK(manual->passed == !core::liveBenchmarkCases()[i].expected);
    }
    CHECK(!f.session.next() && f.backend->starts == 1 && f.backend->stops == 0);
    CHECK(f.pipeline.stop().succeeded && f.backend->stops == 1);
}
void normalLifecycle()
{
    auto selector = std::make_shared<audio::AudioChannelSelector>(0);
    auto backend = std::make_shared<Backend>();
    core::RecognitionPipeline pipeline(selector, backend);
    const float sample[]{0.5F};
    CHECK(pipeline.start({}).succeeded);
    selector->onAudioBlock({sample, 1, {16000, 1}});
    CHECK(backend->pendingSamples == 1 && backend->resets == 0);
    CHECK(pipeline.stop().succeeded);
    CHECK(pipeline.start({}).succeeded);
    CHECK(backend->starts == 2 && backend->stops == 1 && backend->resets == 0);
}
}
int main()
{
    try { lifecycleAndIsolation(); normalLifecycle(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
