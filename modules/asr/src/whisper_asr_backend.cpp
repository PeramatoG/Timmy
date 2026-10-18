/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/asr/whisper_asr_backend.h"

#include "whisper_command_decoding.h"
#include "whisper_configuration.h"
#include "whisper_endpoint_detector.h"
#include "whisper_inference_cancellation.h"
#include "diagnostic_queue.h"
#include "whisper_recognition_guidance.h"
#include "whisper_compute_runtime.h"

#include "grammar-parser.h"
#include <whisper.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace timmy::asr {

    namespace {

        constexpr std::size_t windowSampleCount =
            IAsrBackend::requiredSampleRate * 5;
        constexpr std::size_t maximumQueuedWindows = 2;
        constexpr std::size_t vadFrameSampleCount = 512;
        constexpr std::size_t maximumQueuedVadFrames = 256;

        // Writes only warning and error messages from whisper.cpp and ggml.
        void whisperLogCallback(
            enum ggml_log_level level,
            const char* text,
            void*)
        {
            thread_local bool writeContinuation = false;
            if (level != GGML_LOG_LEVEL_CONT)
            {
                writeContinuation = level == GGML_LOG_LEVEL_WARN
                    || level == GGML_LOG_LEVEL_ERROR;
            }

            if (writeContinuation && text != nullptr)
            {
                std::fputs(text, stderr);
                std::fflush(stderr);
            }
        }

        // Installs Timmy's process-wide Whisper log filter exactly once.
        void configureWhisperLogging()
        {
            static std::once_flag loggingConfigured;
            std::call_once(loggingConfigured, [] {
                whisper_log_set(whisperLogCallback, nullptr);
            });
        }

        // Removes surrounding whitespace from a completed transcript.
        std::string trimTranscript(std::string transcript)
        {
            const auto first = transcript.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return {};
            }

            const auto last = transcript.find_last_not_of(" \t\r\n");
            return transcript.substr(first, last - first + 1);
        }

        // Creates a failed operation result with a structured diagnostic.
        AsrOperationResult failure(
            AsrDiagnosticCode code,
            std::string message)
        {
            return { false, AsrDiagnostic{ code, std::move(message) } };
        }

    } // namespace

    ComputeDiscovery whisperComputeDevices()
    {
        configureWhisperLogging();
        return detail::discoverWhisperCompute();
    }

    class WhisperAsrBackend::Impl
    {
    public:
        explicit Impl(WhisperAsrBackendOptions configuration)
            : options(std::move(configuration)),
              tuning(detail::makeWhisperConfiguration(options.settings,
                  std::thread::hardware_concurrency())), endpoint(tuning.endpoint)
        {
        }

        // Releases all worker and model resources in dependency order.
        ~Impl()
        {
            stopWorkers();
            destroyContexts();
        }

        // Runs queued Whisper recognition jobs until shutdown is complete.
        void runInferenceWorker(WhisperAsrBackend& owner)
        {
            while (true)
            {
                std::vector<float> samples;
                AttemptGeneration jobGeneration;
                std::optional<std::chrono::steady_clock::time_point> finalizedAt;
                {
                    std::unique_lock lock(mutex);
                    inferenceCondition.wait(lock, [this] {
                        return shutdownRequested || !windows.empty();
                    });
                    if (shutdownRequested && windows.empty())
                    {
                        break;
                    }
                    jobGeneration = generation;
                    auto job = windows.pop();
                    samples = std::move(job.value);
                    if (job.finalized) finalizedAt = job.queuedAt;
                }
                infer(owner, samples, jobGeneration, finalizedAt);
            }
        }

        // Runs streaming Silero VAD and sends completed utterances to Whisper.
        void runVadWorker(WhisperAsrBackend& owner)
        {
            while (true)
            {
                std::vector<float> samples;
                {
                    std::unique_lock lock(mutex);
                    vadCondition.wait(lock, [this] {
                        return shutdownRequested || !vadFrames.empty();
                    });
                    if (shutdownRequested && vadFrames.empty())
                    {
                        break;
                    }
                    samples = std::move(vadFrames.pop().value);
                }

                if (!whisper_vad_detect_speech_no_reset(
                        vadContext,
                        samples.data(),
                        static_cast<int>(samples.size())))
                {
                    continue;
                }
                const int probabilityCount = whisper_vad_n_probs(vadContext);
                const float* probabilities = whisper_vad_probs(vadContext);
                if (probabilityCount == 0 || probabilities == nullptr)
                {
                    continue;
                }

                const bool wasSpeaking = endpoint.speaking();
                auto utterance = endpoint.process(
                    samples, probabilities[probabilityCount - 1]);
                if (!wasSpeaking && (endpoint.speaking() || utterance))
                    progress(owner, RecognitionProgressState::speechStarted, generation);
                if (utterance)
                {
                    whisper_vad_reset_state(vadContext);
                    const auto finalizedAt = std::chrono::steady_clock::now();
                    progress(owner, RecognitionProgressState::utteranceFinalized, generation, finalizedAt);
                    enqueueInference(std::move(*utterance), finalizedAt);
                }
            }
        }

        // Adds one completed utterance to the bounded inference queue.
        void enqueueInference(std::vector<float> samples, std::chrono::steady_clock::time_point finalizedAt)
        {
            {
                std::lock_guard lock(mutex);
                if (shutdownRequested)
                {
                    return;
                }
                windows.push(std::move(samples), finalizedAt, true);
            }
            inferenceCondition.notify_one();
        }

        // Transcribes one bounded window and publishes a final Timmy result.
        void infer(
            WhisperAsrBackend& owner,
            const std::vector<float>& samples, AttemptGeneration jobGeneration,
            std::optional<std::chrono::steady_clock::time_point> finalizedAt)
        {
            auto parameters = detail::makeWhisperCommandParameters(tuning);
            parameters.language = preferredLanguage.empty()
                ? "auto"
                : preferredLanguage.c_str();
            parameters.detect_language = preferredLanguage.empty();
            parameters.abort_callback =
                detail::WhisperInferenceCancellation::abortCallback;
            parameters.abort_callback_user_data = &inferenceCancellation;
            auto grammarRules = grammar.c_rules();
            detail::applyWhisperRecognitionGuidance(parameters, guidance, grammar, grammarRules);

            std::optional<double> inferenceSeconds;
            bool admitted = false;
            const auto outcome = inferenceCancellation.run([&] {
                if (samples.size() > static_cast<std::size_t>(
                        std::numeric_limits<int>::max()))
                {
                    return -1;
                }
                admitted = owner.publishInferenceInput({jobGeneration, ++inferenceIndex, samples});
                if (!admitted) return 0; // Attempt closed before this queued job began.
                {
                    std::lock_guard lock(mutex);
                    guidanceDiagnostics = detail::whisperGuidanceDiagnostics(tuning, parameters);
                }
                progress(owner, RecognitionProgressState::inferenceStarted, jobGeneration);
                const auto started = std::chrono::steady_clock::now();
                const int result = whisper_full(context, parameters, samples.data(),
                    static_cast<int>(samples.size()));
                inferenceSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started).count();
                return result;
            });
            if (!admitted || outcome == detail::WhisperInferenceOutcome::cancelled)
            {
                return;
            }
            if (outcome == detail::WhisperInferenceOutcome::failed)
            {
                owner.publishDiagnostic({
                    AsrDiagnosticCode::recognitionFailure,
                    "Whisper inference failed for the queued audio segment."
                });
                return;
            }

            std::string transcript;
            const int segmentCount = whisper_full_n_segments(context);
            for (int index = 0; index < segmentCount; ++index)
            {
                if (const char* text =
                    whisper_full_get_segment_text(context, index))
                {
                    transcript += text;
                }
            }

            transcript = trimTranscript(std::move(transcript));

            std::optional<std::string> detectedLanguage;
            const int languageId = whisper_full_lang_id(context);
            if (const char* language = whisper_lang_str(languageId))
            {
                detectedLanguage = language;
            }

            if (inferenceCancellation.stopRequested())
            {
                return;
            }

            const auto completedAt = std::chrono::steady_clock::now();
            const auto audioSeconds = static_cast<double>(samples.size()) / 16000.0;
            const auto endToResult = finalizedAt
                ? std::optional<double>{std::chrono::duration<double>(completedAt - *finalizedAt).count()}
                : std::nullopt;
            {
                std::lock_guard lock(mutex);
                timings.inferenceDurationSeconds = inferenceSeconds;
                timings.inferenceAudioDurationSeconds = audioSeconds;
                timings.realTimeFactor = inferenceSeconds && audioSeconds > 0
                    ? std::optional<double>{*inferenceSeconds / audioSeconds} : std::nullopt;
                timings.endOfUtteranceToResultSeconds = endToResult;
            }
            progress(owner, transcript.empty() ? RecognitionProgressState::inferenceCompletedNoText
                : RecognitionProgressState::inferenceCompletedWithText, jobGeneration);
            // Empty successful inference is observable, but remains semantically
            // ignored in ordinary recognition.
            if (transcript.empty() && jobGeneration == 0) return;
            owner.publishResult({
                std::move(transcript),
                AsrResultKind::final,
                std::move(detectedLanguage),
                {audioSeconds, inferenceSeconds, endToResult, finalizedAt},
                jobGeneration
            });
        }

        void progress(WhisperAsrBackend& owner, RecognitionProgressState stage,
            AttemptGeneration jobGeneration,
            std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now())
        {
            RecognitionProgress event{stage, jobGeneration, timestamp};
            {
                std::lock_guard lock(mutex);
                event.droppedVadFrames = vadFrames.dropped();
                event.droppedInferenceJobs = windows.dropped();
            }
            owner.publishProgress(event);
        }

        void startWorkers(WhisperAsrBackend& owner)
        {
            {
                std::lock_guard lock(mutex);
                shutdownRequested = false; // Previous workers have joined; audio is gated.
            }
            inferenceCancellation.reset();
            inferenceWorker = std::thread([this, &owner] { runInferenceWorker(owner); });
            if (vadContext) vadWorker = std::thread([this, &owner] { runVadWorker(owner); });
        }

        // Joins backend-owned workers without holding their shared queue lock.
        void stopWorkers()
        {
            inferenceCancellation.requestStop();
            {
                std::lock_guard lock(mutex);
                shutdownRequested = true;
                windows.clear();
                vadFrames.clear();
                accumulation.clear();
                vadAccumulation.clear();
            }
            inferenceCondition.notify_all();
            vadCondition.notify_all();
            if (vadWorker.joinable())
            {
                vadWorker.join();
            }
            if (inferenceWorker.joinable())
            {
                inferenceWorker.join();
            }
            endpoint.reset();
        }

        // Frees the private Whisper and Silero implementation objects.
        void destroyContexts()
        {
            if (vadContext != nullptr)
            {
                whisper_vad_free(vadContext);
                vadContext = nullptr;
            }
            if (context != nullptr)
            {
                whisper_free(context);
                context = nullptr;
            }
        }

        WhisperAsrBackendOptions options;
        const detail::WhisperConfiguration tuning;
        std::atomic<AsrState> lifecycle{ AsrState::stopped };
        detail::WhisperInferenceCancellation inferenceCancellation;
        whisper_context* context = nullptr;
        whisper_vad_context* vadContext = nullptr;
        detail::WhisperEndpointDetector endpoint;
        detail::WhisperRecognitionGuidance guidance;
        grammar_parser::parse_state grammar;
        SettingValues guidanceDiagnostics;
        ComputeDiagnostics computeDiagnostics;
        std::string preferredLanguage;
        std::mutex lifecycleMutex;
        std::mutex mutex;
        std::condition_variable inferenceCondition;
        std::condition_variable vadCondition;
        std::vector<float> accumulation;
        std::vector<float> vadAccumulation;
        detail::DiagnosticQueue<std::vector<float>> windows{maximumQueuedWindows};
        detail::DiagnosticQueue<std::vector<float>> vadFrames{maximumQueuedVadFrames};
        AttemptDiagnostics timings{};
        std::uint64_t inferenceIndex = 0; // Inference worker only; reset with workers joined.
        AttemptGeneration generation = 0; // Changed only with workers joined/audio gated.
        bool shutdownRequested = false;
        std::thread inferenceWorker;
        std::thread vadWorker;
    };

    // Creates a backend with no model or worker resources allocated.
    WhisperAsrBackend::WhisperAsrBackend(WhisperAsrBackendOptions options)
        : impl_(std::make_unique<Impl>(std::move(options)))
    {
        configureWhisperLogging();
    }

    // Stops recognition before releasing private backend resources.
    WhisperAsrBackend::~WhisperAsrBackend()
    {
        stop();
    }

    // Reports the generic features supported by this first Whisper backend.
    AsrCapabilities WhisperAsrBackend::capabilities() const
    {
        return { false, true, true, true };
    }

    // Returns the current backend lifecycle state.
    AsrState WhisperAsrBackend::state() const
    {
        return impl_->lifecycle.load();
    }

    // Loads a local model and starts the backend-owned inference worker.
    AsrOperationResult WhisperAsrBackend::start(
        const AsrConfiguration& configuration)
    {
        std::lock_guard lifecycleLock(impl_->lifecycleMutex);
        if (state() == AsrState::running)
        {
            return failure(
                AsrDiagnosticCode::invalidConfiguration,
                "Whisper ASR is already running.");
        }

        impl_->stopWorkers();
        impl_->destroyContexts();
        impl_->lifecycle = AsrState::starting;
        if ((configuration.compute.deviceIndex && *configuration.compute.deviceIndex < 0)
            || (configuration.compute.backend != ComputeBackend::automatic
                && configuration.compute.backend != ComputeBackend::cpu
                && configuration.compute.backend != ComputeBackend::vulkan))
        {
            impl_->lifecycle = AsrState::failed;
            return failure(AsrDiagnosticCode::invalidConfiguration, "Invalid compute selection.");
        }
        {
            std::lock_guard lock(impl_->mutex);
            impl_->computeDiagnostics = {};
            impl_->computeDiagnostics.requested = configuration.compute;
            impl_->guidanceDiagnostics = detail::whisperGuidanceDiagnostics(
                impl_->tuning, detail::makeWhisperCommandParameters(impl_->tuning));
        }

        impl_->guidance = detail::configuredWhisperGuidance(
            configuration.recognitionHints, impl_->tuning);
        if (!detail::isValidWhisperRecognitionGuidance(impl_->guidance))
        {
            impl_->lifecycle = AsrState::failed;
            return failure(
                AsrDiagnosticCode::invalidConfiguration,
                "Whisper recognition guidance is invalid.");
        }
        impl_->grammar = impl_->guidance.grammar.empty()
            ? grammar_parser::parse_state{}
            : grammar_parser::parse(impl_->guidance.grammar.c_str());

        std::error_code filesystemError;
        const bool regularModelFile =
            !configuration.modelResourcePath.empty()
            && std::filesystem::is_regular_file(
                configuration.modelResourcePath,
                filesystemError);
        if (!regularModelFile || filesystemError)
        {
            impl_->lifecycle = AsrState::failed;
            return failure(
                AsrDiagnosticCode::unavailableResource,
                "Whisper model is missing or unavailable: "
                    + configuration.modelResourcePath.string());
        }

        if (!impl_->options.vadModelPath.empty())
        {
            filesystemError.clear();
            const bool regularVadFile = std::filesystem::is_regular_file(
                impl_->options.vadModelPath, filesystemError);
            if (!regularVadFile || filesystemError)
            {
                impl_->lifecycle = AsrState::failed;
                return failure(
                    AsrDiagnosticCode::unavailableResource,
                    "Silero VAD model is missing or unavailable: "
                        + impl_->options.vadModelPath.string());
            }
        }

        const std::string modelPath =
            configuration.modelResourcePath.string();
        const auto discovery = detail::discoverWhisperCompute(
            configuration.compute.backend != ComputeBackend::cpu);
        const auto compute = initializeCompute(configuration.compute, discovery,
            [&](const ComputeDevice& device) {
                auto parameters = whisper_context_default_params();
                parameters.use_gpu = device.backend == ComputeBackend::vulkan;
                if (parameters.use_gpu) parameters.gpu_device = device.index;
                // The pinned overlay frees partial model/state resources and
                // returns null on a failed GPU attempt, before CPU is retried.
                impl_->context = whisper_init_from_file_with_params(modelPath.c_str(), parameters);
                return impl_->context != nullptr;
            });
        {
            std::lock_guard lock(impl_->mutex);
            impl_->computeDiagnostics = compute;
        }
        if (impl_->context == nullptr)
        {
            impl_->lifecycle = AsrState::failed;
            return failure(
                AsrDiagnosticCode::initializationFailure,
                "Whisper could not initialize the configured model: " + compute.initializationFailure);
        }

        if (!impl_->options.vadModelPath.empty())
        {
            auto vadParameters = whisper_vad_default_context_params();
            vadParameters.use_gpu = false;
            const std::string vadPath = impl_->options.vadModelPath.string();
            impl_->vadContext = whisper_vad_init_from_file_with_params(
                vadPath.c_str(), vadParameters);
            if (impl_->vadContext == nullptr)
            {
                impl_->destroyContexts();
                impl_->lifecycle = AsrState::failed;
                return failure(
                    AsrDiagnosticCode::initializationFailure,
                    "Silero VAD could not initialize the configured model.");
            }
        }

        impl_->preferredLanguage =
            configuration.preferredLanguage.value_or("");
        {
            std::lock_guard queueLock(impl_->mutex);
            impl_->shutdownRequested = false;
            impl_->accumulation.clear();
            impl_->vadAccumulation.clear();
            impl_->windows.clear();
            impl_->vadFrames.clear();
        }
        impl_->endpoint.reset();
        impl_->inferenceCancellation.reset();

        try
        {
            {
                std::lock_guard lock(impl_->mutex);
                impl_->generation = 0;
                impl_->inferenceIndex = 0;
                impl_->vadFrames.reset();
                impl_->windows.reset();
                impl_->timings = {};
            }
            impl_->startWorkers(*this);
        }
        catch (const std::system_error& error)
        {
            impl_->stopWorkers();
            impl_->destroyContexts();
            impl_->lifecycle = AsrState::failed;
            return failure(
                AsrDiagnosticCode::initializationFailure,
                "Whisper worker could not start: "
                    + std::string(error.what()));
        }

        impl_->lifecycle = AsrState::running;
        impl_->progress(*this, RecognitionProgressState::backendReady, 0);
        return { true, std::nullopt };
    }

    AttemptDiagnostics WhisperAsrBackend::attemptDiagnostics() const
    {
        std::lock_guard lock(impl_->mutex);
        AttemptDiagnostics diagnostics = impl_->timings;
        // Capture accounting belongs to AudioEngine; this backend cannot count lost blocks.
        diagnostics.droppedVadFrames = impl_->vadFrames.dropped();
        diagnostics.droppedInferenceJobs = impl_->windows.dropped();
        diagnostics.vadQueueDepth = impl_->vadFrames.size();
        diagnostics.vadQueueMaxDepth = impl_->vadFrames.maximum();
        diagnostics.inferenceQueueDepth = impl_->windows.size();
        diagnostics.inferenceQueueMaxDepth = impl_->windows.maximum();
        diagnostics.oldestVadQueueAgeSeconds = impl_->vadFrames.oldestAge();
        diagnostics.vadQueuedAudioDurationSeconds = static_cast<double>(impl_->vadFrames.size() * vadFrameSampleCount) / 16000.0;
        diagnostics.vadQueueWaitTotalSeconds = impl_->vadFrames.waitTotal();
        diagnostics.vadQueueWaitMaxSeconds = impl_->vadFrames.waitMax();
        diagnostics.inferenceQueueWaitTotalSeconds = impl_->windows.waitTotal();
        diagnostics.inferenceQueueWaitMaxSeconds = impl_->windows.waitMax();
        diagnostics.recognitionGuidance = impl_->guidanceDiagnostics;
        diagnostics.compute = impl_->computeDiagnostics;
        return diagnostics;
    }

    AsrOperationResult WhisperAsrBackend::resetAttempt(AttemptGeneration generation)
    {
        std::lock_guard lifecycleLock(impl_->lifecycleMutex);
        if (state() != AsrState::running || generation == 0)
            return failure(AsrDiagnosticCode::invalidConfiguration, "Cannot reset an inactive attempt.");
        // Cooperative cancellation and joins serialize all access to the private
        // Whisper/Silero contexts. Neither model is freed or reloaded here.
        impl_->stopWorkers();
        if (impl_->vadContext) whisper_vad_reset_state(impl_->vadContext);
        {
            std::lock_guard lock(impl_->mutex);
            impl_->generation = generation;
            impl_->inferenceIndex = 0;
            impl_->vadFrames.reset();
            impl_->windows.reset();
            impl_->timings = {};
            impl_->guidanceDiagnostics = detail::whisperGuidanceDiagnostics(
                impl_->tuning, detail::makeWhisperCommandParameters(impl_->tuning));
        }
        try { impl_->startWorkers(*this); }
        catch (const std::system_error& error)
        {
            impl_->stopWorkers();
            impl_->lifecycle = AsrState::failed;
            return failure(AsrDiagnosticCode::initializationFailure, error.what());
        }
        impl_->progress(*this, RecognitionProgressState::backendReady, generation);
        return {true, std::nullopt};
    }

    // Stops and joins inference before releasing the model context.
    AsrOperationResult WhisperAsrBackend::stop()
    {
        std::lock_guard lifecycleLock(impl_->lifecycleMutex);
        if (state() == AsrState::stopped)
        {
            return { true, std::nullopt };
        }

        impl_->lifecycle = AsrState::stopping;
        impl_->stopWorkers();
        impl_->destroyContexts();
        impl_->preferredLanguage.clear();
        impl_->guidance = {};
        impl_->grammar = {};
        {
            std::lock_guard lock(impl_->mutex);
            impl_->guidanceDiagnostics.clear();
        }
        impl_->lifecycle = AsrState::stopped;
        return { true, std::nullopt };
    }

    AsrOperationResult WhisperAsrBackend::submitInferenceInput(std::span<const float> samples)
    {
        if (samples.empty() || samples.size() > 16000U * 600U
            || std::any_of(samples.begin(), samples.end(), [](float s) { return !std::isfinite(s); }))
            return failure(AsrDiagnosticCode::invalidAudioFormat,
                "Replay requires finite mono 16 kHz PCM of at most ten minutes.");
        {
            std::lock_guard lock(impl_->mutex);
            if (impl_->shutdownRequested || impl_->lifecycle != AsrState::running)
                return failure(AsrDiagnosticCode::invalidConfiguration, "Replay backend is not running.");
            if (impl_->windows.size() >= maximumQueuedWindows)
                return failure(AsrDiagnosticCode::recognitionFailure, "Replay inference queue is full.");
            impl_->windows.push(std::vector<float>{samples.begin(), samples.end()});
        }
        impl_->inferenceCondition.notify_one();
        return {true, std::nullopt};
    }

    // Copies validated PCM into the fixed-window or VAD endpointing path.
    void WhisperAsrBackend::processAudioBlock(
        const audio::AudioBlockView& block)
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->shutdownRequested
            || impl_->lifecycle.load() != AsrState::running)
        {
            return;
        }
        std::vector<float>& accumulation = impl_->vadContext != nullptr
            ? impl_->vadAccumulation
            : impl_->accumulation;
        const std::size_t targetSampleCount = impl_->vadContext != nullptr
            ? vadFrameSampleCount
            : windowSampleCount;

        std::size_t offset = 0;
        while (offset < block.frameCount)
        {
            const std::size_t available =
                targetSampleCount - accumulation.size();
            const std::size_t copyCount =
                std::min(available, block.frameCount - offset);
            accumulation.insert(
                accumulation.end(),
                block.interleavedSamples.begin()
                    + static_cast<std::ptrdiff_t>(offset),
                block.interleavedSamples.begin()
                    + static_cast<std::ptrdiff_t>(offset + copyCount));
            offset += copyCount;

            if (accumulation.size() != targetSampleCount)
            {
                continue;
            }

            if (impl_->vadContext != nullptr)
            {
                impl_->vadFrames.push(std::move(accumulation));
                accumulation.clear();
                accumulation.reserve(vadFrameSampleCount);
                impl_->vadCondition.notify_one();
            }
            else
            {
                impl_->windows.push(std::move(accumulation));
                accumulation.clear();
                accumulation.reserve(windowSampleCount);
                impl_->inferenceCondition.notify_one();
            }
        }
    }

} // namespace timmy::asr
