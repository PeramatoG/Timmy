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
        void emit(asr::AsrResult result) { publishResult(result); }
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
        return value;
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
            1, fixture.recognize("Channel 13 At 50"));
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
        REQUIRE(corpus.size() == 32);
        const std::vector<std::string> ids{
            "num-13","num-30","num-15","num-50","num-17","num-70","num-105","num-150","num-515","num-550",
            "level-zero","level-full","fixture-full","group-half","range-level","range-on","list-select",
            "exclude-select","exclude-level","select-single","test-single","home-fixture","editor-clear",
            "release-single","en-level","en-select","en-home","en-editor","reject-greeting","reject-weather",
            "reject-thanks","reject-english"};
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
        REQUIRE(core::benchmarkPasses(corpus[0], fixture.recognize("Channel 13 At 50")));
        REQUIRE(!core::benchmarkPasses(corpus[0], fixture.recognize("Channel 30 At 50")));
        REQUIRE(!core::benchmarkPasses(corpus[0], fixture.recognize("Channel 13 At Full")));
        REQUIRE(!core::benchmarkPasses(corpus[0], fixture.recognize("Fixture 13 At 50")));
        REQUIRE(!core::benchmarkPasses(corpus.back(), fixture.recognize("Channel 13 At 50")));
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
    }
    void metrics()
    {
        Fixture fixture;
        fixture.backend->emit({"Channel 13 At 50", asr::AsrResultKind::final, "es", {2.5, 0.75}});
        REQUIRE(fixture.observer->last.detectedLanguage == "es");
        REQUIRE(fixture.observer->last.metrics.audioDurationSeconds == 2.5);
        REQUIRE(fixture.observer->last.metrics.inferenceDurationSeconds == 0.75);
        const auto count = fixture.observer->calls;
        fixture.backend->emit({"Channel 1 At 50", asr::AsrResultKind::partial, "en", {}});
        REQUIRE(fixture.observer->calls == count);
        fixture.recognize("Channel 13 At 50");
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
        original.settings["whisper.threads"] = 7;
        REQUIRE(session.snapshot().settings.at("whisper.threads") != original.settings.at("whisper.threads"));
        REQUIRE(!session.next());
        session.retry(); // Cannot manufacture attempts before a recognition result.
        REQUIRE(session.record(fixture.recognize("Channel 13 At 50"))->attempt == 1);
        REQUIRE(!session.awaitingResult() && session.index() == 0);
        REQUIRE(!session.record(fixture.recognize("Channel 30 At 50")));
        session.retry();
        const auto retry = session.record(fixture.recognize("Channel 30 At 50"));
        REQUIRE(retry && retry->attempt == 2 && !retry->passed && retry->caseId == "num-13");
        REQUIRE(session.next());
        const auto next = session.record(fixture.recognize("Channel 30 At 50"));
        REQUIRE(next && next->attempt == 1 && next->passed && next->caseId == "num-30");
        // All writes are visible even before close, without requiring GUI events.
        std::ifstream in(path);
        std::string line;
        unsigned lines = 0;
        while (std::getline(in, line))
        {
            REQUIRE(line.front() == '{' && line.back() == '}');
            REQUIRE(line.find("\"recognition_settings\":{") != std::string::npos);
            REQUIRE(line.find("\"schema_version\":1") != std::string::npos);
            REQUIRE(line.find("\"audio_duration_seconds\":null") != std::string::npos);
            ++lines;
        }
        REQUIRE(lines == 3);
        for (std::size_t i = 2; i < core::liveBenchmarkCases().size(); ++i)
        {
            REQUIRE(session.next());
            REQUIRE(session.record(fixture.recognize(core::liveBenchmarkCases()[i].phrase)).has_value());
        }
        REQUIRE(!session.next());
        session.retry();
        REQUIRE(session.record(fixture.recognize(core::liveBenchmarkCases().back().phrase))->attempt == 2);
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
    std::string jsonFixture()
    {
        Fixture fixture;
        auto result = fixture.recognize("Channel 13 At 50");
        result.rawTranscript = "\"quoted\"\\path\n\t\r\b\f" + std::string(1, '\x01') + " \xe2\x80\x94 espa\xc3\xb1ol";
        result.detectedLanguage = "es";
        result.metrics = {2.5, 0.75};
        return core::benchmarkJson(snapshot(), "session-id", core::liveBenchmarkCases()[0], 2, result);
    }
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--json-fixture")
        { std::cout << jsonFixture() << '\n'; return 0; }
        unicodePathsAndJson(); corpusAndEvaluation(); metrics(); sessionAndJson();
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
