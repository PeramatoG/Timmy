/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "timmy/audio/float_wav.h"
#include "timmy/core/inference_audio.h"
#include "timmy/core/recognition_benchmark.h"
#include "timmy/core/recognition_replay.h"

#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <stdexcept>
#include <thread>

using namespace timmy;
namespace {
    void require(bool value, const std::source_location where = std::source_location::current())
    {
        if (!value) throw std::runtime_error("Inference audio check failed at line " + std::to_string(where.line()));
    }
    template<class Action> void rejects(Action action)
    {
        bool threw = false;
        try { action(); } catch (const std::exception&) { threw = true; }
        require(threw);
    }
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("timmy-inference-" + std::to_string(std::random_device{}()));
        Directory() { require(std::filesystem::create_directory(path)); }
        ~Directory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    };
    const std::vector<float> pcm{0.0F, -0.0F, 0.125F, -0.5F, 0.998F, 0.999F,
        1.1F, -1.2F, std::numeric_limits<float>::denorm_min(), 0.0F, 0.0F};
    bool same(std::span<const float> left, std::span<const float> right)
    { return left.size() == right.size() && std::memcmp(left.data(), right.data(), left.size_bytes()) == 0; }
    class Backend final : public asr::IAsrBackend
    {
    public:
        ~Backend() override { stop(); }
        asr::AsrCapabilities capabilities() const override { return {}; }
        asr::AsrState state() const override { return state_; }
        asr::AsrOperationResult start(const asr::AsrConfiguration&) override
        { state_ = asr::AsrState::running; return {true, {}}; }
        asr::AsrOperationResult stop() override
        { if (worker.joinable()) worker.join(); state_ = asr::AsrState::stopped; return {true, {}}; }
        asr::AsrOperationResult resetAttempt(asr::AttemptGeneration next) override
        { if (worker.joinable()) worker.join(); generation = next; index = 0; return {true, {}}; }
        bool observe(std::span<const float> samples, std::optional<asr::AttemptGeneration> gen = {})
        { return publishInferenceInput({gen.value_or(generation), ++index, samples}); }
        void finish()
        { publishResult({text, asr::AsrResultKind::final, "es", {0.0006875, 0.01, {}, {}}, generation}); }
        asr::AsrOperationResult submitInferenceInput(std::span<const float> samples) override
        {
            consumed.assign(samples.begin(), samples.end());
            worker = std::thread([this] {
                if (timeout) return;
                if (fail) { publishDiagnostic({asr::AsrDiagnosticCode::recognitionFailure, "synthetic inference failure"}); return; }
                if (observe(consumed)) finish();
            });
            return {true, {}};
        }
        std::vector<float> consumed;
        std::string text = "canal 25, 50.";
        bool fail = false, timeout = false;
    private:
        void processAudioBlock(const audio::AudioBlockView&) override { require(false); }
        std::thread worker;
        asr::AsrState state_ = asr::AsrState::stopped;
        asr::AttemptGeneration generation = 0;
        std::uint64_t index = 0;
    };
    class Observer final : public core::IRecognitionPipelineObserver
    {
    public:
        void onRecognitionStateChanged(core::RecognitionPipelineState) override {}
        void onRecognitionDiagnostic(const asr::AsrDiagnostic&) override {}
        void onRecognitionTranscript(const core::RecognitionTranscript& value) override { last = value; }
        core::RecognitionTranscript last;
    };
    struct Fixture
    {
        std::shared_ptr<Backend> backend = std::make_shared<Backend>();
        std::shared_ptr<Observer> observer = std::make_shared<Observer>();
        core::RecognitionPipeline pipeline{std::make_shared<audio::AudioChannelSelector>(0), backend, {}, true, true};
        Fixture() { pipeline.setObserver(observer); require(pipeline.start({}).succeeded); }
        void arm(core::RecognitionBenchmarkSession& session)
        {
            require(session.prepare(pipeline).succeeded);
            const auto now = core::RecognitionBenchmarkSession::Clock::now();
            require(session.startAttempt(now));
            require(!session.tick(pipeline, now + std::chrono::seconds(1)));
        }
    };
    void roundTrip()
    {
        Directory directory;
        const auto path = directory.path / "exact.wav";
        audio::writeFloatWav(path, pcm);
        const auto wav = audio::readFloatWav(path);
        require(wav.sampleRate == 16000 && same(wav.samples, pcm));
        rejects([&] { audio::writeFloatWav(path, std::vector<float>{1}); });
        require(same(audio::readFloatWav(path).samples, pcm));
        rejects([&] { audio::writeFloatWav(directory.path / "empty.wav", {}); });
        rejects([&] { audio::writeFloatWav(directory.path / "nan.wav", std::vector<float>{std::numeric_limits<float>::quiet_NaN()}); });
        std::ifstream input(path, std::ios::binary);
        std::vector<char> bytes{std::istreambuf_iterator<char>{input}, {}};
        auto mutated = directory.path / "invalid.wav";
        const auto writeBytes = [&] { std::ofstream output(mutated, std::ios::binary); output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); };
        bytes[20] = 1; writeBytes(); // PCM tag, not IEEE float.
        rejects([&] { (void)audio::readFloatWav(mutated); });
        bytes[20] = 3; bytes[22] = 2; writeBytes();
        rejects([&] { (void)audio::readFloatWav(mutated); });
        bytes[22] = 1; bytes.pop_back(); writeBytes();
        rejects([&] { (void)audio::readFloatWav(mutated); });
        input.clear(); input.seekg(0);
        bytes.assign(std::istreambuf_iterator<char>{input}, {});
        bytes[56] = 0; bytes[57] = 0; bytes[58] = static_cast<char>(0xc0); bytes[59] = 0x7f;
        writeBytes(); // Nonfinite float in otherwise well-formed WAV.
        rejects([&] { (void)audio::readFloatWav(mutated); });
        // Unknown, odd-length RIFF chunk is skipped without touching data.
        input.clear(); input.seekg(0);
        bytes.assign(std::istreambuf_iterator<char>{input}, {});
        const char junk[]{'J', 'U', 'N', 'K', 1, 0, 0, 0, 42, 0};
        bytes.insert(bytes.begin() + 12, std::begin(junk), std::end(junk));
        const auto riffSize = static_cast<std::uint32_t>(bytes.size() - 8);
        for (int i = 0; i < 4; ++i) bytes[4 + i] = static_cast<char>(riffSize >> (8 * i));
        writeBytes();
        require(same(audio::readFloatWav(mutated).samples, pcm));
    }
    void diagnosticsAndNames()
    {
        Directory directory;
        core::InferenceAudioCapture capture{directory.path / "run.jsonl", "session:1/..", "case/25", 2, 42};
        const auto path = core::inferenceAudioPath(capture, 1);
        require(!path.is_absolute() && path.begin()->string() == "run_audio");
        const auto name = path.filename().string();
        require(name.find_first_of(":/\\ ") == std::string::npos);
        require(path == core::inferenceAudioPath(capture, 1));
        require(path != core::inferenceAudioPath(capture, 2));
        auto next = capture; ++next.attempt; require(path != core::inferenceAudioPath(next, 1));
        next = capture; ++next.generation; require(path != core::inferenceAudioPath(next, 1));
        next = capture; next.caseId = "case_2f25"; require(path != core::inferenceAudioPath(next, 1));
        const auto audio = core::inspectInferenceAudio({42, 1, pcm}, capture);
        require(audio.wavPath && !audio.wavError && audio.sampleCount == pcm.size());
        require(audio.sampleRate == 16000 && audio.sampleFormat == "ieee_float32");
        require(audio.peakLinear > 1.19 && audio.nearFullScaleSamples == 3);
        require(audio.nearFullScaleRatio == 3.0 / pcm.size() && audio.peakDbfs && audio.rmsDbfs);
        require(!audio.estimatedSnrDb && !audio.speechRmsDbfs && !audio.noiseRmsDbfs);
        require(same(audio::readFloatWav(directory.path / *audio.wavPath).samples, pcm));
        const auto duplicate = core::inspectInferenceAudio({42, 1, pcm}, capture);
        require(!duplicate.wavPath && duplicate.wavError); // Never overwrite.
        const auto noSave = core::inspectInferenceAudio({42, 2, pcm});
        require(!noSave.wavPath && !noSave.wavError);
        const auto silent = core::inspectInferenceAudio({42, 3, std::vector<float>(16, 0)});
        require(silent.peakLinear == 0 && silent.rmsLinear == 0 && !silent.peakDbfs && !silent.rmsDbfs);
    }
    std::vector<std::string> captureLifecycle()
    {
        Directory directory;
        std::vector<std::string> json;
        {
            Fixture fixture;
            core::RecognitionBenchmarkSnapshot snapshot;
            require(!snapshot.saveAsrInputWavs);
            core::RecognitionBenchmarkSession session(directory.path / "disabled.jsonl", snapshot);
            fixture.arm(session);
            fixture.backend->observe(pcm); fixture.backend->finish();
            require(fixture.observer->last.diagnostics.inferenceInputs.size() == 1);
            require(!fixture.observer->last.diagnostics.inferenceInputs.front().wavPath);
            require(!std::filesystem::exists(directory.path / "disabled_audio"));
        }
        {
            Fixture fixture;
            core::RecognitionBenchmarkSnapshot snapshot;
            snapshot.saveAsrInputWavs = true;
            core::RecognitionBenchmarkSession session(directory.path / "enabled.jsonl", snapshot);
            fixture.arm(session);
            require(!std::filesystem::exists(directory.path / "enabled_audio"));
            fixture.backend->observe(pcm, session.generation() + 100); // Stale job cannot be saved.
            require(fixture.pipeline.attemptDiagnostics().inferenceInputs.empty());
            fixture.backend->observe(pcm); fixture.backend->observe(pcm);
            fixture.backend->finish();
            const auto& transcript = fixture.observer->last;
            const auto& inputs = transcript.diagnostics.inferenceInputs;
            require(inputs.size() == 2 && inputs[0].wavPath && inputs[1].wavPath);
            require(transcript.diagnostics.audioSamples == 0 && transcript.diagnostics.inputPeak == 0
                && inputs[0].peakLinear > 1); // Armed input and inference PCM stay distinct.
            require(inputs[0].wavPath != inputs[1].wavPath);
            for (const auto& audio : inputs)
                require(same(audio::readFloatWav(directory.path / *audio.wavPath).samples, pcm));
            json.push_back(core::benchmarkJson(snapshot, session.id(), core::liveBenchmarkCases()[3], 1, transcript));
            require(session.record(transcript).has_value());
            require(!fixture.backend->observe(pcm)); // Closed attempt cannot start another inference/file.
            require(fixture.pipeline.attemptDiagnostics().inferenceInputs.size() == 2);
            const auto first = *inputs[0].wavPath;
            session.retry(); fixture.arm(session);
            fixture.backend->observe(pcm); fixture.backend->finish();
            require(fixture.observer->last.diagnostics.inferenceInputs.size() == 1);
            require(*fixture.observer->last.diagnostics.inferenceInputs[0].wavPath != first);
            require(session.record(fixture.observer->last).has_value());
            // No inference: no files/records, even with capture enabled.
            session.retry(); fixture.arm(session);
            require(fixture.pipeline.attemptDiagnostics().inferenceInputs.empty());
            const auto noResult = session.recordNoResult(fixture.pipeline);
            require(noResult && noResult->diagnostics.inferenceInputs.empty());
            json.push_back(core::benchmarkJson(snapshot, session.id(), core::liveBenchmarkCases()[0], 3,
                fixture.observer->last, core::BenchmarkRecognitionOutcome::emptyAsrResult));
            session.retry(); fixture.arm(session);
            const auto timedOut = session.tick(fixture.pipeline, core::RecognitionBenchmarkSession::Clock::now() + std::chrono::seconds(10));
            require(timedOut && timedOut->outcome == core::BenchmarkRecognitionOutcome::noSpeech
                && timedOut->diagnostics.inferenceInputs.empty());
            json.push_back(core::benchmarkJson(snapshot, session.id(), core::liveBenchmarkCases()[0], 4, {}, core::BenchmarkRecognitionOutcome::noSpeech));
            // Shutdown joins a real worker while its WAV save may still be active.
            session.retry(); fixture.arm(session);
            require(fixture.pipeline.submitInferenceInput(pcm).succeeded);
            fixture.pipeline.stop(); session.close();
        }
        {
            Fixture fixture;
            core::RecognitionBenchmarkSnapshot snapshot; snapshot.saveAsrInputWavs = true;
            std::ofstream(directory.path / "blocked_audio") << "not a directory";
            core::RecognitionBenchmarkSession session(directory.path / "blocked.jsonl", snapshot);
            fixture.arm(session);
            fixture.backend->observe(pcm); fixture.backend->finish();
            const auto& transcript = fixture.observer->last;
            require(transcript.validation.accepted && transcript.rendering.text == "Channel 25 At 50");
            require(transcript.diagnostics.inferenceInputs[0].wavError && !transcript.diagnostics.inferenceInputs[0].wavPath);
            require(session.recordNoResult(fixture.pipeline).has_value());
            json.push_back(core::benchmarkJson(snapshot, session.id(), core::liveBenchmarkCases()[3], 1,
                transcript, core::BenchmarkRecognitionOutcome::operatorNoResult));
        }
        return json;
    }
    void replay()
    {
        Directory directory;
        const auto path = directory.path / "replay.wav";
        audio::writeFloatWav(path, pcm);
        auto backend = std::make_shared<Backend>();
        const auto result = core::replayInferenceWav(backend, path, {});
        require(result.operation.succeeded && result.transcript);
        require(same(backend->consumed, pcm));
        require(backend->state() == asr::AsrState::stopped);
        require(result.transcript->rendering.text == "Channel 25 At 50");
        require(result.transcript->diagnostics.inferenceInputs[0].sampleCount == pcm.size());
        backend = std::make_shared<Backend>(); backend->text.clear();
        const auto empty = core::replayInferenceWav(backend, path, {});
        require(empty.operation.succeeded && empty.transcript && empty.transcript->rawTranscript.empty());
        backend = std::make_shared<Backend>(); backend->fail = true;
        require(!core::replayInferenceWav(backend, path, {}).operation.succeeded);
        backend = std::make_shared<Backend>(); backend->timeout = true;
        require(!core::replayInferenceWav(backend, path, {}, {}, std::chrono::milliseconds(1)).operation.succeeded);
        require(backend->state() == asr::AsrState::stopped);
        auto wrongRate = directory.path / "wrong-rate.wav";
        audio::writeFloatWav(wrongRate, pcm, 48000);
        require(!core::replayInferenceWav(backend, wrongRate, {}).operation.succeeded);
        require(!core::replayInferenceWav(backend, directory.path / "missing.wav", {}).operation.succeeded);
    }
}
int main(int argc, char** argv)
{
    try {
        roundTrip(); diagnosticsAndNames(); replay();
        const auto rows = captureLifecycle();
        if (argc == 2 && std::string_view(argv[1]) == "--json-fixtures") {
            for (const auto& row : rows) std::cout << row << '\n';
        } else std::cout << "Inference WAV capture/replay tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
