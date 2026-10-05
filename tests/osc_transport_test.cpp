#include "timmy/transport/osc_command_emitter.h"
#include "timmy/transport/osc_packet.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

    // Terminates the test immediately when a deterministic expectation fails.
    void require(bool condition)
    {
        if (!condition) std::abort();
    }

    // Creates a valid intensity command for one target and percentage.
    timmy::command::SemanticCommand setIntensity(
        timmy::command::LightingTarget target, int percent)
    {
        const auto level = timmy::command::IntensityLevel::fromPercent(percent);
        require(level.has_value());
        return timmy::command::SetIntensity{std::move(target), *level};
    }

    // Requires one argument to equal an exact signed integer.
    void requireInteger(const timmy::transport::OscArgument& argument,
        std::int32_t expected)
    {
        require(std::holds_alternative<std::int32_t>(argument));
        require(std::get<std::int32_t>(argument) == expected);
    }

    class RecordingSender final : public timmy::transport::IUdpSender
    {
    public:
        // Records one requested datagram and returns the configured outcome.
        timmy::transport::EmissionResult send(
            const timmy::transport::OscDestination& destinationValue,
            std::span<const std::uint8_t> payload) override
        {
            ++sendCount;
            destination = destinationValue;
            bytes.assign(payload.begin(), payload.end());
            return result;
        }

        int sendCount = 0;
        timmy::transport::OscDestination destination;
        std::vector<std::uint8_t> bytes;
        timmy::transport::EmissionResult result{
            timmy::transport::EmissionState::succeeded, {}};
    };

    class RejectingProfile final : public timmy::transport::IOscCommandProfile
    {
    public:
        // Rejects every semantic command to model unsupported profile behavior.
        timmy::transport::OscMappingResult map(
            const timmy::command::SemanticCommand&) const override
        {
            return {std::nullopt, "Unsupported by test profile."};
        }
    };

    // Verifies all generic mappings, boundary levels, and exact typed arguments.
    void testMappings()
    {
        const timmy::transport::GenericOscCommandProfile profile;
        const auto channel = profile.map(setIntensity(
            timmy::command::Channel{7}, 0));
        require(channel.message.has_value());
        require(channel.message->address == "/timmy/channel/intensity");
        require(channel.message->arguments.size() == 2);
        requireInteger(channel.message->arguments[0], 7);
        requireInteger(channel.message->arguments[1], 0);

        const auto fixture = profile.map(setIntensity(
            timmy::command::Fixture{42}, 100));
        require(fixture.message.has_value());
        require(fixture.message->address == "/timmy/fixture/42/intensity");
        require(fixture.message->arguments.size() == 1);
        requireInteger(fixture.message->arguments[0], 100);

        const auto group = profile.map(setIntensity(
            timmy::command::Group{3}, 55));
        require(group.message.has_value());
        require(group.message->address == "/timmy/group/intensity/3");
        require(group.message->arguments.size() == 1);
        requireInteger(group.message->arguments[0], 55);
    }

    // Verifies an OSC message is serialized to exact OSC 1.0 bytes.
    void testSerialization()
    {
        const timmy::transport::OscMessage message{
            "/x", {std::int32_t{0x01020304}, 1.0F, std::string{"ok"}}};
        const auto packet = timmy::transport::serializeOscMessage(message);
        require(packet.bytes.has_value());
        const std::vector<std::uint8_t> expected{
            '/', 'x', 0, 0, ',', 'i', 'f', 's', 0, 0, 0, 0,
            1, 2, 3, 4, 0x3f, 0x80, 0, 0, 'o', 'k', 0, 0};
        require(*packet.bytes == expected);
    }

    // Verifies validation and mapping failures occur before any UDP send.
    void testPreSendFailures()
    {
        auto profile = std::make_shared<timmy::transport::GenericOscCommandProfile>();
        auto sender = std::make_unique<RecordingSender>();
        auto* recorder = sender.get();
        timmy::transport::OscCommandEmitter invalid(
            {"", 0}, profile, std::move(sender));
        const auto invalidResult = invalid.emit(setIntensity(
            timmy::command::Channel{1}, 50));
        require(invalidResult.state == timmy::transport::EmissionState::failed);
        require(invalidResult.diagnostic == "OSC destination host must not be empty.");
        require(recorder->sendCount == 0);

        auto portSender = std::make_unique<RecordingSender>();
        auto* portRecorder = portSender.get();
        timmy::transport::OscCommandEmitter invalidPort(
            {"127.0.0.1", 65536}, profile, std::move(portSender));
        const auto invalidPortResult = invalidPort.emit(setIntensity(
            timmy::command::Channel{1}, 50));
        require(invalidPortResult.state
            == timmy::transport::EmissionState::failed);
        require(invalidPortResult.diagnostic
            == "OSC destination port must be between 1 and 65535.");
        require(portRecorder->sendCount == 0);

        auto rejecting = std::make_shared<RejectingProfile>();
        auto rejectedSender = std::make_unique<RecordingSender>();
        auto* rejectedRecorder = rejectedSender.get();
        timmy::transport::OscCommandEmitter rejected(
            {"127.0.0.1", 9000}, rejecting, std::move(rejectedSender));
        const auto rejectedResult = rejected.emit(setIntensity(
            timmy::command::Group{1}, 50));
        require(rejectedResult.state == timmy::transport::EmissionState::failed);
        require(rejectedResult.diagnostic == "Unsupported by test profile.");
        require(rejectedRecorder->sendCount == 0);
    }

    // Verifies success and failure propagation with exactly one send per emit.
    void testEmissionPropagation()
    {
        auto profile = std::make_shared<timmy::transport::GenericOscCommandProfile>();
        auto sender = std::make_unique<RecordingSender>();
        auto* recorder = sender.get();
        timmy::transport::OscCommandEmitter emitter(
            {"localhost", 8000}, profile, std::move(sender));
        const auto success = emitter.emit(setIntensity(
            timmy::command::Fixture{2}, 25));
        require(success.state == timmy::transport::EmissionState::succeeded);
        require(recorder->sendCount == 1);
        require(recorder->destination.host == "localhost");
        require(recorder->destination.port == 8000);

        recorder->result = {timmy::transport::EmissionState::failed,
            "Injected UDP failure."};
        const auto failure = emitter.emit(setIntensity(
            timmy::command::Fixture{2}, 25));
        require(failure.state == timmy::transport::EmissionState::failed);
        require(failure.diagnostic == "Injected UDP failure.");
        require(recorder->sendCount == 2);
    }

    // Verifies the production sender delivers one exact datagram on loopback.
    void testLoopbackDelivery()
    {
#ifndef _WIN32
        const int receiver = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        require(receiver >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        require(::bind(receiver, reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == 0);
        socklen_t length = sizeof(address);
        require(::getsockname(receiver, reinterpret_cast<sockaddr*>(&address),
            &length) == 0);
        const timeval timeout{1, 0};
        require(::setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout,
            sizeof(timeout)) == 0);

        auto profile = std::make_shared<timmy::transport::GenericOscCommandProfile>();
        timmy::transport::OscCommandEmitter emitter(
            {"127.0.0.1", ntohs(address.sin_port)}, profile);
        const auto command = setIntensity(timmy::command::Channel{9}, 100);
        const auto result = emitter.emit(command);
        require(result.state == timmy::transport::EmissionState::succeeded);

        std::vector<std::uint8_t> received(256);
        const auto receivedSize = ::recv(receiver, received.data(),
            received.size(), 0);
        ::close(receiver);
        require(receivedSize > 0);
        received.resize(static_cast<std::size_t>(receivedSize));
        const auto mapped = profile->map(command);
        require(mapped.message.has_value());
        const auto expected = timmy::transport::serializeOscMessage(
            *mapped.message);
        require(expected.bytes.has_value());
        require(received == *expected.bytes);
#endif
    }

} // namespace

// Runs deterministic mapping, serialization, validation, and emitter tests.
int main()
{
    testMappings();
    testSerialization();
    testPreSendFailures();
    testEmissionPropagation();
    testLoopbackDelivery();
}
