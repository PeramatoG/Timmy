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
#include <limits>
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
void mapping(const command::SemanticCommand& commandValue,
    std::vector<std::string> addresses, bool clear = false) {
    Hydra2OscOptions options;
    options.preClear = clear;
    auto mapped = Hydra2OscCommandProfile{options}.map(commandValue);
    require(mapped.plan.has_value());
    if (clear && !std::holds_alternative<command::AdvanceTest>(commandValue)
        && !std::holds_alternative<command::ClearEditor>(commandValue))
        addresses.insert(addresses.begin(), {"/keyC", "/keyC"});
    require(mapped.plan->size() == addresses.size());
    auto sender = std::make_unique<Sender>();
    auto* recorded = sender.get();
    std::vector<std::chrono::milliseconds> waits;
    OscCommandEmitter emitter({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(options),
        std::move(sender), [&](auto duration) { waits.push_back(duration); });
    require(emitter.emit(commandValue).state == EmissionState::succeeded);
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        auto pulse = std::get<OscKeyPulse>((*mapped.plan)[i]);
        require(pulse.address == addresses[i]);
        require(pulse.address != "/keyRST"
            || std::holds_alternative<command::ClearEditor>(commandValue));
        require(recorded->packets[2*i] == packet(addresses[i], 1));
        require(recorded->packets[2*i+1] == packet(addresses[i], 0));
    }
    require(recorded->packets.size() == 2*addresses.size());
    require(waits.size() == 2*addresses.size());
    for (auto wait : waits) require(wait == std::chrono::milliseconds{30});
}
void mapping(const std::string& text, std::vector<std::string> addresses, bool clear = false) {
    mapping(parse(text), std::move(addresses), clear);
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
    require(options.keyAddresses.size() == 26);
    for (const auto& key : {"CH", "FXT", "GRP", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "RST", "REL", "THRU", "EXCP", "CALL", "HOME", "OK", "TEST", "NEXT", "PREV"})
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
    for (const auto& key : {"TEST", "NEXT", "PREV"}) {
        options = Hydra2OscOptions{};
        options.keyAddresses[key] = std::string{"/custom/"} + key;
        const command::SemanticCommand commandValue = std::string{key} == "TEST"
            ? command::SemanticCommand{command::AdvanceTest{}}
            : command::SemanticCommand{command::NavigateSelection{
                command::TargetType::channel, std::string{key} == "NEXT"
                    ? command::SelectionDirection::next : command::SelectionDirection::previous}};
        const auto mapped = Hydra2OscCommandProfile(options).map(commandValue);
        require(mapped.plan.has_value());
        require(std::get<OscKeyPulse>(mapped.plan->back()).address == options.keyAddresses[key]);
        options.keyAddresses.erase(key);
        const auto missing = Hydra2OscCommandProfile(options).map(commandValue);
        require(!missing.plan && missing.diagnostic.find(key) != std::string::npos);
    }
    options = Hydra2OscOptions{}; options.hold = std::chrono::milliseconds{-1};
    auto negativeSender = std::make_unique<Sender>(); auto* negativeRecorded = negativeSender.get();
    OscCommandEmitter negative({"test", 8000}, std::make_shared<Hydra2OscCommandProfile>(options),
        std::move(negativeSender), [](auto) {});
    require(negative.emit(parse("Channel 1 At Full")).state == EmissionState::failed);
    require(negativeRecorded->packets.empty());
}
void selectionsAndActions() {
    mapping("Canales 1 a 10 al 50", {"/keyCH", "/key1", "/keyTHRU", "/key1", "/key0", "/key@", "/key5", "/key0"});
    mapping("Canales 1, 3 y 5 al 70", {"/keyCH", "/key1", "/keyOK", "/key3", "/keyOK", "/key5", "/key@", "/key7", "/key0"});
    mapping("Canales 1 a 15 excepto 5 al 50", {"/keyCH", "/key1", "/keyTHRU", "/key1", "/key5", "/keyEXCP", "/key5", "/key@", "/key5", "/key0"});
    mapping("Canales 1 a 15 excepto 5 a 8 al 50", {"/keyCH", "/key1", "/keyTHRU", "/key1", "/key5", "/keyEXCP", "/key5", "/keyTHRU", "/key8", "/key@", "/key5", "/key0"});
    mapping("Selecciona canal 5", {"/keyCH", "/key5", "/keyOK"});
    mapping("Selecciona fixtures 1, 3 y 5", {"/keyFXT", "/key1", "/keyOK", "/key3", "/keyOK", "/key5", "/keyOK"}, true);
    mapping("Libera canales 30 a 40", {"/keyCH", "/key3", "/key0", "/keyTHRU", "/key4", "/key0", "/keyREL"});
    mapping("Limpia editor", {"/keyRST"});
    mapping("Clear editor", {"/keyRST"}, true);
    mapping("Test canal 21", {"/keyCH", "/key2", "/key1", "/keyTEST"});
    mapping("Prueba fixtures 1 a 6", {"/keyFXT", "/key1", "/keyTHRU", "/key6", "/keyTEST"}, true);
    mapping("Test grupo 3", {"/keyGRP", "/key3", "/keyTEST"});
    for (const auto& phrase : {"test", "siguiente", "next", "siguiente test", "test siguiente", "next test"}) {
        mapping(phrase, {"/keyTEST"});
        mapping(phrase, {"/keyTEST"}, true);
    }
    mapping("Siguiente canal", {"/keyCH", "/keyNEXT"});
    mapping("Canal anterior", {"/keyCH", "/keyPREV"});
    mapping("Next fixture", {"/keyFXT", "/keyNEXT"});
    mapping("Previous fixture", {"/keyFXT", "/keyPREV"}, true);
    mapping("Next group", {"/keyGRP", "/keyNEXT"});
    mapping("Previous group", {"/keyGRP", "/keyPREV"});
    mapping("Home fixture 5", {"/keyFXT", "/key5", "/keyHOME"});
    mapping("Fixtures 1 a 6 a home", {"/keyFXT", "/key1", "/keyTHRU", "/key6", "/keyHOME"}, true);

    const command::TargetSelection compact{command::TargetType::channel,
        {{1, 15}, {20, 20}}, {{5, 5}, {7, 8}}};
    mapping(command::SelectSelection{compact}, {"/keyCH", "/key1", "/keyTHRU", "/key1", "/key5", "/keyOK", "/key2", "/key0", "/keyEXCP", "/key5", "/keyEXCP", "/key7", "/keyTHRU", "/key8", "/keyOK"});
    const command::TargetSelection large{command::TargetType::fixture,
        {{1, std::numeric_limits<std::uint32_t>::max()}}};
    mapping(command::HomeSelection{large}, {"/keyFXT", "/key1", "/keyTHRU", "/key4", "/key2", "/key9", "/key4", "/key9", "/key6", "/key7", "/key2", "/key9", "/key5", "/keyHOME"});
}
void invalidSelections() {
    const Hydra2OscCommandProfile profile;
    const auto rejected = [&](const command::SemanticCommand& commandValue) {
        const auto mapped = profile.map(commandValue);
        require(!mapped.plan && !mapped.diagnostic.empty());
        auto sender = std::make_unique<Sender>();
        auto* recorded = sender.get();
        OscCommandEmitter emitter({"test", 8000},
            std::make_shared<Hydra2OscCommandProfile>(), std::move(sender), [](auto) {});
        require(emitter.emit(commandValue).state == EmissionState::failed);
        require(recorded->packets.empty());
    };
    rejected(command::SelectSelection{command::TargetSelection{}});
    rejected(command::SelectSelection{command::Channel{0}});
    rejected(command::TestSelection{command::TargetSelection{
        command::TargetType::channel, {{10, 1}}}});
    rejected(command::ReleaseSelection{command::TargetSelection{
        command::TargetType::fixture, {{1, 10}}, {{0, 5}}}});
    rejected(command::SelectSelection{command::TargetSelection{
        static_cast<command::TargetType>(999), {{1, 1}}}});
    rejected(command::SelectSelection{command::TargetSelection{
        command::TargetType::channel,
        std::vector<command::TargetRange>(command::TargetSelection::maxTerms + 1, {1, 1})}});
    rejected(command::HomeSelection{command::Channel{1}});
    rejected(command::HomeSelection{command::Group{1}});
    rejected(command::NavigateSelection{static_cast<command::TargetType>(999),
        command::SelectionDirection::next});
    rejected(command::NavigateSelection{command::TargetType::channel,
        static_cast<command::SelectionDirection>(999)});
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
    selectionsAndActions(); invalidSelections();
    failures(); configuration(); testPlanSerialization();
}
