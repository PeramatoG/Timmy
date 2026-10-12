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
        void settings(std::ostream& out, const asr::SettingValues& values)
        {
            out << '{';
            bool first = true;
            for (const auto& [id, value] : values)
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
            out << '}';
        }
        void computeMetadata(std::ostream& out, const asr::ComputeSelection& requested,
            const std::optional<asr::ComputeDiagnostics>& diagnostics)
        {
            out << ",\"requested_compute_backend\":" << quoted(std::string(asr::computeBackendId(requested.backend)))
                << ",\"requested_device\":";
            if (requested.deviceIndex) out << *requested.deviceIndex;
            else out << "null";
            out << ",\"active_compute_backend\":";
            if (diagnostics && diagnostics->activeBackend)
                out << quoted(std::string(asr::computeBackendId(*diagnostics->activeBackend)));
            else out << "null";
            out << ",\"compute_device_index\":";
            if (diagnostics && diagnostics->activeDeviceIndex) out << *diagnostics->activeDeviceIndex;
            else out << "null";
            out << ",\"compute_device_name\":" << (diagnostics ? quoted(diagnostics->activeDeviceName) : "null")
                << ",\"active_device\":" << (diagnostics ? quoted(diagnostics->activeDeviceId) : "null")
                << ",\"fallback_used\":" << (diagnostics ? diagnostics->fallbackUsed ? "true" : "false" : "null")
                << ",\"fallback_reason\":" << (diagnostics ? quoted(diagnostics->fallbackReason) : "null")
                << ",\"compute_initialization_failure\":" << (diagnostics ? quoted(diagnostics->initializationFailure) : "null")
                << ",\"compute_discovery\":";
            if (!diagnostics) { out << "null"; return; }
            const auto& discovery = diagnostics->discovery;
            out << "{\"vulkan_compiled\":" << discovery.vulkanCompiled
                << ",\"vulkan_available\":" << discovery.vulkanAvailable << ",\"devices\":[";
            bool first = true;
            for (const auto& device : discovery.devices) {
                if (!first) out << ',';
                first = false;
                out << "{\"backend\":" << quoted(std::string(asr::computeBackendId(device.backend)))
                    << ",\"index\":" << device.index << ",\"id\":" << quoted(device.id)
                    << ",\"name\":" << quoted(device.name) << ",\"class\":" << quoted(device.deviceClass) << '}';
            }
            out << "]}";
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

    const char* benchmarkOutcomeName(BenchmarkRecognitionOutcome outcome)
    {
        switch (outcome)
        {
        case BenchmarkRecognitionOutcome::finalResult: return "final_result";
        case BenchmarkRecognitionOutcome::noSpeech: return "no_speech";
        case BenchmarkRecognitionOutcome::emptyAsrResult: return "empty_asr_result";
        case BenchmarkRecognitionOutcome::operatorNoResult: return "operator_no_result";
        }
        return "operator_no_result";
    }

    bool benchmarkPasses(const RecognitionBenchmarkCase& test, const RecognitionTranscript& actual,
        BenchmarkRecognitionOutcome outcome)
    {
        // Non-command cases pass when no command is produced, for every explicit
        // absence outcome. They measure false positives, not speech sensitivity.
        if (outcome != BenchmarkRecognitionOutcome::finalResult) return !test.expected;
        const bool accepted = actual.assemblyState == RecognitionAssemblyState::complete
            && actual.semanticCommand && actual.validation.accepted && actual.rendering.text;
        if (!test.expected) return !accepted;
        return accepted && sameIntent(*test.expected, *actual.semanticCommand);
    }

    std::string benchmarkJson(const RecognitionBenchmarkSnapshot& snapshot,
        const std::string& sessionId, const RecognitionBenchmarkCase& test,
        unsigned attempt, const RecognitionTranscript& recognized, BenchmarkRecognitionOutcome outcome)
    {
        const bool noResult = outcome == BenchmarkRecognitionOutcome::noSpeech
            || outcome == BenchmarkRecognitionOutcome::operatorNoResult;
        // A no-result attempt has no inference/transcript data, even if a caller
        // supplies stale data. It is kept distinct from an empty final ASR result.
        const RecognitionTranscript absent{};
        RecognitionTranscript empty;
        empty.detectedLanguage = recognized.detectedLanguage;
        empty.metrics = recognized.metrics;
        const auto& transcript = noResult ? absent
            : outcome == BenchmarkRecognitionOutcome::emptyAsrResult ? empty : recognized;
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<double>::max_digits10) << std::boolalpha;
        const auto assembly = noResult ? "not_attempted" : transcript.assemblyState == RecognitionAssemblyState::complete ? "complete"
            : transcript.assemblyState == RecognitionAssemblyState::pending ? "pending" : "rejected";
        const auto& config = snapshot.configuration;
        out << "{\"schema_version\":2,\"corpus_version\":" << quoted(liveBenchmarkCorpusVersion)
            << ",\"session_id\":" << quoted(sessionId) << ",\"timestamp\":" << quoted(timestamp())
            << ",\"case_id\":" << quoted(test.id) << ",\"category\":" << quoted(test.category)
            << ",\"displayed_phrase\":" << quoted(test.phrase)
            << ",\"expected_semantic_result\":" << (test.expected ? quoted(command::describe(*test.expected)) : "null")
            << ",\"expected_no_command\":" << !test.expected
            << ",\"recognition_outcome\":" << quoted(benchmarkOutcomeName(outcome))
            << ",\"raw_transcript\":" << (noResult ? "null" : quoted(transcript.rawTranscript))
            << ",\"normalized_transcript\":" << (noResult ? "null" : quoted(transcript.normalizedTranscript))
            << ",\"repaired_transcript\":" << (noResult ? "null" : quoted(transcript.repairedTranscript))
            << ",\"repair_applied\":" << (noResult ? "null" : transcript.repairApplied ? "true" : "false")
            << ",\"repairs\":";
        if (noResult) out << "null";
        else {
            out << '[';
            for (std::size_t i = 0; i < transcript.repairs.size(); ++i) {
                if (i) out << ',';
                const auto& repair = transcript.repairs[i];
                out << "{\"original\":" << quoted(repair.original)
                    << ",\"replacement\":" << quoted(repair.replacement)
                    << ",\"category\":" << quoted(repair.category)
                    << ",\"reason\":" << quoted(repair.reason) << '}';
            }
            out << ']';
        }
        out << ",\"numeric_inference_reasons\":";
        if (noResult) out << "null";
        else strings(out, transcript.numericInferenceReasons);
        out << ",\"assembly_state\":" << quoted(assembly) << ",\"assembly_text\":" << (noResult ? "null" : quoted(transcript.assemblyText))
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
            << ",\"selected_language\":" << nullable(config.preferredLanguage);
        computeMetadata(out, config.compute, recognized.diagnostics.compute);
        out << ",\"audio_source\":{\"device_display_name\":" << quoted(snapshot.audioSource.deviceDisplayName)
            << ",\"channel_index\":" << snapshot.audioSource.channelIndex
            << ",\"channel_display_name\":" << nullable(snapshot.audioSource.channelDisplayName) << '}'
            << ",\"recognition_settings\":{\"model_path\":" << quoted(pathText(config.modelResourcePath))
            << ",\"vad_model_path\":" << quoted(pathText(snapshot.vadModelPath))
            << ",\"language\":" << nullable(config.preferredLanguage)
            << ",\"engine_id\":" << quoted(snapshot.engineId)
            << ",\"command_profile_id\":" << quoted(snapshot.commandProfile.id)
            << ",\"rendering_target\":" << quoted(snapshot.commandProfile.rendering.target == command::RenderingTarget::nativeText ? "native" : "hydra2")
            << ",\"pre_clear\":" << snapshot.commandProfile.rendering.preClear
            << ",\"default_target\":" << (snapshot.commandProfile.parsing.defaultTarget
                ? quoted(std::string(command::defaultTargetName(*snapshot.commandProfile.parsing.defaultTarget))) : "null")
            << ",\"tuning\":";
        settings(out, snapshot.settings);
        out << ",\"active_guidance\":";
        if (recognized.diagnostics.recognitionGuidance.empty()) out << "null";
        else settings(out, recognized.diagnostics.recognitionGuidance);
        out << ",\"recognition_hints\":{\"constraint_intent\":"
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
        out << "},\"attempt_generation\":" << recognized.generation
            << ",\"diagnostics\":{\"audio_samples\":" << recognized.diagnostics.audioSamples
            << ",\"audio_duration_seconds\":";
        number(out, recognized.diagnostics.audioDurationSeconds);
        out << ",\"input_peak\":"; number(out, recognized.diagnostics.inputPeak);
        out << ",\"input_rms\":"; number(out, recognized.diagnostics.inputRms);
        out << ",\"speech_detected\":" << recognized.diagnostics.speechDetected
            << ",\"speech_start_seconds\":"; number(out, recognized.diagnostics.speechStartSeconds);
        out << ",\"utterance_duration_seconds\":"; number(out, recognized.diagnostics.utteranceDurationSeconds);
        out << ",\"inference_duration_seconds\":"; number(out, recognized.diagnostics.inferenceDurationSeconds);
        const auto counter = [&](const char* name, const std::optional<std::uint64_t>& value) {
            out << ",\"" << name << "\":";
            if (value) out << *value; else out << "null";
        };
        counter("dropped_audio_blocks", recognized.diagnostics.droppedAudioBlocks);
        counter("dropped_vad_frames", recognized.diagnostics.droppedVadFrames);
        counter("dropped_inference_jobs", recognized.diagnostics.droppedInferenceJobs);
        counter("captured_frames", recognized.diagnostics.capturedFrames);
        counter("dropped_capture_frames", recognized.diagnostics.droppedCaptureFrames);
        counter("capture_ring_occupancy_frames", recognized.diagnostics.captureRingOccupancyFrames);
        counter("capture_ring_max_occupancy_frames", recognized.diagnostics.captureRingMaxOccupancyFrames);
        counter("capture_ring_capacity_frames", recognized.diagnostics.captureRingCapacityFrames);
        counter("capture_session_ring_high_water_frames", recognized.diagnostics.captureSessionRingHighWaterFrames);
        counter("vad_queue_depth", recognized.diagnostics.vadQueueDepth);
        counter("vad_queue_max_depth", recognized.diagnostics.vadQueueMaxDepth);
        counter("inference_queue_depth", recognized.diagnostics.inferenceQueueDepth);
        counter("inference_queue_max_depth", recognized.diagnostics.inferenceQueueMaxDepth);
        out << ",\"armed_duration_seconds\":"; number(out, recognized.diagnostics.armedDurationSeconds);
        out << ",\"captured_duration_seconds\":"; number(out, recognized.diagnostics.capturedDurationSeconds);
        out << ",\"utterance_finalized_seconds\":"; number(out, recognized.diagnostics.utteranceFinalizedSeconds);
        out << ",\"inference_audio_duration_seconds\":"; number(out, recognized.diagnostics.inferenceAudioDurationSeconds);
        out << ",\"real_time_factor\":"; number(out, recognized.diagnostics.realTimeFactor);
        out << ",\"end_of_utterance_to_result_seconds\":"; number(out, recognized.diagnostics.endOfUtteranceToResultSeconds);
        out << ",\"oldest_vad_queue_age_seconds\":"; number(out, recognized.diagnostics.oldestVadQueueAgeSeconds);
        out << ",\"vad_queued_audio_duration_seconds\":"; number(out, recognized.diagnostics.vadQueuedAudioDurationSeconds);
        out << ",\"vad_queue_wait_total_seconds\":"; number(out, recognized.diagnostics.vadQueueWaitTotalSeconds);
        out << ",\"vad_queue_wait_max_seconds\":"; number(out, recognized.diagnostics.vadQueueWaitMaxSeconds);
        out << ",\"inference_queue_wait_total_seconds\":"; number(out, recognized.diagnostics.inferenceQueueWaitTotalSeconds);
        out << ",\"inference_queue_wait_max_seconds\":"; number(out, recognized.diagnostics.inferenceQueueWaitMaxSeconds);
        out << ",\"command_processing_duration_seconds\":"; number(out, recognized.diagnostics.commandProcessingDurationSeconds);
        out << ",\"normalization_duration_seconds\":"; number(out, recognized.diagnostics.normalizationDurationSeconds);
        out << ",\"assembly_duration_seconds\":"; number(out, recognized.diagnostics.assemblyDurationSeconds);
        out << ",\"semantic_parse_duration_seconds\":"; number(out, recognized.diagnostics.semanticParseDurationSeconds);
        out << ",\"validation_duration_seconds\":"; number(out, recognized.diagnostics.validationDurationSeconds);
        out << ",\"rendering_duration_seconds\":"; number(out, recognized.diagnostics.renderingDurationSeconds);

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
    asr::AsrOperationResult RecognitionBenchmarkSession::prepare(RecognitionPipeline& pipeline)
    {
        asr::AttemptGeneration nextGeneration;
        {
            std::lock_guard lock(mutex_);
            if (closed_ || state_ != BenchmarkAttemptState::preparing)
                return {false, asr::AsrDiagnostic{asr::AsrDiagnosticCode::invalidConfiguration,
                    "Attempt is not awaiting preparation."}};
            nextGeneration = ++generation_;
            diagnostics_ = {};
            speechAt_.reset(); inferenceAt_.reset();
        }
        const auto prepared = pipeline.prepareAttempt(nextGeneration);
        {
            std::lock_guard lock(mutex_);
            if (prepared.succeeded && !closed_) state_ = BenchmarkAttemptState::ready;
        }
        return prepared;
    }
    bool RecognitionBenchmarkSession::startAttempt(Clock::time_point now)
    {
        std::lock_guard lock(mutex_);
        if (closed_ || state_ != BenchmarkAttemptState::ready) return false;
        countdownAt_ = now;
        state_ = BenchmarkAttemptState::countdown;
        return true;
    }
    std::optional<RecognitionBenchmarkAttempt> RecognitionBenchmarkSession::tick(
        RecognitionPipeline& pipeline, Clock::time_point now)
    {
        bool arm = false;
        std::optional<RecognitionBenchmarkAttempt> timedOut;
        {
            std::lock_guard lock(mutex_);
            if (closed_) return {};
            if (state_ == BenchmarkAttemptState::countdown && now - countdownAt_ >= std::chrono::seconds(1))
            {
                speakAt_ = now;
                state_ = BenchmarkAttemptState::waitingSpeech;
                arm = true;
            }
            // Keep the speech-start cue visible briefly without delaying endpointing.
            if (state_ == BenchmarkAttemptState::speechDetected && speechAt_
                && now - *speechAt_ >= std::chrono::milliseconds(250))
                state_ = BenchmarkAttemptState::waitingEnd;
            // Backends without speech-start support may report a later stage;
            // the five-second deadline applies only while waiting for speech.
            if (!arm && state_ == BenchmarkAttemptState::waitingSpeech && now - speakAt_ >= std::chrono::seconds(5))
            {
                RecognitionTranscript absent;
                absent.generation = generation_;
                absent.diagnostics = pipeline.attemptDiagnostics();
                timedOut = recordAttempt(absent, BenchmarkRecognitionOutcome::noSpeech);
            }
        }
        if (timedOut) pipeline.disarmAttempt();
        if (arm) pipeline.armAttempt();
        return timedOut;
    }
    void RecognitionBenchmarkSession::progress(const asr::RecognitionProgress& event)
    {
        std::lock_guard lock(mutex_);
        if (closed_ || event.generation != generation_ || !awaiting_
            || state_ == BenchmarkAttemptState::preparing || state_ == BenchmarkAttemptState::ready
            || state_ == BenchmarkAttemptState::countdown) return;
        if (event.timestamp < speakAt_) return;
        if (event.droppedAudioBlocks) diagnostics_.droppedAudioBlocks = event.droppedAudioBlocks;
        if (event.droppedVadFrames) diagnostics_.droppedVadFrames = event.droppedVadFrames;
        if (event.droppedInferenceJobs) diagnostics_.droppedInferenceJobs = event.droppedInferenceJobs;
        using Stage = asr::RecognitionProgressState;
        switch (event.state)
        {
        case Stage::speechStarted:
            if (!speechAt_)
            {
                speechAt_ = event.timestamp;
                diagnostics_.speechDetected = true;
                diagnostics_.speechStartSeconds = std::chrono::duration<double>(*speechAt_ - speakAt_).count();
                state_ = BenchmarkAttemptState::speechDetected;
            }
            break;
        case Stage::utteranceFinalized:
            diagnostics_.utteranceFinalizedSeconds = std::chrono::duration<double>(event.timestamp - speakAt_).count();
            if (speechAt_) diagnostics_.utteranceDurationSeconds = std::chrono::duration<double>(event.timestamp - *speechAt_).count();
            state_ = BenchmarkAttemptState::processing;
            break;
        case Stage::inferenceStarted:
            inferenceAt_ = event.timestamp;
            state_ = BenchmarkAttemptState::processing;
            break;
        case Stage::inferenceCompletedWithText:
        case Stage::inferenceCompletedNoText:
            if (inferenceAt_) diagnostics_.inferenceDurationSeconds = std::chrono::duration<double>(event.timestamp - *inferenceAt_).count();
            break; // Final transcript arbitrates and records the outcome.
        default: break;
        }
    }
    BenchmarkAttemptState RecognitionBenchmarkSession::attemptState() const
    { std::lock_guard lock(mutex_); return state_; }
    asr::AttemptGeneration RecognitionBenchmarkSession::generation() const
    { std::lock_guard lock(mutex_); return generation_; }
    std::optional<RecognitionBenchmarkAttempt> RecognitionBenchmarkSession::record(const RecognitionTranscript& transcript)
    {
        std::lock_guard lock(mutex_);
        return recordAttempt(transcript, transcript.normalizedTranscript.empty()
            ? BenchmarkRecognitionOutcome::emptyAsrResult : BenchmarkRecognitionOutcome::finalResult);
    }
    std::optional<RecognitionBenchmarkAttempt> RecognitionBenchmarkSession::recordNoResult(asr::AttemptDiagnostics diagnostics)
    {
        std::lock_guard lock(mutex_);
        RecognitionTranscript absent;
        absent.generation = generation_;
        absent.diagnostics = diagnostics;
        return recordAttempt(absent, BenchmarkRecognitionOutcome::operatorNoResult);
    }
    std::optional<RecognitionBenchmarkAttempt> RecognitionBenchmarkSession::recordAttempt(
        const RecognitionTranscript& transcript, BenchmarkRecognitionOutcome outcome)
    {
        // Caller holds mutex_, including timeout/progress/result arbitration.
        if (closed_ || !awaiting_ || transcript.generation != generation_
            || state_ == BenchmarkAttemptState::preparing || state_ == BenchmarkAttemptState::ready
            || state_ == BenchmarkAttemptState::countdown) return std::nullopt;
        auto measured = transcript;
        measured.diagnostics.speechDetected = diagnostics_.speechDetected;
        measured.diagnostics.speechStartSeconds = diagnostics_.speechStartSeconds;
        measured.diagnostics.utteranceDurationSeconds = diagnostics_.utteranceDurationSeconds;
        measured.diagnostics.utteranceFinalizedSeconds = transcript.metrics.utteranceFinalizedAt
            ? std::optional<double>{std::chrono::duration<double>(*transcript.metrics.utteranceFinalizedAt - speakAt_).count()}
            : diagnostics_.utteranceFinalizedSeconds;
        measured.diagnostics.inferenceDurationSeconds = transcript.metrics.inferenceDurationSeconds
            ? transcript.metrics.inferenceDurationSeconds : diagnostics_.inferenceDurationSeconds;
        if (transcript.metrics.audioDurationSeconds)
            measured.diagnostics.inferenceAudioDurationSeconds = transcript.metrics.audioDurationSeconds;
        if (transcript.metrics.endOfUtteranceToResultSeconds)
            measured.diagnostics.endOfUtteranceToResultSeconds = transcript.metrics.endOfUtteranceToResultSeconds;
        if (measured.diagnostics.inferenceDurationSeconds && measured.diagnostics.inferenceAudioDurationSeconds
            && *measured.diagnostics.inferenceAudioDurationSeconds > 0)
            measured.diagnostics.realTimeFactor = *measured.diagnostics.inferenceDurationSeconds / *measured.diagnostics.inferenceAudioDurationSeconds;
        if (!measured.diagnostics.droppedAudioBlocks) measured.diagnostics.droppedAudioBlocks = diagnostics_.droppedAudioBlocks;
        if (!measured.diagnostics.droppedVadFrames) measured.diagnostics.droppedVadFrames = diagnostics_.droppedVadFrames;
        if (!measured.diagnostics.droppedInferenceJobs) measured.diagnostics.droppedInferenceJobs = diagnostics_.droppedInferenceJobs;
        const auto& test = liveBenchmarkCases().at(index_);
        // Flush each complete attempt before publishing a result to the operator.
        output_ << benchmarkJson(snapshot_, sessionId_, test, attempt_, measured, outcome) << '\n';
        output_.flush();
        awaiting_ = false;
        state_ = BenchmarkAttemptState::result;
        return RecognitionBenchmarkAttempt{test.id, attempt_, benchmarkPasses(test, measured, outcome), outcome, generation_, measured.diagnostics};
    }
    bool RecognitionBenchmarkSession::next()
    {
        std::lock_guard lock(mutex_);
        if (closed_ || awaiting_ || index_ + 1 == liveBenchmarkCases().size()) return false;
        ++index_;
        attempt_ = 1;
        awaiting_ = true;
        state_ = BenchmarkAttemptState::preparing;
        return true;
    }
    void RecognitionBenchmarkSession::retry()
    {
        std::lock_guard lock(mutex_);
        if (closed_ || awaiting_) return;
        ++attempt_;
        awaiting_ = true;
        state_ = BenchmarkAttemptState::preparing;
    }
    void RecognitionBenchmarkSession::close()
    {
        std::lock_guard lock(mutex_);
        if (closed_) return;
        closed_ = true;
        state_ = BenchmarkAttemptState::closed;
        output_.close();
    }
    bool RecognitionBenchmarkSession::awaitingResult() const
    { std::lock_guard lock(mutex_); return !closed_ && awaiting_; }
    std::size_t RecognitionBenchmarkSession::index() const
    { std::lock_guard lock(mutex_); return index_; }
} // namespace timmy::core
