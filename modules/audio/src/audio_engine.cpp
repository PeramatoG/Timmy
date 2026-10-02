/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/audio/audio_engine.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace timmy::audio {

    namespace {

        constexpr ma_uint32 kDispatchFrames = 512;
        constexpr ma_uint32 kRingBufferSeconds = 1;

        std::string makeMiniaudioError(
            std::string_view operation,
            ma_result result)
        {
            return std::string(operation)
                + " failed (miniaudio error "
                + std::to_string(static_cast<int>(result))
                + ")";
        }

    } // namespace

    class AudioEngine::Impl
    {
    public:
        ~Impl()
        {
            stop();
        }

        void addConsumer(std::shared_ptr<IAudioConsumer> consumer)
        {
            if (!consumer)
            {
                return;
            }

            std::lock_guard lock(consumersMutex_);

            const auto existing =
                std::find(consumers_.begin(), consumers_.end(), consumer);

            if (existing == consumers_.end())
            {
                consumers_.push_back(std::move(consumer));
            }
        }

        void removeConsumer(
            const std::shared_ptr<IAudioConsumer>& consumer)
        {
            std::lock_guard lock(consumersMutex_);

            consumers_.erase(
                std::remove(
                    consumers_.begin(),
                    consumers_.end(),
                    consumer),
                consumers_.end());
        }

        void clearConsumers()
        {
            std::lock_guard lock(consumersMutex_);
            consumers_.clear();
        }

        AudioStartResult start(const AudioCaptureConfig& config)
        {
            stop();

            AudioStartResult output;

            const ma_result contextResult =
                ma_context_init(
                    nullptr,
                    0,
                    nullptr,
                    &context_);

            if (contextResult != MA_SUCCESS)
            {
                output.errorMessage =
                    makeMiniaudioError(
                        "Audio context initialization",
                        contextResult);

                return output;
            }

            contextInitialized_ = true;

            const ma_device_id* selectedDeviceId = nullptr;

            ma_device_info* playbackDevices = nullptr;
            ma_uint32 playbackDeviceCount = 0;

            ma_device_info* captureDevices = nullptr;
            ma_uint32 captureDeviceCount = 0;

            if (config.runtimeDeviceIndex.has_value())
            {
                const ma_result enumerationResult =
                    ma_context_get_devices(
                        &context_,
                        &playbackDevices,
                        &playbackDeviceCount,
                        &captureDevices,
                        &captureDeviceCount);

                if (enumerationResult != MA_SUCCESS)
                {
                    output.errorMessage =
                        makeMiniaudioError(
                            "Audio device enumeration",
                            enumerationResult);

                    releaseResources();
                    return output;
                }

                const std::size_t requestedIndex =
                    *config.runtimeDeviceIndex;

                if (requestedIndex >= captureDeviceCount)
                {
                    output.errorMessage =
                        "The requested capture device index is no longer valid.";

                    releaseResources();
                    return output;
                }

                selectedDeviceId =
                    &captureDevices[requestedIndex].id;
            }

            ma_device_config deviceConfig =
                ma_device_config_init(ma_device_type_capture);

            deviceConfig.capture.pDeviceID =
                selectedDeviceId;

            // Timmy uses float PCM internally. Channel count and sample rate
            // remain native to the selected capture device.
            deviceConfig.capture.format =
                ma_format_f32;

            deviceConfig.capture.channels =
                0;

            deviceConfig.sampleRate =
                0;

            deviceConfig.dataCallback =
                &Impl::dataCallback;

            deviceConfig.pUserData =
                this;

            const ma_result deviceResult =
                ma_device_init(
                    &context_,
                    &deviceConfig,
                    &device_);

            if (deviceResult != MA_SUCCESS)
            {
                output.errorMessage =
                    makeMiniaudioError(
                        "Audio capture device initialization",
                        deviceResult);

                releaseResources();
                return output;
            }

            deviceInitialized_ = true;

            format_.sampleRate =
                device_.sampleRate;

            format_.channelCount =
                device_.capture.channels;

            if (format_.sampleRate == 0
                || format_.channelCount == 0)
            {
                output.errorMessage =
                    "The capture device returned an invalid stream format.";

                releaseResources();
                return output;
            }

            const ma_uint32 ringBufferCapacityFrames =
                std::max(
                    static_cast<ma_uint32>(format_.sampleRate)
                    * kRingBufferSeconds,
                    kDispatchFrames * 2);

            const ma_result ringBufferResult =
                ma_pcm_rb_init(
                    ma_format_f32,
                    format_.channelCount,
                    ringBufferCapacityFrames,
                    nullptr,
                    nullptr,
                    &ringBuffer_);

            if (ringBufferResult != MA_SUCCESS)
            {
                output.errorMessage =
                    makeMiniaudioError(
                        "Audio ring buffer initialization",
                        ringBufferResult);

                releaseResources();
                return output;
            }

            ringBufferInitialized_ = true;
            droppedFrameCount_.store(0, std::memory_order_relaxed);

            running_.store(true, std::memory_order_release);

            try
            {
                workerThread_ =
                    std::thread(&Impl::workerLoop, this);
            }
            catch (const std::exception& exception)
            {
                running_.store(false, std::memory_order_release);

                output.errorMessage =
                    std::string("Audio worker thread initialization failed: ")
                    + exception.what();

                releaseResources();
                return output;
            }

            const ma_result startResult =
                ma_device_start(&device_);

            if (startResult != MA_SUCCESS)
            {
                running_.store(false, std::memory_order_release);

                if (workerThread_.joinable())
                {
                    workerThread_.join();
                }

                output.errorMessage =
                    makeMiniaudioError(
                        "Audio capture start",
                        startResult);

                releaseResources();
                return output;
            }

            deviceStarted_ = true;

            output.format = format_;
            return output;
        }

        void stop() noexcept
        {
            if (deviceStarted_)
            {
                ma_device_stop(&device_);
                deviceStarted_ = false;
            }

            running_.store(false, std::memory_order_release);

            if (workerThread_.joinable())
            {
                workerThread_.join();
            }

            releaseResources();
        }

        [[nodiscard]]
        bool isRunning() const noexcept
        {
            return running_.load(std::memory_order_acquire);
        }

    private:
        static void dataCallback(
            ma_device* device,
            void*,
            const void* input,
            ma_uint32 frameCount)
        {
            if (device == nullptr
                || device->pUserData == nullptr
                || input == nullptr
                || frameCount == 0)
            {
                return;
            }

            auto* self =
                static_cast<Impl*>(device->pUserData);

            self->writeCapturedFrames(
                static_cast<const float*>(input),
                frameCount);
        }

        void writeCapturedFrames(
            const float* samples,
            ma_uint32 frameCount) noexcept
        {
            if (!ringBufferInitialized_
                || format_.channelCount == 0)
            {
                return;
            }

            const float* readPosition = samples;
            ma_uint32 remainingFrames = frameCount;

            while (remainingFrames > 0)
            {
                ma_uint32 framesToWrite =
                    remainingFrames;

                void* writeBuffer = nullptr;

                const ma_result acquireResult =
                    ma_pcm_rb_acquire_write(
                        &ringBuffer_,
                        &framesToWrite,
                        &writeBuffer);

                if (acquireResult != MA_SUCCESS
                    || framesToWrite == 0
                    || writeBuffer == nullptr)
                {
                    droppedFrameCount_.fetch_add(
                        remainingFrames,
                        std::memory_order_relaxed);

                    return;
                }

                const std::size_t sampleCount =
                    static_cast<std::size_t>(framesToWrite)
                    * format_.channelCount;

                std::memcpy(
                    writeBuffer,
                    readPosition,
                    sampleCount * sizeof(float));

                const ma_result commitResult =
                    ma_pcm_rb_commit_write(
                        &ringBuffer_,
                        framesToWrite);

                if (commitResult != MA_SUCCESS)
                {
                    droppedFrameCount_.fetch_add(
                        remainingFrames,
                        std::memory_order_relaxed);

                    return;
                }

                readPosition += sampleCount;
                remainingFrames -= framesToWrite;
            }
        }

        void workerLoop()
        {
            const std::size_t maximumSampleCount =
                static_cast<std::size_t>(kDispatchFrames)
                * format_.channelCount;

            std::vector<float> scratchBuffer(
                maximumSampleCount);

            while (running_.load(std::memory_order_acquire))
            {
                ma_uint32 framesToRead =
                    kDispatchFrames;

                void* readBuffer = nullptr;

                const ma_result acquireResult =
                    ma_pcm_rb_acquire_read(
                        &ringBuffer_,
                        &framesToRead,
                        &readBuffer);

                if (acquireResult != MA_SUCCESS
                    || framesToRead == 0
                    || readBuffer == nullptr)
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(2));

                    continue;
                }

                const std::size_t sampleCount =
                    static_cast<std::size_t>(framesToRead)
                    * format_.channelCount;

                std::memcpy(
                    scratchBuffer.data(),
                    readBuffer,
                    sampleCount * sizeof(float));

                const ma_result commitResult =
                    ma_pcm_rb_commit_read(
                        &ringBuffer_,
                        framesToRead);

                if (commitResult != MA_SUCCESS)
                {
                    continue;
                }

                std::vector<std::shared_ptr<IAudioConsumer>>
                    consumersSnapshot;

                {
                    std::lock_guard lock(consumersMutex_);
                    consumersSnapshot = consumers_;
                }

                const AudioBlockView block{
                    std::span<const float>(
                        scratchBuffer.data(),
                        sampleCount),
                    framesToRead,
                    format_
                };

                for (const auto& consumer : consumersSnapshot)
                {
                    if (consumer)
                    {
                        consumer->onAudioBlock(block);
                    }
                }
            }
        }

        void releaseResources() noexcept
        {
            if (ringBufferInitialized_)
            {
                ma_pcm_rb_uninit(&ringBuffer_);
                ringBufferInitialized_ = false;
            }

            if (deviceInitialized_)
            {
                ma_device_uninit(&device_);
                deviceInitialized_ = false;
            }

            if (contextInitialized_)
            {
                ma_context_uninit(&context_);
                contextInitialized_ = false;
            }

            format_ = {};
        }

        ma_context context_{};
        ma_device device_{};
        ma_pcm_rb ringBuffer_{};

        bool contextInitialized_ = false;
        bool deviceInitialized_ = false;
        bool deviceStarted_ = false;
        bool ringBufferInitialized_ = false;

        AudioStreamFormat format_{};

        std::atomic<bool> running_{ false };
        std::atomic<std::uint64_t> droppedFrameCount_{ 0 };

        std::thread workerThread_;

        std::mutex consumersMutex_;
        std::vector<std::shared_ptr<IAudioConsumer>> consumers_;
    };

    AudioEngine::AudioEngine()
        : impl_(std::make_unique<Impl>())
    {}

    AudioEngine::~AudioEngine() = default;

    AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;

    AudioEngine&
        AudioEngine::operator=(AudioEngine&&) noexcept = default;

    void AudioEngine::addConsumer(
        std::shared_ptr<IAudioConsumer> consumer)
    {
        if (!impl_)
        {
            impl_ = std::make_unique<Impl>();
        }

        impl_->addConsumer(std::move(consumer));
    }

    void AudioEngine::removeConsumer(
        const std::shared_ptr<IAudioConsumer>& consumer)
    {
        if (impl_)
        {
            impl_->removeConsumer(consumer);
        }
    }

    void AudioEngine::clearConsumers()
    {
        if (impl_)
        {
            impl_->clearConsumers();
        }
    }

    AudioStartResult
        AudioEngine::start(const AudioCaptureConfig& config)
    {
        if (!impl_)
        {
            impl_ = std::make_unique<Impl>();
        }

        return impl_->start(config);
    }

    void AudioEngine::stop() noexcept
    {
        if (impl_)
        {
            impl_->stop();
        }
    }

    bool AudioEngine::isRunning() const noexcept
    {
        return impl_ && impl_->isRunning();
    }

} // namespace timmy::audio