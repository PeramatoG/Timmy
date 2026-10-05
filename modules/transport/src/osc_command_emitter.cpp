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
        std::unique_ptr<IUdpSender> sender)
        : destination_(std::move(destination)),
          profile_(std::move(profile)),
          sender_(std::move(sender))
    {
    }

    // Destroys the owned UDP sender while its platform state is still valid.
    OscCommandEmitter::~OscCommandEmitter() = default;

    // Maps, serializes, and sends at most one OSC datagram for one command.
    EmissionResult OscCommandEmitter::emit(
        const command::SemanticCommand& commandValue)
    {
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
        if (!mapped.message)
        {
            return {EmissionState::failed, mapped.diagnostic.empty()
                ? "The OSC profile does not support this semantic command."
                : mapped.diagnostic};
        }
        const auto packet = serializeOscMessage(*mapped.message);
        if (!packet.bytes)
        {
            return {EmissionState::failed, packet.diagnostic};
        }
        return sender_->send(destination_, *packet.bytes);
    }

} // namespace timmy::transport
