/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/transport/hydra2_osc_command_profile.h"
#include "timmy/transport/osc_command_emitter.h"
#include "timmy/transport/osc_packet.h"
#include "timmy/command/semantic_command_parser.h"
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <vector>

using namespace timmy;
using namespace timmy::transport;
namespace {
void require(bool value) { if (!value) std::abort(); }
command::SemanticCommand parse(const std::string& text) {
    auto result = command::SemanticCommandParser{}.parse(text);
    require(result.command.has_value());
    return *result.command;
}
std::vector<std::uint8_t> packet(const std::string& address, int edge) {
    return *serializeOscMessage({address, {std::int32_t{edge}}}).bytes;
}
class Sender final : public IUdpSender {
public:
    std::vector<std::vector<std::uint8_t>> packets;
    int failAt = -1;
    bool throws = false;
    bool alwaysFail = false;
    EmissionResult send(const OscDestination&, std::span<const std::uint8_t> bytes) override {
        packets.emplace_back(bytes.begin(), bytes.end());
        if (static_cast<int>(packets.size()) == failAt || alwaysFail) {
            if (throws) throw std::runtime_error("injected");
            return {EmissionState::failed, "injected"};
        }
        return {EmissionState::succeeded, {}};
    }
};
void mapping(const std::string& text, std::vector<std::string> addresses, bool clear = false) {
    Hydra2OscOptions options;
    options.preClear = clear;
    auto mapped = Hydra2OscCommandProfile{options}.map(parse(text));
    require(mapped.plan.has_value());
    if (clear) addresses.insert(addresses.begin(), {"/keyC", "/keyC"});
    require(mapped.plan->size() == addresses.size());
    auto sender = std::make_unique<Sender>();
    auto* recorded = sender.get();
    std::vector<std::chrono::milliseconds> waits;
    OscCommandEmitter emitter({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(options),
        std::move(sender), [&](auto duration) { waits.push_back(duration); });
    require(emitter.emit(parse(text)).state == EmissionState::succeeded);
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        auto pulse = std::get<OscKeyPulse>((*mapped.plan)[i]);
        require(pulse.address == addresses[i] && pulse.address != "/keyRST");
        require(recorded->packets[2*i] == packet(addresses[i], 1));
        require(recorded->packets[2*i+1] == packet(addresses[i], 0));
    }
    require(recorded->packets.size() == 2*addresses.size());
    require(waits.size() == 2*addresses.size());
    for (auto wait : waits) require(wait == std::chrono::milliseconds{30});
}
void failures() {
    for (int failAt : {1, 2, 3}) for (bool throws : {false, true}) {
        auto sender = std::make_unique<Sender>(); auto* recorded = sender.get();
        recorded->failAt = failAt; recorded->throws = throws;
        OscCommandEmitter emitter({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(),
            std::move(sender), [](auto) {});
        auto result = emitter.emit(parse("Channel 5 At 50"));
        require(result.state == EmissionState::failed && result.diagnostic == "injected");
        require(recorded->packets.size() == static_cast<std::size_t>(failAt+1));
        require(recorded->packets.back() == packet(failAt == 3 ? "/key5" : "/keyCH", 0));
    }
    for (bool throws : {false, true}) {
        auto sender = std::make_unique<Sender>(); auto* recorded = sender.get();
        recorded->alwaysFail = true; recorded->throws = throws;
        OscCommandEmitter emitter({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(),
            std::move(sender), [](auto) {});
        require(emitter.emit(parse("Channel 5 At 50")).state == EmissionState::failed);
        require(recorded->packets.size() == 2);
    }
    auto sender = std::make_unique<Sender>(); auto* recorded = sender.get();
    OscCommandEmitter emitter({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(),
        std::move(sender), [](auto) { throw std::runtime_error("wait failed"); });
    require(emitter.emit(parse("Channel 5 At 50")).diagnostic == "wait failed");
    require(recorded->packets.size() == 2 && recorded->packets.back() == packet("/keyCH", 0));
}
void configuration() {
    Hydra2OscOptions options;
    require(options.keyAddresses.size() == 23);
    for (const auto& key : {"CH", "FXT", "GRP", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "RST", "REL", "THRU", "EXCP", "CALL", "HOME", "OK"})
        require(options.keyAddresses.at(key) == std::string{"/key"} + key);
    require(options.keyAddresses.at("AT") == "/key@");
    require(options.keyAddresses.at("DOT") == "/key.");
    options.keyAddresses["AT"] = "/custom/at";
    options.hold = std::chrono::milliseconds{7}; options.gap = std::chrono::milliseconds{11};
    auto plan = Hydra2OscCommandProfile(options).map(parse("Channel 1 At Full"));
    require(std::get<OscKeyPulse>((*plan.plan)[2]).address == "/custom/at");
    auto sender = std::make_unique<Sender>(); auto* recorded = sender.get();
    std::vector<std::chrono::milliseconds> waits;
    OscCommandEmitter emitter({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(options),
        std::move(sender), [&](auto d) { waits.push_back(d); });
    require(emitter.emit(parse("Channel 1 At Full")).state == EmissionState::succeeded);
    for (std::size_t i=0; i<waits.size(); ++i)
        require(waits[i] == std::chrono::milliseconds{i%2 == 0 ? 7 : 11});
    require(recorded->packets[4] == packet("/custom/at", 1));
    options.keyAddresses["0"] = "invalid";
    auto invalidSender = std::make_unique<Sender>(); auto* invalidRecorded = invalidSender.get();
    OscCommandEmitter invalid({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(options),
        std::move(invalidSender), [](auto) {});
    require(invalid.emit(parse("Channel 5 At 50")).state == EmissionState::failed);
    require(invalidRecorded->packets.empty());
    options.keyAddresses.erase("AT");
    require(!Hydra2OscCommandProfile(options).map(parse("Channel 1 At Full")).plan);
    options = Hydra2OscOptions{}; options.hold = std::chrono::milliseconds{-1};
    auto negativeSender = std::make_unique<Sender>(); auto* negativeRecorded = negativeSender.get();
    OscCommandEmitter negative({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(options),
        std::move(negativeSender), [](auto) {});
    require(negative.emit(parse("Channel 1 At Full")).state == EmissionState::failed);
    require(negativeRecorded->packets.empty());
}
void testPlanSerialization() {
    auto sender = std::make_unique<Sender>(); auto* recorded = sender.get();
    std::promise<void> entered, resume, secondStarted;
    auto gate = resume.get_future().share(); bool firstWait = true;
    OscCommandEmitter emitter({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(),
        std::move(sender), [&](auto) { if (firstWait) { firstWait=false; entered.set_value(); gate.wait(); } });
    auto first = std::async(std::launch::async, [&] { return emitter.emit(parse("Channel 1 At Full")); });
    entered.get_future().wait();
    auto second = std::async(std::launch::async, [&] {
        secondStarted.set_value(); return emitter.emit(parse("Group 3 At 0")); });
    secondStarted.get_future().wait(); resume.set_value();
    require(first.get().state == EmissionState::succeeded);
    require(second.get().state == EmissionState::succeeded);
    const std::vector<std::string> addresses{
        "/keyCH", "/key1", "/key@", "/key@", "/keyGRP", "/key3", "/key@", "/key0", "/key0"};
    require(recorded->packets.size() == 2*addresses.size());
    for (std::size_t i=0; i<addresses.size(); ++i) {
        require(recorded->packets[2*i] == packet(addresses[i], 1));
        require(recorded->packets[2*i+1] == packet(addresses[i], 0));
    }
}
}
int main() {
    mapping("Channel 5 At 50", {"/keyCH", "/key5", "/key@", "/key5", "/key0"});
    mapping("Channel 7 At 5", {"/keyCH", "/key7", "/key@", "/key0", "/key5"});
    mapping("Fixture 12 At 99", {"/keyFXT", "/key1", "/key2", "/key@", "/key9", "/key9"});
    mapping("Group 3 At 0", {"/keyGRP", "/key3", "/key@", "/key0", "/key0"});
    mapping("Channel 1 At Full", {"/keyCH", "/key1", "/key@", "/key@"});
    mapping("Channel 1 At 100", {"/keyCH", "/key1", "/key@", "/key@"}, true);
    failures(); configuration(); testPlanSerialization();
}
