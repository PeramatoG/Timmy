#include "timmy/core/command_dispatcher.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace {

    // Terminates the test immediately when a deterministic expectation fails.
    void require(bool condition)
    {
        if (!condition)
        {
            std::abort();
        }
    }

    class RecordingEmitter final : public timmy::transport::ICommandEmitter
    {
    public:
        // Records the exact typed command and returns the configured result.
        timmy::transport::EmissionResult emit(
            const timmy::command::SemanticCommand& command) override
        {
            ++invocationCount;
            receivedCommand = command;
            return result;
        }

        timmy::transport::EmissionResult result{
            timmy::transport::EmissionState::succeeded, {}
        };
        int invocationCount = 0;
        std::optional<timmy::command::SemanticCommand> receivedCommand;
    };

    // Creates a valid typed intensity command for dispatch assertions.
    timmy::command::SemanticCommand setIntensity(
        timmy::command::LightingTarget target,
        int percent)
    {
        const auto level = timmy::command::IntensityLevel::fromPercent(percent);
        require(level.has_value());
        return timmy::command::SetIntensity{std::move(target), *level};
    }

} // namespace

// Verifies success, failure, absence, and single-invocation dispatch behavior.
int main()
{
    auto emitter = std::make_shared<RecordingEmitter>();
    timmy::core::CommandDispatcher dispatcher(emitter);

    const auto command = setIntensity(timmy::command::Channel{5}, 50);
    const auto success = dispatcher.dispatch(command);
    require(success.state == timmy::core::CommandDispatchState::succeeded);
    require(success.diagnostic.empty());
    require(emitter->invocationCount == 1);
    require(emitter->receivedCommand.has_value());

    const auto& received = std::get<timmy::command::SetIntensity>(
        *emitter->receivedCommand);
    require(received.target.singleTarget().has_value());
    require(std::get<timmy::command::Channel>(*received.target.singleTarget()).id == 5);
    require(received.level.percent() == 50);
    require(timmy::command::describe(command)
        == "SetIntensity(Channel(5), 50%)");

    emitter->result = {
        timmy::transport::EmissionState::failed,
        "Output endpoint unavailable."
    };
    const auto failure = dispatcher.dispatch(command);
    require(failure.state == timmy::core::CommandDispatchState::failed);
    require(failure.diagnostic == "Output endpoint unavailable.");
    require(emitter->invocationCount == 2);

    const std::optional<timmy::command::SemanticCommand> absent;
    const auto skipped = dispatcher.dispatch(absent);
    require(skipped.state == timmy::core::CommandDispatchState::notDispatched);
    require(skipped.diagnostic.empty());
    require(emitter->invocationCount == 2);
}
