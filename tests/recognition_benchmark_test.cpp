/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "timmy/core/recognition_benchmark.h"
#include "timmy/core/asr_engine_factory.h"
#include "timmy/core/command_recognition_profile.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace timmy;
namespace {
    void require(bool condition, const char* expression, int line)
    {
        if (condition) return;
        throw std::runtime_error(std::string("Line ") + std::to_string(line) + ": " + expression);
    }
#define REQUIRE(...) require((__VA_ARGS__), #__VA_ARGS__, __LINE__)
    class FakeBackend final : public asr::IAsrBackend
    {
    public:
        asr::AsrCapabilities capabilities() const override { return {}; }
        asr::AsrState state() const override { return state_; }
        asr::AsrOperationResult start(const asr::AsrConfiguration&) override
        { state_ = asr::AsrState::running; return {true, {}}; }
        asr::AsrOperationResult stop() override
        { state_ = asr::AsrState::stopped; return {true, {}}; }
        asr::AsrOperationResult resetAttempt(asr::AttemptGeneration next) override
        { generation = next; return {true, {}}; }
        asr::AttemptDiagnostics attemptDiagnostics() const override
        { asr::AttemptDiagnostics result; result.recognitionGuidance = guidance; return result; }
        asr::SettingValues guidance;
        void emit(asr::AsrResult result) { result.generation = generation; publishResult(result); }
        asr::AttemptGeneration generation = 0;
    private:
        void processAudioBlock(const audio::AudioBlockView&) override {}
        asr::AsrState state_{asr::AsrState::stopped};
    };
    class Observer final : public core::IRecognitionPipelineObserver
    {
    public:
        void onRecognitionStateChanged(core::RecognitionPipelineState) override {}
        void onRecognitionDiagnostic(const asr::AsrDiagnostic&) override {}
        void onRecognitionTranscript(const core::RecognitionTranscript& value) override
        { last = value; ++calls; }
        core::RecognitionTranscript last;
        unsigned calls{0};
    };
    struct Fixture
    {
        explicit Fixture(bool isolated = true)
            : pipeline(selector, backend, {}, isolated)
        {
            pipeline.setObserver(observer);
            REQUIRE(pipeline.start({}).succeeded);
        }
        core::RecognitionTranscript recognize(const std::string& phrase)
        { backend->emit({phrase, asr::AsrResultKind::final, {}}); return observer->last; }
        std::shared_ptr<FakeBackend> backend = std::make_shared<FakeBackend>();
        std::shared_ptr<audio::AudioChannelSelector> selector = std::make_shared<audio::AudioChannelSelector>(0);
        std::shared_ptr<Observer> observer = std::make_shared<Observer>();
        core::RecognitionPipeline pipeline;
    };
    void arm(core::RecognitionBenchmarkSession& session, Fixture& fixture)
    {
        REQUIRE(session.prepare(fixture.pipeline).succeeded);
        const auto now = core::RecognitionBenchmarkSession::Clock::now();
        REQUIRE(session.startAttempt(now));
        REQUIRE(!session.tick(fixture.pipeline, now + std::chrono::seconds(1)));
    }
    core::RecognitionTranscript forSession(core::RecognitionBenchmarkSession& session,
        core::RecognitionTranscript transcript)
    { transcript.generation = session.generation(); return transcript; }
    core::RecognitionBenchmarkSnapshot snapshot()
    {
        core::RecognitionBenchmarkSnapshot value;
        value.engineId = "fake\"\\engine";
        value.configuration.modelResourcePath = std::filesystem::path(u8"models/ni\u00f1o.bin");
        // Exercise native Windows separators as well as UTF-8 filenames.
        value.configuration.modelResourcePath.make_preferred();
        value.configuration.preferredLanguage = "es";
        value.vadModelPath = "models/vad.bin";
        value.vadModelPath.make_preferred();
        value.settings = asr::defaultSettings(core::asrSettingDescriptors(core::AsrEngineId::whisperCpp));
        value.configuration.recognitionHints = core::commandRecognitionHints();
        value.audioSource = {"Device \"one\"\\input\n", 2, "Input \"three\"\t"};
        return value;
    }
    void guidanceObservability()
    {
        Fixture fixture;
        auto transcript = fixture.recognize("Channel 1 At 50");
        REQUIRE(core::benchmarkJson(snapshot(), "unknown", core::liveBenchmarkCases().front(), 1,
            transcript).find("\"active_guidance\":null") != std::string::npos);
        fixture.backend->guidance = {{"whisper.grammar_enabled", false}, {"whisper.compact_prompt", true},
            {"whisper.grammar_passed", false}, {"whisper.initial_prompt_passed", true}};
        transcript = fixture.recognize("Channel 1 At 50");
        REQUIRE(transcript.diagnostics.recognitionGuidance == fixture.backend->guidance);
        REQUIRE(fixture.pipeline.attemptDiagnostics().recognitionGuidance == fixture.backend->guidance);
        const auto json = core::benchmarkJson(snapshot(), "reported", core::liveBenchmarkCases().front(), 1, transcript);
        REQUIRE(json.find("\"active_guidance\":{\"whisper.compact_prompt\":true,\"whisper.grammar_enabled\":false,"
            "\"whisper.grammar_passed\":false,\"whisper.initial_prompt_passed\":true}") != std::string::npos);
    }
    void unicodePathsAndJson()
    {
        Fixture fixture;
        auto value = snapshot();
        // Universal escapes fix the intended Unicode code points independently
        // of MSVC's source code page; narrow escapes check the actual UTF-8 bytes.
        REQUIRE(value.configuration.modelResourcePath.filename().u8string() == u8"ni\u00f1o.bin");
        value.vadModelPath = std::filesystem::path(u8"vad/canci\u00f3n-\U0001f4a1.bin");
        value.vadModelPath.make_preferred();
        REQUIRE(value.vadModelPath.filename().u8string() == u8"canci\u00f3n-\U0001f4a1.bin");
        const auto json = core::benchmarkJson(value, "unicode-paths", core::liveBenchmarkCases()[0],
            1, fixture.recognize("Channel 1 At 50"));
        REQUIRE(json.find("\"model_filename\":\"ni\xc3\xb1o.bin\"") != std::string::npos);
        REQUIRE(json.find("\"vad_model_filename\":\"canci\xc3\xb3n-\xf0\x9f\x92\xa1.bin\"") != std::string::npos);
        const std::string jsonSeparator(
            std::filesystem::path::preferred_separator == '/' ? 1 : 2,
            static_cast<char>(std::filesystem::path::preferred_separator));
        REQUIRE(json.find("\"model_path\":\"models" + jsonSeparator + "ni\xc3\xb1o.bin\"") != std::string::npos);
        REQUIRE(json.find("\"vad_model_path\":\"vad" + jsonSeparator + "canci\xc3\xb3n-\xf0\x9f\x92\xa1.bin\"") != std::string::npos);
    }
    void corpusAndEvaluation()
    {
        Fixture fixture;
        const auto& corpus = core::liveBenchmarkCases();
        REQUIRE(corpus.size() == 36);
        const std::vector<std::string> ids{
            "es-num-1","es-num-5","es-num-15","es-num-25","es-num-35","es-num-105","es-num-115","es-num-150","es-num-515","es-num-550",
            "level-zero","level-full","fixture-full","group-half","range-level","range-on","list-select",
            "exclude-select","exclude-level","select-single","test-single","home-fixture","editor-clear",
            "release-single","en-num-13","en-num-30","en-num-15","en-num-50","en-num-17","en-num-70",
            "en-select","en-home","reject-greeting","reject-weather",
            "reject-thanks","reject-english"};
        const std::vector<unsigned> spanishNumbers{1,5,15,25,35,105,115,150,515,550};
        for (std::size_t i = 0; i < spanishNumbers.size(); ++i)
        {
            const auto& test = corpus[i];
            REQUIRE(test.category == "numeric-robustness");
            const auto& intent = std::get<command::SetIntensity>(*test.expected);
            REQUIRE(intent.target.included.front().first == spanishNumbers[i]);
            REQUIRE(intent.level.percent() == 50);
            REQUIRE(intent.target.type == (i < 8 ? command::TargetType::channel : command::TargetType::fixture));
        }
        const std::vector<unsigned> englishNumbers{13,30,15,50,17,70};
        for (std::size_t i = 0; i < englishNumbers.size(); ++i)
        {
            const auto& test = corpus[24 + i];
            REQUIRE(test.category == "english-numeric-confusion");
            REQUIRE(test.phrase == "Channel " + std::to_string(englishNumbers[i]) + " at 50");
            const auto& intent = std::get<command::SetIntensity>(*test.expected);
            REQUIRE(intent.target.included.front().first == englishNumbers[i]);
            REQUIRE(intent.target.type == command::TargetType::channel && intent.level.percent() == 50);
        }
        std::set<std::string> unique;
        for (std::size_t i = 0; i < corpus.size(); ++i)
        {
            const auto& test = corpus[i];
            REQUIRE(test.id == ids[i]);
            REQUIRE(unique.insert(test.id).second);
            REQUIRE(!test.category.empty() && !test.phrase.empty());
            const auto result = fixture.recognize(test.phrase);
            if (!core::benchmarkPasses(test, result))
                std::cerr << test.id << " / " << test.phrase << " / " << result.normalizedTranscript
                    << " / " << result.assemblyText << " / "
                    << (result.semanticCommand ? command::describe(*result.semanticCommand) : "none") << '\n';
            REQUIRE(core::benchmarkPasses(test, result));
            if (test.expected)
            {
                REQUIRE(command::validateIntent(*test.expected).accepted);
                REQUIRE(command::renderIntent(*test.expected).text.has_value());
                auto rejected = result;
                rejected.validation.accepted = false;
                REQUIRE(!core::benchmarkPasses(test, rejected));
                rejected = result;
                rejected.rendering.text.reset();
                REQUIRE(!core::benchmarkPasses(test, rejected));
            }
            else REQUIRE(!result.semanticCommand || !result.validation.accepted || !result.rendering.text);
        }
        // Keep live corpus expectations unchanged; simulate connector-loss ASR
        // transcripts against those same semantic results.
        for (const auto& phrase : {"canales 1 a 10 excepto 5 a 50",
                "canales 1 a 10 excepto 5 50", "channels 1 through 10 except 5 50"}) {
            const auto result = fixture.recognize(phrase);
            REQUIRE(core::benchmarkPasses(corpus[18], result));
            REQUIRE(!result.numericInferenceReasons.empty());
            const auto json = core::benchmarkJson(snapshot(), "numeric-regression", corpus[18], 1, result);
            REQUIRE(json.find("exclusion_level_disambiguation") != std::string::npos);
            REQUIRE(json.find("\"default_target\":\"channel\"") != std::string::npos);
        }
        REQUIRE(core::benchmarkPasses(corpus[0], fixture.recognize("1 50")));
        REQUIRE(core::benchmarkPasses(corpus[14], fixture.recognize("canales 2 a 40 50")));
        REQUIRE(core::benchmarkPasses(corpus[15], fixture.recognize("enciende 2 a 40")));
        REQUIRE(core::benchmarkPasses(corpus[16], fixture.recognize("selecciona fixture 1 3 5")));
        core::RecognitionBenchmarkCase onList{"on-list", "numeric-regression", "",
            command::SetIntensity{command::TargetSelection{command::TargetType::channel, {{10,10}, {20,20}}},
                *command::IntensityLevel::fromPercent(100)}};
        REQUIRE(core::benchmarkPasses(onList, fixture.recognize("enciende 10 20")));
        REQUIRE(fixture.recognize("channel 1 at 50").numericInferenceReasons.empty());
        REQUIRE(core::benchmarkPasses(corpus[0], fixture.recognize("Channel 1 At 50")));
        REQUIRE(!core::benchmarkPasses(corpus[0], fixture.recognize("Channel 5 At 50")));
        REQUIRE(!core::benchmarkPasses(corpus[0], fixture.recognize("Channel 1 At Full")));
        REQUIRE(!core::benchmarkPasses(corpus[0], fixture.recognize("Fixture 1 At 50")));
        REQUIRE(!core::benchmarkPasses(corpus.back(), fixture.recognize("Channel 1 At 50")));
        // Equivalent semantic selections with a different raw wording/structure.
        core::RecognitionBenchmarkCase equivalent{"eq", "list", "", command::SelectSelection{
            command::TargetSelection{command::TargetType::channel, {{1,3}}, {{2,2}}}}};
        REQUIRE(core::benchmarkPasses(equivalent, fixture.recognize("Select channels 3 and 1")));
        REQUIRE(!core::benchmarkPasses(equivalent, fixture.recognize("Select channels 1 through 3")));

        // Isolated benchmark utterances never inherit linguistic target/assembly.
        fixture.recognize("Select group 3");
        REQUIRE(!fixture.recognize("add 4").semanticCommand);
        fixture.recognize("Channel 5");
        REQUIRE(!fixture.recognize("At 50").semanticCommand);
        Fixture normal(false);
        normal.recognize("Select group 3");
        REQUIRE(normal.recognize("add 4").semanticCommand.has_value());
        normal.pipeline.discardPendingAssembly();
        normal.recognize("Channel 5");
        REQUIRE(normal.recognize("At 50").semanticCommand.has_value());
        normal.recognize("fixtures 1,");
        const auto continued = normal.recognize("3, 5 at 50");
        REQUIRE(continued.semanticCommand && continued.validation.accepted);
        REQUIRE(command::describe(*continued.semanticCommand) == "SetIntensity(Fixture(1, 3, 5), 50%)");
    }
    void metrics()
    {
        Fixture fixture;
        fixture.backend->emit({"Channel 1 At 50", asr::AsrResultKind::final, "es", {2.5, 0.75}});
        REQUIRE(fixture.observer->last.detectedLanguage == "es");
        REQUIRE(fixture.observer->last.metrics.audioDurationSeconds == 2.5);
        REQUIRE(fixture.observer->last.metrics.inferenceDurationSeconds == 0.75);
        REQUIRE(fixture.observer->last.diagnostics.realTimeFactor == 0.3);
        REQUIRE(!fixture.observer->last.diagnostics.commandProcessingDurationSeconds);
        fixture.backend->emit({"Channel 1 At 50", asr::AsrResultKind::final, {}, {0, 0.75}});
        REQUIRE(!fixture.observer->last.diagnostics.realTimeFactor);
        const auto count = fixture.observer->calls;
        fixture.backend->emit({"Channel 1 At 50", asr::AsrResultKind::partial, "en", {}});
        REQUIRE(fixture.observer->calls == count);
        fixture.recognize("Channel 1 At 50");
        REQUIRE(!fixture.observer->last.metrics.audioDurationSeconds);
        REQUIRE(!fixture.observer->last.metrics.inferenceDurationSeconds);
        REQUIRE(!fixture.observer->last.detectedLanguage);
    }
    void sessionAndJson()
    {
        Fixture fixture;
        const auto path = std::filesystem::temp_directory_path() / "timmy-benchmark-unit.jsonl";
        std::filesystem::remove(path);
        auto original = snapshot();
        core::RecognitionBenchmarkSession session(path, original);
        arm(session, fixture);
        original.settings["whisper.threads"] = 7;
        original.audioSource = {"changed", 9, std::nullopt};
        REQUIRE(session.snapshot().audioSource.channelIndex == 2);
        REQUIRE(session.snapshot().audioSource.channelDisplayName.has_value());
        REQUIRE(session.snapshot().audioSource.deviceDisplayName != "changed");
        REQUIRE(session.snapshot().settings.at("whisper.threads") != original.settings.at("whisper.threads"));
        REQUIRE(!session.next());
        session.retry(); // Cannot manufacture attempts before a recognition result.
        REQUIRE(session.record(fixture.recognize("Channel 1 At 50"))->attempt == 1);
        REQUIRE(!session.awaitingResult() && session.index() == 0);
        REQUIRE(!session.record(fixture.recognize("Channel 5 At 50")));
        session.retry();
        arm(session, fixture);        const auto retry = session.record(fixture.recognize("Channel 5 At 50"));
        REQUIRE(retry && retry->attempt == 2 && !retry->passed && retry->caseId == "es-num-1");
        REQUIRE(session.next());
        arm(session, fixture);
        const auto next = session.record(fixture.recognize("Channel 5 At 50"));
        REQUIRE(next && next->attempt == 1 && next->passed && next->caseId == "es-num-5");
        // All writes are visible even before close, without requiring GUI events.
        std::ifstream in(path);
        std::string line;
        unsigned lines = 0;
        while (std::getline(in, line))
        {
            REQUIRE(line.front() == '{' && line.back() == '}');
            REQUIRE(line.find("\"recognition_settings\":{") != std::string::npos);
            REQUIRE(line.find("\"recognition_outcome\":\"final_result\"") != std::string::npos);
            REQUIRE(line.find("\"channel_index\":2") != std::string::npos);
            REQUIRE(line.find("\"schema_version\":2") != std::string::npos);
            REQUIRE(line.find("\"audio_duration_seconds\":null") != std::string::npos);
            ++lines;
        }
        REQUIRE(lines == 3);
        for (std::size_t i = 2; i < core::liveBenchmarkCases().size(); ++i)
        {
            REQUIRE(session.next());
        arm(session, fixture);
            REQUIRE(session.record(fixture.recognize(core::liveBenchmarkCases()[i].phrase)).has_value());
        }
        REQUIRE(!session.next());
        session.retry();
        arm(session, fixture);        REQUIRE(session.record(fixture.recognize(core::liveBenchmarkCases().back().phrase))->attempt == 2);
        session.close();
        session.close();
        REQUIRE(!session.record({}));
        REQUIRE(!session.next());
        in.close();
        std::filesystem::remove(path);
        bool failed = false;
        try { core::RecognitionBenchmarkSession bad(path / "missing.jsonl", snapshot()); }
        catch (const std::exception&) { failed = true; }
        REQUIRE(failed);
    }
    void noResultEvaluationAndSession()
    {
        using Outcome = core::BenchmarkRecognitionOutcome;
        Fixture fixture;
        const auto& corpus = core::liveBenchmarkCases();
        const auto stale = fixture.recognize("Channel 1 At 50");
        REQUIRE(!core::benchmarkPasses(corpus.front(), {}, Outcome::operatorNoResult));
        REQUIRE(core::benchmarkPasses(corpus.back(), {}, Outcome::operatorNoResult));
        REQUIRE(core::benchmarkPasses(corpus.back(), stale, Outcome::operatorNoResult));
        const auto path = std::filesystem::temp_directory_path() / "timmy-benchmark-no-result.jsonl";
        std::filesystem::remove(path);
        core::RecognitionBenchmarkSession session(path, snapshot());
        arm(session, fixture);
        const auto first = session.recordNoResult();
        REQUIRE(first && !first->passed && first->outcome == Outcome::operatorNoResult && first->attempt == 1);
        REQUIRE(!session.awaitingResult());
        REQUIRE(!session.recordNoResult() && !session.record(forSession(session, stale)));
        session.retry();
        arm(session, fixture);        const auto retried = session.record(forSession(session, stale));
        REQUIRE(retried && retried->passed && retried->attempt == 2 && retried->outcome == Outcome::finalResult);
        // Navigate through positive and negative cases without any backend results.
        for (std::size_t i = 1; i < corpus.size(); ++i)
        {
            REQUIRE(session.next());
        arm(session, fixture);
            const auto attempt = session.recordNoResult();
            REQUIRE(attempt && attempt->passed == !corpus[i].expected);
            REQUIRE(attempt->attempt == 1 && attempt->outcome == Outcome::operatorNoResult);
        }
        REQUIRE(!session.next());
        session.retry();
        arm(session, fixture);        REQUIRE(session.recordNoResult()->attempt == 2);
        session.close();
        REQUIRE(!session.recordNoResult());
        std::ifstream input(path);
        std::string line;
        unsigned count = 0;
        while (std::getline(input, line))
        {
            if (count != 1)
            {
                REQUIRE(line.find("\"recognition_outcome\":\"operator_no_result\"") != std::string::npos);
                REQUIRE(line.find("\"raw_transcript\":null") != std::string::npos);
                REQUIRE(line.find("\"assembly_state\":\"not_attempted\"") != std::string::npos);
                REQUIRE(line.find("\"actual_semantic_description\":null") != std::string::npos);
                REQUIRE(line.find("\"audio_duration_seconds\":null") != std::string::npos);
            }
            REQUIRE(line.find("\"channel_index\":2") != std::string::npos);
            ++count;
        }
        REQUIRE(count == corpus.size() + 2);
        input.close();
        std::filesystem::remove(path);
    }
    std::string jsonFixture()
    {
        Fixture fixture;
        auto result = fixture.recognize("Channel 1 At 50");
        result.rawTranscript = "\"quoted\"\\path\n\t\r\b\f" + std::string(1, '\x01') + " \xe2\x80\x94 espa\xc3\xb1ol";
        result.detectedLanguage = "es";
        result.metrics = {2.5, 0.75};
        result.diagnostics.realTimeFactor = std::numeric_limits<double>::infinity();
        return core::benchmarkJson(snapshot(), "session-id", core::liveBenchmarkCases()[0], 2, result);
    }
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--repair-fixture")
        {
            Fixture fixture;
            std::cout << core::benchmarkJson(snapshot(), "repair", core::liveBenchmarkCases().front(),
                1, fixture.recognize("selecciona, fishtur cinco")) << '\n';
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--compute-fixtures")
        {
            Fixture fixture;
            using Backend = asr::ComputeBackend;
            const asr::ComputeDiscovery discovery{true, true,
                {{Backend::vulkan, 1, "Vulkan1", "GPU \"one\" · Vulkan", "gpu"}}, {}};
            for (auto requested : {Backend::automatic, Backend::cpu, Backend::vulkan})
            for (bool succeeds : {true, false}) {
                auto source = snapshot();
                source.configuration.compute = {requested, 1};
                auto recognized = fixture.recognize("Channel 1 At 50");
                recognized.diagnostics.compute = asr::initializeCompute(source.configuration.compute,
                    discovery, [succeeds](const auto& d) { return succeeds || d.backend == Backend::cpu; });
                for (auto outcome : {core::BenchmarkRecognitionOutcome::finalResult,
                        core::BenchmarkRecognitionOutcome::noSpeech,
                        core::BenchmarkRecognitionOutcome::emptyAsrResult,
                        core::BenchmarkRecognitionOutcome::operatorNoResult})
                    std::cout << core::benchmarkJson(source, "compute", core::liveBenchmarkCases().front(),
                        1, recognized, outcome) << '\n';
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--json-fixture")
        { std::cout << jsonFixture() << '\n'; return 0; }
        if (argc == 2 && std::string(argv[1]) == "--guidance-fixtures")
        {
            Fixture fixture;
            for (const bool grammar : {true, false})
            for (const bool prompt : {true, false})
            {
                auto source = snapshot();
                source.settings["whisper.grammar_enabled"] = grammar;
                source.settings["whisper.compact_prompt"] = prompt;
                fixture.backend->guidance = {{"whisper.grammar_enabled", grammar}, {"whisper.compact_prompt", prompt},
                    {"whisper.grammar_passed", grammar}, {"whisper.initial_prompt_passed", prompt}};
                if (grammar) fixture.backend->guidance["whisper.grammar_penalty"] = 20.0;
                std::cout << core::benchmarkJson(source, "guidance", core::liveBenchmarkCases().front(),
                    1, fixture.recognize("Channel 1 At 50")) << '\n';
                fixture.backend->guidance["whisper.grammar_passed"] = false;
                fixture.backend->guidance["whisper.initial_prompt_passed"] = false;
                fixture.backend->guidance.erase("whisper.grammar_penalty");
                core::RecognitionTranscript absent;
                absent.diagnostics = fixture.pipeline.attemptDiagnostics();
                std::cout << core::benchmarkJson(source, "guidance", core::liveBenchmarkCases().front(),
                    2, absent, core::BenchmarkRecognitionOutcome::noSpeech) << '\n';
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--outcome-fixtures")
        {
            Fixture fixture;
            auto recognized = fixture.recognize("Channel 1 At 50");
            recognized.metrics = {2.5, 0.75};
            recognized.detectedLanguage = "es";
            recognized.generation = 42;
            recognized.diagnostics.audioSamples = 48000;
            recognized.diagnostics.audioDurationSeconds = 3;
            recognized.diagnostics.inputPeak = 0.5;
            recognized.diagnostics.inputRms = 0.25;
            recognized.diagnostics.speechDetected = true;
            recognized.diagnostics.speechStartSeconds = 0.2;
            recognized.diagnostics.utteranceDurationSeconds = 2;
            recognized.diagnostics.inferenceDurationSeconds = 0.75;
            recognized.diagnostics.droppedAudioBlocks = 1;
            recognized.diagnostics.droppedVadFrames = 2;
            recognized.diagnostics.droppedInferenceJobs = 3;
            recognized.diagnostics.capturedFrames = 144000;
            recognized.diagnostics.droppedCaptureFrames = 1536;
            recognized.diagnostics.captureRingOccupancyFrames = 512;
            recognized.diagnostics.captureRingMaxOccupancyFrames = 1024;
            recognized.diagnostics.captureRingCapacityFrames = 48000;
            recognized.diagnostics.captureSessionRingHighWaterFrames = 2048;
            recognized.diagnostics.vadQueueDepth = 2; recognized.diagnostics.vadQueueMaxDepth = 256;
            recognized.diagnostics.inferenceQueueDepth = 1; recognized.diagnostics.inferenceQueueMaxDepth = 2;
            recognized.diagnostics.armedDurationSeconds = 4;
            recognized.diagnostics.capturedDurationSeconds = 3;
            recognized.diagnostics.utteranceFinalizedSeconds = 2.2;
            recognized.diagnostics.inferenceAudioDurationSeconds = 2.5;
            recognized.diagnostics.realTimeFactor = 0.3;
            recognized.diagnostics.endOfUtteranceToResultSeconds = 1.1;
            recognized.diagnostics.oldestVadQueueAgeSeconds = 0.1;
            recognized.diagnostics.vadQueuedAudioDurationSeconds = 0.064;
            recognized.diagnostics.vadQueueWaitTotalSeconds = 1;
            recognized.diagnostics.vadQueueWaitMaxSeconds = 0.2;
            recognized.diagnostics.inferenceQueueWaitTotalSeconds = 0.3;
            recognized.diagnostics.inferenceQueueWaitMaxSeconds = 0.3;
            recognized.diagnostics.commandProcessingDurationSeconds = 0.006;
            recognized.diagnostics.normalizationDurationSeconds = 0.001;
            recognized.diagnostics.assemblyDurationSeconds = 0.001;
            recognized.diagnostics.semanticParseDurationSeconds = 0.002;
            recognized.diagnostics.validationDurationSeconds = 0.001;
            recognized.diagnostics.renderingDurationSeconds = 0.001;
            using Outcome = core::BenchmarkRecognitionOutcome;
            for (auto outcome : {Outcome::finalResult, Outcome::noSpeech,
                    Outcome::emptyAsrResult, Outcome::operatorNoResult})
            {
                std::cout << core::benchmarkJson(snapshot(), "diagnostics", core::liveBenchmarkCases().front(),
                    1, recognized, outcome) << '\n';
                std::cout << core::benchmarkJson(snapshot(), "diagnostics", core::liveBenchmarkCases().back(),
                    1, recognized, outcome) << '\n';
            }
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--no-result-fixture")
        {
            // Deliberately supply stale data to verify no_result never claims it
            // came from inference. The explicit outcome takes precedence.
            Fixture fixture;
            auto stale = fixture.recognize("Channel 1 At 50");
            stale.detectedLanguage = "es";
            stale.metrics = {2.5, 0.75};
            auto source = snapshot();
            std::cout << core::benchmarkJson(source, "no-result-session", core::liveBenchmarkCases().front(),
                1, stale, core::BenchmarkRecognitionOutcome::operatorNoResult) << '\n';
            source.audioSource.channelDisplayName.reset();
            std::cout << core::benchmarkJson(source, "no-result-session", core::liveBenchmarkCases().back(),
                2, stale, core::BenchmarkRecognitionOutcome::operatorNoResult) << '\n';
            return 0;
        }
        unicodePathsAndJson(); corpusAndEvaluation(); metrics(); sessionAndJson(); noResultEvaluationAndSession();
        guidanceObservability();
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
