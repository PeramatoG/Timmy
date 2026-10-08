/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "timmy/core/recognition_benchmark.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <locale>
#include <random>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace timmy::core {
    namespace {
        using namespace command;
        SemanticCommand intensity(TargetType type, std::uint32_t id, int level)
        {
            return SetIntensity{TargetSelection{type, {{id, id}}}, *IntensityLevel::fromPercent(level)};
        }
        std::string quoted(const std::string& text)
        {
            std::string result = "\"";
            constexpr char hex[] = "0123456789abcdef";
            for (unsigned char c : text)
            {
                switch (c)
                {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (c < 0x20) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
                    else result += static_cast<char>(c);
                }
            }
            return result + '"';
        }
        std::string nullable(const std::optional<std::string>& value)
        { return value ? quoted(*value) : "null"; }
        std::string pathText(const std::filesystem::path& path)
        {
            const auto utf8 = path.u8string();
            return {utf8.begin(), utf8.end()};
        }
        std::string timestamp()
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::tm utc{};
#ifdef _WIN32
            gmtime_s(&utc, &time);
#else
            gmtime_r(&time, &utc);
#endif
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
                << std::setw(3) << std::setfill('0')
                << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000
                << 'Z';
            return out.str();
        }
        std::string newSessionId()
        {
            static std::atomic<unsigned> sequence{0};
            std::random_device random;
            return timestamp() + '-' + std::to_string(random()) + '-' + std::to_string(++sequence);
        }
        void number(std::ostream& out, const std::optional<double>& value)
        {
            if (value && std::isfinite(*value)) out << *value;
            else out << "null";
        }
        void strings(std::ostream& out, const std::vector<std::string>& values)
        {
            out << '[';
            bool first = true;
            for (const auto& value : values)
            {
                if (!first) out << ',';
                first = false;
                out << quoted(value);
            }
            out << ']';
        }
        // Compare semantic operations, including equivalent lists/ranges. Merge
        // and subtract compact intervals without expanding console identifiers.
        using Span = std::pair<std::uint64_t, std::uint64_t>;
        std::vector<Span> merged(const std::vector<TargetRange>& ranges)
        {
            std::vector<Span> result;
            for (auto range : ranges) result.emplace_back(range.first, range.last);
            std::sort(result.begin(), result.end());
            std::vector<Span> compact;
            for (auto range : result)
            {
                if (!compact.empty() && range.first <= compact.back().second + 1)
                    compact.back().second = std::max(compact.back().second, range.second);
                else compact.push_back(range);
            }
            return compact;
        }
        std::vector<Span> selected(const TargetSelection& selection)
        {
            auto included = merged(selection.included);
            const auto excluded = merged(selection.excluded);
            std::vector<Span> result;
            for (auto range : included)
            {
                auto start = range.first;
                for (auto removed : excluded)
                {
                    if (removed.second < start || removed.first > range.second) continue;
                    if (removed.first > start) result.emplace_back(start, removed.first - 1);
                    start = std::max(start, removed.second + 1);
                    if (start > range.second) break;
                }
                if (start <= range.second) result.emplace_back(start, range.second);
            }
            return result;
        }
        bool sameSelection(const TargetSelection& a, const TargetSelection& b)
        {
            return a.type == b.type && a.all == b.all && a.isValid() && b.isValid()
                && (a.all || selected(a) == selected(b));
        }
        bool sameIntent(const SemanticCommand& a, const SemanticCommand& b)
        {
            if (a.index() != b.index()) return false;
            return std::visit([&](const auto& left) {
                using T = std::decay_t<decltype(left)>;
                const auto& right = std::get<T>(b);
                if constexpr (std::is_same_v<T, SetIntensity>)
                    return sameSelection(left.target, right.target) && left.level.percent() == right.level.percent();
                else if constexpr (std::is_same_v<T, ModifySelection>)
                    return left.modification == right.modification && sameSelection(left.target, right.target);
                else if constexpr (std::is_same_v<T, NavigateSelection>)
                    return left.type == right.type && left.direction == right.direction;
                else if constexpr (requires { left.target; })
                    return sameSelection(left.target, right.target);
                else return true;
            }, a);
        }
    }

    const std::vector<RecognitionBenchmarkCase>& liveBenchmarkCases()
    {
        using namespace command;
        static const std::vector<RecognitionBenchmarkCase> cases{
            {"es-num-1", "numeric-robustness", "Canal 1 al 50", intensity(TargetType::channel, 1, 50)},
            {"es-num-5", "numeric-robustness", "Canal 5 al 50", intensity(TargetType::channel, 5, 50)},
            {"es-num-15", "numeric-robustness", "Canal 15 al 50", intensity(TargetType::channel, 15, 50)},
            {"es-num-25", "numeric-robustness", "Canal 25 al 50", intensity(TargetType::channel, 25, 50)},
            {"es-num-35", "numeric-robustness", "Canal 35 al 50", intensity(TargetType::channel, 35, 50)},
            {"es-num-105", "numeric-robustness", "Canal 105 al 50", intensity(TargetType::channel, 105, 50)},
            {"es-num-115", "numeric-robustness", "Canal 115 al 50", intensity(TargetType::channel, 115, 50)},
            {"es-num-150", "numeric-robustness", "Canal 150 al 50", intensity(TargetType::channel, 150, 50)},
            {"es-num-515", "numeric-robustness", "Fixture 515 al 50", intensity(TargetType::fixture, 515, 50)},
            {"es-num-550", "numeric-robustness", "Fixture 550 al 50", intensity(TargetType::fixture, 550, 50)},
            {"level-zero", "intensity", "Apaga el canal 5", intensity(TargetType::channel, 5, 0)},
            {"level-full", "intensity", "Enciende canal 1", intensity(TargetType::channel, 1, 100)},
            {"fixture-full", "fixture", "Fixture 1 a tope", intensity(TargetType::fixture, 1, 100)},
            {"group-half", "group", "Grupo 2 al 50", intensity(TargetType::group, 2, 50)},
            {"range-level", "range", "Canales del 2 al 40 al 50", SetIntensity{TargetSelection{TargetType::channel, {{2,40}}}, *IntensityLevel::fromPercent(50)}},
            {"range-on", "range", "Enciende los canales del 2 al 40", SetIntensity{TargetSelection{TargetType::channel, {{2,40}}}, *IntensityLevel::fromPercent(100)}},
            {"list-select", "list", "Selecciona fixtures 1, 3 y 5", SelectSelection{TargetSelection{TargetType::fixture, {{1,1},{3,3},{5,5}}}}},
            {"exclude-select", "exclusion", "Dame los canales del 1 al 10 y me quitas el 6", SelectSelection{TargetSelection{TargetType::channel, {{1,10}}, {{6,6}}}}},
            {"exclude-level", "exclusion", "Canales 1 a 10 excepto 5 al 50", SetIntensity{TargetSelection{TargetType::channel, {{1,10}}, {{5,5}}}, *IntensityLevel::fromPercent(50)}},
            {"select-single", "select", "Selecciona canal 5", SelectSelection{Channel{5}}},
            {"test-single", "test", "Prueba canal 1", TestSelection{Channel{1}}},
            {"home-fixture", "home", "Lleva fixture 5 a home", HomeSelection{Fixture{5}}},
            {"editor-clear", "editor", "Limpia editor", ClearEditor{}},
            {"release-single", "release", "Libera canal 5", ReleaseSelection{Channel{5}}},
            {"en-num-13", "english-numeric-confusion", "Channel 13 at 50", intensity(TargetType::channel, 13, 50)},
            {"en-num-30", "english-numeric-confusion", "Channel 30 at 50", intensity(TargetType::channel, 30, 50)},
            {"en-num-15", "english-numeric-confusion", "Channel 15 at 50", intensity(TargetType::channel, 15, 50)},
            {"en-num-50", "english-numeric-confusion", "Channel 50 at 50", intensity(TargetType::channel, 50, 50)},
            {"en-num-17", "english-numeric-confusion", "Channel 17 at 50", intensity(TargetType::channel, 17, 50)},
            {"en-num-70", "english-numeric-confusion", "Channel 70 at 50", intensity(TargetType::channel, 70, 50)},
            {"en-select", "english", "Select groups 1 through 4 except 2", SelectSelection{TargetSelection{TargetType::group, {{1,4}}, {{2,2}}}}},
            {"en-home", "english", "Home fixture 5", HomeSelection{Fixture{5}}},
            {"reject-greeting", "non-command", "Hola, buenos días", std::nullopt},
            {"reject-weather", "non-command", "Hoy hace buen tiempo", std::nullopt},
            {"reject-thanks", "non-command", "Muchas gracias por venir", std::nullopt},
            {"reject-english", "non-command", "The microphone is working", std::nullopt}
        };
        return cases;
    }

    bool benchmarkPasses(const RecognitionBenchmarkCase& test, const RecognitionTranscript& actual,
        BenchmarkRecognitionOutcome outcome)
    {
        if (outcome == BenchmarkRecognitionOutcome::noResult) return !test.expected;
        const bool accepted = actual.assemblyState == RecognitionAssemblyState::complete
            && actual.semanticCommand && actual.validation.accepted && actual.rendering.text;
        if (!test.expected) return !accepted;
        return accepted && sameIntent(*test.expected, *actual.semanticCommand);
    }

    std::string benchmarkJson(const RecognitionBenchmarkSnapshot& snapshot,
        const std::string& sessionId, const RecognitionBenchmarkCase& test,
        unsigned attempt, const RecognitionTranscript& recognized, BenchmarkRecognitionOutcome outcome)
    {
        const bool noResult = outcome == BenchmarkRecognitionOutcome::noResult;
        // A no-result attempt has no inference/transcript data, even if a caller
        // supplies stale data. It is kept distinct from an empty final ASR result.
        const RecognitionTranscript absent{};
        const auto& transcript = noResult ? absent : recognized;
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<double>::max_digits10) << std::boolalpha;
        const auto assembly = noResult ? "not_attempted" : transcript.assemblyState == RecognitionAssemblyState::complete ? "complete"
            : transcript.assemblyState == RecognitionAssemblyState::pending ? "pending" : "rejected";
        const auto& config = snapshot.configuration;
        out << "{\"schema_version\":1,\"corpus_version\":" << quoted(liveBenchmarkCorpusVersion)
            << ",\"session_id\":" << quoted(sessionId) << ",\"timestamp\":" << quoted(timestamp())
            << ",\"case_id\":" << quoted(test.id) << ",\"category\":" << quoted(test.category)
            << ",\"displayed_phrase\":" << quoted(test.phrase)
            << ",\"expected_semantic_result\":" << (test.expected ? quoted(command::describe(*test.expected)) : "null")
            << ",\"expected_no_command\":" << !test.expected
            << ",\"recognition_outcome\":" << quoted(noResult ? "no_result" : "final_result")
            << ",\"raw_transcript\":" << (noResult ? "null" : quoted(transcript.rawTranscript))
            << ",\"normalized_transcript\":" << (noResult ? "null" : quoted(transcript.normalizedTranscript))
            << ",\"assembly_state\":" << quoted(assembly) << ",\"assembly_text\":" << (noResult ? "null" : quoted(transcript.assemblyText))
            << ",\"actual_semantic_description\":" << (transcript.semanticCommand ? quoted(command::describe(*transcript.semanticCommand)) : "null")
            << ",\"validation_state\":" << quoted(!transcript.semanticCommand ? "not_attempted" : transcript.validation.accepted ? "accepted" : "rejected")
            << ",\"validation_diagnostic\":" << quoted(transcript.validation.diagnostic)
            << ",\"rendered_command\":" << nullable(transcript.rendering.text)
            << ",\"rendering_diagnostic\":" << quoted(transcript.rendering.diagnostic)
            << ",\"result\":" << quoted(benchmarkPasses(test, transcript, outcome) ? "PASS" : "FAIL")
            << ",\"detected_language\":" << nullable(transcript.detectedLanguage)
            << ",\"asr_engine_id\":" << quoted(snapshot.engineId)
            << ",\"model_filename\":" << quoted(pathText(config.modelResourcePath.filename()))
            << ",\"vad_model_filename\":" << quoted(pathText(snapshot.vadModelPath.filename()))
            << ",\"selected_language\":" << nullable(config.preferredLanguage)
            << ",\"audio_source\":{\"device_display_name\":" << quoted(snapshot.audioSource.deviceDisplayName)
            << ",\"channel_index\":" << snapshot.audioSource.channelIndex
            << ",\"channel_display_name\":" << nullable(snapshot.audioSource.channelDisplayName) << '}'
            << ",\"recognition_settings\":{\"model_path\":" << quoted(pathText(config.modelResourcePath))
            << ",\"vad_model_path\":" << quoted(pathText(snapshot.vadModelPath))
            << ",\"language\":" << nullable(config.preferredLanguage)
            << ",\"engine_id\":" << quoted(snapshot.engineId)
            << ",\"command_profile_id\":" << quoted(snapshot.commandProfile.id)
            << ",\"rendering_target\":" << quoted(snapshot.commandProfile.rendering.target == command::RenderingTarget::nativeText ? "native" : "hydra2")
            << ",\"pre_clear\":" << snapshot.commandProfile.rendering.preClear
            << ",\"tuning\":{";
        bool first = true;
        for (const auto& [id, value] : snapshot.settings)
        {
            if (!first) out << ',';
            first = false;
            out << quoted(id) << ':';
            std::visit([&](auto v) {
                if constexpr (std::is_same_v<decltype(v), double>)
                    if (!std::isfinite(v)) throw std::invalid_argument("Nonfinite benchmark setting.");
                out << v;
            }, value);
        }
        out << "},\"recognition_hints\":{\"constraint_intent\":"
            << quoted(config.recognitionHints.constraintIntent == asr::RecognitionConstraintIntent::commandPhrase ? "command_phrase" : "none")
            << ",\"vocabulary\":";
        strings(out, config.recognitionHints.vocabulary);
        out << ",\"expected_phrases\":";
        strings(out, config.recognitionHints.expectedPhrases);
        out << "}},\"attempt_number\":" << attempt << ",\"retry_number\":" << attempt - 1
            << ",\"metrics\":{\"audio_duration_seconds\":";
        number(out, transcript.metrics.audioDurationSeconds);
        out << ",\"inference_duration_seconds\":";
        number(out, transcript.metrics.inferenceDurationSeconds);
        out << "}}";
        return out.str();
    }

    RecognitionBenchmarkSession::RecognitionBenchmarkSession(std::filesystem::path resultPath,
        RecognitionBenchmarkSnapshot snapshot)
        : path_(std::move(resultPath)), snapshot_(std::move(snapshot)), sessionId_(newSessionId()),
          output_(path_, std::ios::binary | std::ios::app)
    {
        if (!output_) throw std::runtime_error("Cannot open benchmark JSONL result file.");
        output_.exceptions(std::ios::badbit | std::ios::failbit);
    }
    RecognitionBenchmarkSession::~RecognitionBenchmarkSession()
    {
        try { close(); } catch (...) { /* Explicit close reports errors to the frontend. */ }
    }
    std::optional<RecognitionBenchmarkAttempt> RecognitionBenchmarkSession::record(const RecognitionTranscript& transcript)
    {
        return recordAttempt(transcript, BenchmarkRecognitionOutcome::finalResult);
    }
    std::optional<RecognitionBenchmarkAttempt> RecognitionBenchmarkSession::recordNoResult()
    {
        return recordAttempt({}, BenchmarkRecognitionOutcome::noResult);
    }
    std::optional<RecognitionBenchmarkAttempt> RecognitionBenchmarkSession::recordAttempt(
        const RecognitionTranscript& transcript, BenchmarkRecognitionOutcome outcome)
    {
        std::lock_guard lock(mutex_);
        if (closed_ || !awaiting_) return std::nullopt;
        const auto& test = liveBenchmarkCases().at(index_);
        // Flush each complete attempt before publishing a result to the operator.
        output_ << benchmarkJson(snapshot_, sessionId_, test, attempt_, transcript, outcome) << '\n';
        output_.flush();
        awaiting_ = false;
        return RecognitionBenchmarkAttempt{test.id, attempt_, benchmarkPasses(test, transcript, outcome), outcome};
    }
    bool RecognitionBenchmarkSession::next()
    {
        std::lock_guard lock(mutex_);
        if (closed_ || awaiting_ || index_ + 1 == liveBenchmarkCases().size()) return false;
        ++index_;
        attempt_ = 1;
        awaiting_ = true;
        return true;
    }
    void RecognitionBenchmarkSession::retry()
    {
        std::lock_guard lock(mutex_);
        if (closed_ || awaiting_) return;
        ++attempt_;
        awaiting_ = true;
    }
    void RecognitionBenchmarkSession::close()
    {
        std::lock_guard lock(mutex_);
        if (closed_) return;
        closed_ = true;
        output_.close();
    }
    bool RecognitionBenchmarkSession::awaitingResult() const
    { std::lock_guard lock(mutex_); return !closed_ && awaiting_; }
    std::size_t RecognitionBenchmarkSession::index() const
    { std::lock_guard lock(mutex_); return index_; }
} // namespace timmy::core
