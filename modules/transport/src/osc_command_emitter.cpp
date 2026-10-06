/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/transport/osc_command_emitter.h"

#include "timmy/transport/osc_packet.h"

#include <limits>
#include <thread>
#include <exception>
#include <string>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace timmy::transport {

    namespace {

#ifdef _WIN32
        using SocketHandle = SOCKET;
        constexpr SocketHandle invalidSocket = INVALID_SOCKET;

        // Closes a Windows socket handle.
        void closeSocket(SocketHandle socket)
        {
            closesocket(socket);
        }
#else
        using SocketHandle = int;
        constexpr SocketHandle invalidSocket = -1;

        // Closes a POSIX socket handle.
        void closeSocket(SocketHandle socket)
        {
            close(socket);
        }
#endif

        class SystemUdpSender final : public IUdpSender
        {
        public:
            SystemUdpSender();
            ~SystemUdpSender() override;

            [[nodiscard]] EmissionResult send(
                const OscDestination& destination,
                std::span<const std::uint8_t> payload) override;

        private:
            bool initialized_ = true;
        };

        // Initializes the platform socket subsystem owned by this sender.
        SystemUdpSender::SystemUdpSender()
        {
#ifdef _WIN32
            WSADATA data{};
            initialized_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
        }

        // Releases platform socket state after all sends have completed.
        SystemUdpSender::~SystemUdpSender()
        {
#ifdef _WIN32
            if (initialized_)
            {
                WSACleanup();
            }
#endif
        }

        // Resolves, connects, and sends exactly one UDP datagram.
        EmissionResult SystemUdpSender::send(
            const OscDestination& destination,
            std::span<const std::uint8_t> payload)
        {
            if (!initialized_)
            {
                return {EmissionState::failed,
                    "UDP networking could not be initialized."};
            }

            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_protocol = IPPROTO_UDP;
            addrinfo* addresses = nullptr;
            const std::string port = std::to_string(destination.port);
            if (getaddrinfo(destination.host.c_str(), port.c_str(),
                    &hints, &addresses) != 0)
            {
                return {EmissionState::failed,
                    "OSC destination host could not be resolved."};
            }

            SocketHandle socket = invalidSocket;
            for (auto* address = addresses; address != nullptr;
                 address = address->ai_next)
            {
                const auto candidate = ::socket(address->ai_family,
                    address->ai_socktype, address->ai_protocol);
                if (candidate == invalidSocket)
                {
                    continue;
                }
                if (::connect(candidate, address->ai_addr,
                        static_cast<int>(address->ai_addrlen)) == 0)
                {
                    socket = candidate;
                    break;
                }
                closeSocket(candidate);
            }
            freeaddrinfo(addresses);

            if (socket == invalidSocket)
            {
                return {EmissionState::failed,
                    "OSC destination could not be opened for UDP delivery."};
            }

#ifdef _WIN32
            const auto sent = ::send(socket,
                reinterpret_cast<const char*>(payload.data()),
                static_cast<int>(payload.size()), 0);
#else
            const auto sent = ::send(socket, payload.data(), payload.size(), 0);
#endif
            closeSocket(socket);
            if (sent < 0 || static_cast<std::size_t>(sent) != payload.size())
            {
                return {EmissionState::failed,
                    "OSC UDP datagram could not be sent."};
            }
            return {EmissionState::succeeded, {}};
        }

        // Creates the production UDP sender hidden behind the emitter boundary.
        std::unique_ptr<IUdpSender> makeSystemUdpSender()
        {
            return std::make_unique<SystemUdpSender>();
        }

    } // namespace

    // Allows injected UDP senders to be destroyed through their contract.
    IUdpSender::~IUdpSender() = default;

    // Creates an OSC emitter with an internally owned system UDP sender.
    OscCommandEmitter::OscCommandEmitter(OscDestination destination,
        std::shared_ptr<const IOscCommandProfile> profile)
        : OscCommandEmitter(std::move(destination), std::move(profile),
            makeSystemUdpSender())
    {
    }

    // Creates an OSC emitter with an owned sender for deterministic testing.
    OscCommandEmitter::OscCommandEmitter(OscDestination destination,
        std::shared_ptr<const IOscCommandProfile> profile,
        std::unique_ptr<IUdpSender> sender,
        std::function<void(std::chrono::milliseconds)> wait)
        : wait_(wait ? std::move(wait) : [](std::chrono::milliseconds duration) {
              std::this_thread::sleep_for(duration);
          }),
          destination_(std::move(destination)),
          profile_(std::move(profile)),
          sender_(std::move(sender))
    {
    }

    // Destroys the owned UDP sender while its platform state is still valid.
    OscCommandEmitter::~OscCommandEmitter() = default;

    // Validates the whole plan before I/O and executes it without interleaving.
    EmissionResult OscCommandEmitter::emit(
        const command::SemanticCommand& commandValue)
    {
        const std::lock_guard lock(executionMutex_);
        if (destination_.host.empty())
        {
            return {EmissionState::failed,
                "OSC destination host must not be empty."};
        }
        if (destination_.port == 0
            || destination_.port > std::numeric_limits<std::uint16_t>::max())
        {
            return {EmissionState::failed,
                "OSC destination port must be between 1 and 65535."};
        }
        if (!profile_)
        {
            return {EmissionState::failed,
                "OSC command profile is not configured."};
        }
        if (!sender_)
        {
            return {EmissionState::failed,
                "UDP sender is not available."};
        }

        const auto mapped = profile_->map(commandValue);
        if (!mapped.plan || mapped.plan->empty())
        {
            return {EmissionState::failed, mapped.diagnostic.empty()
                ? "The OSC profile does not support this semantic command."
                : mapped.diagnostic};
        }
        struct PreparedStep
        {
            std::vector<std::uint8_t> bytes;
            std::optional<std::vector<std::uint8_t>> release;
            std::chrono::milliseconds hold{0};
            std::chrono::milliseconds gap{0};
        };
        std::vector<PreparedStep> prepared;
        for (const auto& step : *mapped.plan)
        {
            PreparedStep next;
            OscMessage message;
            if (const auto* pulse = std::get_if<OscKeyPulse>(&step))
            {
                if (pulse->hold.count() < 0 || pulse->gap.count() < 0)
                    return {EmissionState::failed, "OSC key timing must be nonnegative."};
                message = {pulse->address, {std::int32_t{1}}};
                auto release = serializeOscMessage({pulse->address, {std::int32_t{0}}});
                if (!release.bytes) return {EmissionState::failed, release.diagnostic};
                next.release = std::move(*release.bytes);
                next.hold = pulse->hold;
                next.gap = pulse->gap;
            }
            else message = std::get<OscMessage>(step);
            auto packet = serializeOscMessage(message);
            if (!packet.bytes) return {EmissionState::failed, packet.diagnostic};
            next.bytes = std::move(*packet.bytes);
            prepared.push_back(std::move(next));
        }
        for (const auto& step : prepared)
        {
            // Mark a pulse before attempting delivery: a failing sender may have
            // delivered the datagram. Cleanup only releases, never retries presses.
            bool needsRelease = step.release.has_value();
            auto cleanup = [&] {
                if (needsRelease)
                {
                    try { (void)sender_->send(destination_, *step.release); }
                    catch (...) { /* Preserve the original failure. */ }
                }
            };
            try
            {
                auto result = sender_->send(destination_, step.bytes);
                if (result.state == EmissionState::failed)
                {
                    cleanup();
                    return result;
                }
                if (step.release)
                {
                    wait_(step.hold);
                    result = sender_->send(destination_, *step.release);
                    if (result.state == EmissionState::failed)
                    {
                        cleanup();
                        return result;
                    }
                    needsRelease = false;
                    wait_(step.gap);
                }
            }
            catch (const std::exception& error)
            {
                cleanup();
                return {EmissionState::failed, error.what()};
            }
            catch (...)
            {
                cleanup();
                return {EmissionState::failed, "OSC execution failed with an unknown exception."};
            }
        }
        return {EmissionState::succeeded, {}};
    }

} // namespace timmy::transport
