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
class CaptureStats final : public audio::IAudioCaptureStatsSource
{
public:
    std::optional<audio::AudioCaptureStats> value = audio::AudioCaptureStats{7, 48000, 1000, 100, 10, 900, 48000};
    std::optional<audio::AudioCaptureStats> captureStats() const noexcept override { return value; }
};
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
        if (reportAudioDrops) d.droppedAudioBlocks = 2;
        d.droppedVadFrames = 3; d.droppedInferenceJobs = 4;
        d.vadQueueDepth = 2; d.vadQueueMaxDepth = 256;
        d.inferenceQueueDepth = 1; d.inferenceQueueMaxDepth = 2;
        d.oldestVadQueueAgeSeconds = 0.25;
        return d;
    }
    void result(std::string text, asr::AttemptGeneration tag, asr::RecognitionMetrics metrics = {})
    { publishResult({std::move(text), asr::AsrResultKind::final, {}, metrics, tag}); }
    void progress(Stage stage, asr::AttemptGeneration tag, Clock::time_point at)
    { publishProgress({stage, tag, at, 2, 3, 4}); }
    bool reportAudioDrops = true;
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
    std::shared_ptr<CaptureStats> capture = std::make_shared<CaptureStats>();
    core::RecognitionPipeline pipeline{selector, backend, {}, true, true, capture};
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
    CHECK(f.observer->last->diagnostics.commandProcessingDurationSeconds);
    CHECK(!f.observer->last->diagnostics.semanticParseDurationSeconds);
    CHECK(!f.observer->last->diagnostics.validationDurationSeconds);
    CHECK(!f.observer->last->diagnostics.renderingDurationSeconds);
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
void captureDiagnostics()
{
    Fixture f;
    auto now = Clock::now(); f.prepare(); f.arm(now);
    f.backend->reportAudioDrops = false;
    f.capture->value->capturedFrames += 48000;
    f.capture->value->droppedCaptureFrames += 1536;
    f.capture->value->ringOccupancyFrames = 512;
    auto d = f.pipeline.attemptDiagnostics(); // No downstream blocks required.
    CHECK(d.capturedFrames == 48000 && d.droppedCaptureFrames == 1536);
    CHECK(!d.droppedAudioBlocks);
    CHECK(d.vadQueueDepth == 2 && d.vadQueueMaxDepth == 256);
    CHECK(d.inferenceQueueDepth == 1 && d.inferenceQueueMaxDepth == 2);
    CHECK(d.oldestVadQueueAgeSeconds == 0.25);
    core::RecognitionTranscript loss; loss.diagnostics = d;
    const auto json = core::benchmarkJson({}, "capture-loss", core::liveBenchmarkCases()[0], 1, loss);
    CHECK(json.find("\"dropped_capture_frames\":1536") != std::string::npos);
    CHECK(json.find("\"dropped_audio_blocks\":null") != std::string::npos);
    CHECK(d.capturedDurationSeconds == 1 && d.captureRingOccupancyFrames == 512);
    CHECK(d.captureSessionRingHighWaterFrames == 900);
    CHECK(d.captureRingMaxOccupancyFrames == 512); // Never inherits the session's 900 peak.
    f.capture->value->ringOccupancyFrames = 128;
    CHECK(f.pipeline.attemptDiagnostics().captureRingMaxOccupancyFrames == 512);
    const auto recorded = f.session.recordNoResult(f.pipeline.attemptDiagnostics());
    CHECK(recorded && recorded->diagnostics.droppedCaptureFrames == 1536);
    f.pipeline.disarmAttempt();
    f.capture->value->droppedCaptureFrames += 100;
    CHECK(f.pipeline.attemptDiagnostics().droppedCaptureFrames == 1536);
    f.session.retry(); f.prepare(); f.arm(now + 100s);
    d = f.pipeline.attemptDiagnostics();
    CHECK(d.capturedFrames == 0 && d.droppedCaptureFrames == 0);
    CHECK(d.captureRingMaxOccupancyFrames == 128 && !d.commandProcessingDurationSeconds);
    f.capture->value->session++;
    CHECK(!f.pipeline.attemptDiagnostics().droppedCaptureFrames);
    f.capture->value.reset();
    CHECK(!f.pipeline.attemptDiagnostics().capturedFrames);
}
void commandDiagnostics()
{
    Fixture f;
    const auto now = Clock::now(); f.prepare(); f.arm(now);
    f.backend->progress(Stage::speechStarted, f.session.generation(), now + 1200ms);
    f.backend->progress(Stage::utteranceFinalized, f.session.generation(), now + 2s);
    // A later finalized utterance must not overwrite timing for the returned job.
    f.backend->progress(Stage::utteranceFinalized, f.session.generation(), now + 3s);
    f.backend->result("Channel 1 At 50", f.session.generation(), {2.5, 0.75, 1.1, now + 2s});
    CHECK(f.observer->last);
    const auto& d = f.observer->last->diagnostics;
    CHECK(d.utteranceFinalizedSeconds == 1);
    CHECK(d.inferenceAudioDurationSeconds == 2.5 && d.inferenceDurationSeconds == 0.75);
    CHECK(d.realTimeFactor == 0.3 && d.endOfUtteranceToResultSeconds == 1.1);
    CHECK(d.commandProcessingDurationSeconds && *d.commandProcessingDurationSeconds >= 0);
    CHECK(d.normalizationDurationSeconds && d.assemblyDurationSeconds && d.semanticParseDurationSeconds);
    CHECK(d.validationDurationSeconds && d.renderingDurationSeconds);
    CHECK(*d.commandProcessingDurationSeconds >= *d.normalizationDurationSeconds + *d.assemblyDurationSeconds
        + *d.semanticParseDurationSeconds + *d.validationDurationSeconds + *d.renderingDurationSeconds);
}
void normalLifecycle()
{
    auto selector = std::make_shared<audio::AudioChannelSelector>(0);
    auto backend = std::make_shared<Backend>();
    auto capture = std::make_shared<CaptureStats>(); capture->value.reset();
    core::RecognitionPipeline pipeline(selector, backend, {}, false, false, capture);
    const float sample[]{0.5F};
    CHECK(pipeline.start({}).succeeded);
    selector->onAudioBlock({sample, 1, {16000, 1}});
    CHECK(backend->pendingSamples == 1 && backend->resets == 0);
    capture->value = audio::AudioCaptureStats{8, 16000, 32000, 512, 0, 512, 16000};
    const auto d = pipeline.attemptDiagnostics();
    CHECK(d.capturedFrames == 32000 && d.droppedCaptureFrames == 512);
    CHECK(!d.commandProcessingDurationSeconds);
    CHECK(pipeline.stop().succeeded);
    CHECK(pipeline.start({}).succeeded);
    CHECK(backend->starts == 2 && backend->stops == 1 && backend->resets == 0);
}
}
int main()
{
    try { lifecycleAndIsolation(); captureDiagnostics(); commandDiagnostics(); normalLifecycle(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
