// Include the frontend translation unit to exercise its private option boundary
// and observer without starting audio, loading a model or constructing transport.
#include "../apps/cli/asr_listen_command.cpp"
#include <cstdlib>
#include <sstream>
#include <vector>
#include "timmy/command/semantic_command_parser.h"

namespace {
    void require(bool condition) { if (!condition) std::abort(); }
    bool parse(std::vector<std::string> args, timmy::cli::AsrListenOptions& options)
    {
        std::vector<char*> argv;
        for (auto& arg : args) argv.push_back(arg.data());
        return timmy::cli::parseOptions(static_cast<int>(argv.size()), argv.data(), options);
    }
}
int main()
{
    using namespace timmy;
    cli::AsrListenOptions transcriptOnly;
    require(parse({"--model", "unused-model", "--language", "es"}, transcriptOnly));
    require(transcriptOnly.profile.empty());
    cli::ConsoleRecognitionObserver observer;
    require(observer.executor == nullptr);
    core::RecognitionTranscript transcript;
    transcript.assemblyState = core::RecognitionAssemblyState::complete;
    transcript.semanticState = core::RecognitionSemanticState::produced;
    transcript.semanticCommand = command::SemanticCommandParser{}.parse("Channel 5 At 50").command;
        transcript.validation = command::validateIntent(*transcript.semanticCommand);
        transcript.rendering = command::renderIntent(*transcript.semanticCommand);
    std::ostringstream output;
    auto* previous = std::cout.rdbuf(output.rdbuf());
    observer.onRecognitionTranscript(transcript);
    std::cout.rdbuf(previous);
    require(output.str().find("PARSED INTENT: produced") != std::string::npos);
    require(output.str().find("VALIDATION: accepted") != std::string::npos);
    require(output.str().find("RENDERED COMMAND: Channel 5 At 50") != std::string::npos);
    require(output.str().find("DISPATCH:") == std::string::npos);
    const auto unsupported = command::CommandProcessor{command::hydra2CommandProfile()}.process("fuera todo");
    transcript.semanticCommand = unsupported.parsed.command;
    transcript.validation = unsupported.validation;
    transcript.rendering = unsupported.rendering;
    output.str("");
    previous = std::cout.rdbuf(output.rdbuf());
    observer.onRecognitionTranscript(transcript);
    std::cout.rdbuf(previous);
    require(output.str().find("RENDERING: rejected") != std::string::npos);
    require(output.str().find(unsupported.rendering.diagnostic) != std::string::npos);

    for (const auto& extra : std::vector<std::vector<std::string>>{
        {"--host", "localhost"}, {"--port", "8000"}, {"--key-hold-ms", "30"},
        {"--key-gap-ms", "30"}, {"--pre-clear"}, {"--profile", "hydra2"},
        {"--profile", "other"}, {"--profile", "generic", "--host", "localhost",
            "--port", "8000", "--pre-clear"},
        {"--profile", "hydra2", "--host", "localhost", "--port", "65536"}})
    {
        std::vector<std::string> args{"--model", "unused-model"};
        args.insert(args.end(), extra.begin(), extra.end());
        cli::AsrListenOptions options;
        require(!parse(args, options));
    }
    cli::AsrListenOptions live;
    require(parse({"--model", "unused-model", "--host", "localhost", "--port", "8000",
        "--key-hold-ms", "40", "--key-gap-ms", "20", "--pre-clear", "--profile", "hydra2"}, live));
    require(live.port == 8000 && live.hydra.hold.count() == 40 && live.hydra.gap.count() == 20 && live.hydra.preClear);
    cli::AsrListenOptions generic;
    require(parse({"--model", "unused-model", "--profile", "generic", "--host", "localhost", "--port", "8000"}, generic));
}
