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

#include <whisper.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
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

    class WhisperAsrBackend::Impl
    {
    public:
        // Releases the whisper context after the inference worker has stopped.
        ~Impl()
        {
            stopWorker();
            destroyContext();
        }

        // Runs recognition jobs until shutdown is requested and the queue is empty.
        void runWorker(WhisperAsrBackend& owner)
        {
            while (true)
            {
                std::vector<float> samples;
                {
                    std::unique_lock lock(mutex);
                    condition.wait(lock, [this] {
                        return shutdownRequested || !windows.empty();
                    });

                    if (shutdownRequested && windows.empty())
                    {
                        break;
                    }

                    samples = std::move(windows.front());
                    windows.erase(windows.begin());
                }

                infer(owner, samples);
            }
        }

        // Transcribes one bounded window and publishes a final Timmy result.
        void infer(
            WhisperAsrBackend& owner,
            const std::vector<float>& samples)
        {
            auto parameters = whisper_full_default_params(
                WHISPER_SAMPLING_GREEDY);
            parameters.print_progress = false;
            parameters.print_realtime = false;
            parameters.print_timestamps = false;
            parameters.no_context = true;
            parameters.language = preferredLanguage.empty()
                ? "auto"
                : preferredLanguage.c_str();
            parameters.detect_language = preferredLanguage.empty();

            if (samples.size()
                > static_cast<std::size_t>(std::numeric_limits<int>::max())
                || whisper_full(
                    context,
                    parameters,
                    samples.data(),
                    static_cast<int>(samples.size())) != 0)
            {
                owner.publishDiagnostic({
                    AsrDiagnosticCode::recognitionFailure,
                    "Whisper inference failed for the queued audio window."
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
            if (transcript.empty())
            {
                return;
            }

            std::optional<std::string> detectedLanguage;
            const int languageId = whisper_full_lang_id(context);
            if (const char* language = whisper_lang_str(languageId))
            {
                detectedLanguage = language;
            }

            owner.publishResult({
                std::move(transcript),
                AsrResultKind::final,
                std::move(detectedLanguage)
            });
        }

        // Joins the backend-owned worker without holding its queue lock.
        void stopWorker()
        {
            {
                std::lock_guard lock(mutex);
                shutdownRequested = true;
                windows.clear();
                accumulation.clear();
            }
            condition.notify_all();
            if (worker.joinable())
            {
                worker.join();
            }
        }

        // Frees the private whisper implementation object.
        void destroyContext()
        {
            if (context != nullptr)
            {
                whisper_free(context);
                context = nullptr;
            }
        }

        std::atomic<AsrState> lifecycle{ AsrState::stopped };
        whisper_context* context = nullptr;
        std::string preferredLanguage;
        std::mutex lifecycleMutex;
        std::mutex mutex;
        std::condition_variable condition;
        std::vector<float> accumulation;
        std::vector<std::vector<float>> windows;
        bool shutdownRequested = false;
        std::thread worker;
    };

    // Creates a backend with no model or worker resources allocated.
    WhisperAsrBackend::WhisperAsrBackend()
        : impl_(std::make_unique<Impl>())
    {
    }

    // Stops recognition before releasing private backend resources.
    WhisperAsrBackend::~WhisperAsrBackend()
    {
        stop();
    }

    // Reports the generic features supported by this first Whisper backend.
    AsrCapabilities WhisperAsrBackend::capabilities() const
    {
        return { false, true, false, false };
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

        impl_->stopWorker();
        impl_->destroyContext();
        impl_->lifecycle = AsrState::starting;

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

        const std::string modelPath =
            configuration.modelResourcePath.string();
        auto contextParameters = whisper_context_default_params();
        contextParameters.use_gpu = false;
        impl_->context = whisper_init_from_file_with_params(
            modelPath.c_str(),
            contextParameters);
        if (impl_->context == nullptr)
        {
            impl_->lifecycle = AsrState::failed;
            return failure(
                AsrDiagnosticCode::initializationFailure,
                "Whisper could not initialize the configured model.");
        }

        impl_->preferredLanguage =
            configuration.preferredLanguage.value_or("");
        {
            std::lock_guard queueLock(impl_->mutex);
            impl_->shutdownRequested = false;
            impl_->accumulation.clear();
            impl_->windows.clear();
        }

        try
        {
            impl_->worker = std::thread(
                [this] { impl_->runWorker(*this); });
        }
        catch (const std::system_error& error)
        {
            impl_->destroyContext();
            impl_->lifecycle = AsrState::failed;
            return failure(
                AsrDiagnosticCode::initializationFailure,
                "Whisper inference worker could not start: "
                    + std::string(error.what()));
        }

        impl_->lifecycle = AsrState::running;
        return { true, std::nullopt };
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
        impl_->stopWorker();
        impl_->destroyContext();
        impl_->preferredLanguage.clear();
        impl_->lifecycle = AsrState::stopped;
        return { true, std::nullopt };
    }

    // Copies validated PCM into bounded backend-owned inference windows.
    void WhisperAsrBackend::processAudioBlock(
        const audio::AudioBlockView& block)
    {
        std::lock_guard lock(impl_->mutex);
        std::size_t offset = 0;
        while (offset < block.frameCount)
        {
            const std::size_t available =
                windowSampleCount - impl_->accumulation.size();
            const std::size_t copyCount =
                std::min(available, block.frameCount - offset);
            impl_->accumulation.insert(
                impl_->accumulation.end(),
                block.interleavedSamples.begin()
                    + static_cast<std::ptrdiff_t>(offset),
                block.interleavedSamples.begin()
                    + static_cast<std::ptrdiff_t>(offset + copyCount));
            offset += copyCount;

            if (impl_->accumulation.size() == windowSampleCount)
            {
                if (impl_->windows.size() == maximumQueuedWindows)
                {
                    impl_->windows.erase(impl_->windows.begin());
                }
                impl_->windows.push_back(std::move(impl_->accumulation));
                impl_->accumulation.clear();
                impl_->accumulation.reserve(windowSampleCount);
                impl_->condition.notify_one();
            }
        }
    }

} // namespace timmy::asr
