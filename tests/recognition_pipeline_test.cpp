#include "timmy/core/recognition_pipeline.h"
#include "timmy/core/command_recognition_profile.h"
#include "timmy/command/command_lexicon.h"
#include "timmy/transport/hydra2_osc_command_profile.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <source_location>
#include <span>
#include <string>
#include <vector>

namespace {

    // Keep pipeline and lifecycle regressions active in every build configuration.
    void require(bool condition,
        const std::source_location& location = std::source_location::current())
    {
        if (!condition)
        {
            std::cerr << location.file_name() << ':' << location.line()
                << ": pipeline check failed\n";
            std::abort();
        }
    }

    class FakeBackend final : public timmy::asr::IAsrBackend
    {
    public:
        // Reports the minimal deterministic fake capabilities.
        timmy::asr::AsrCapabilities capabilities() const override { return {}; }

        // Returns the fake backend lifecycle state.
        timmy::asr::AsrState state() const override { return state_; }

        // Starts the model-free fake backend.
        timmy::asr::AsrOperationResult start(
            const timmy::asr::AsrConfiguration&) override
        {
            state_ = timmy::asr::AsrState::running;
            return { true, std::nullopt };
        }

        // Stops the model-free fake backend.
        timmy::asr::AsrOperationResult stop() override
        {
            state_ = timmy::asr::AsrState::stopped;
            return { true, std::nullopt };
        }

        // Emits a final result through the production listener contract.
        void emit(std::string text)
        {
            publishResult({ std::move(text), timmy::asr::AsrResultKind::final, {} });
        }

        // Emits a structured diagnostic through the production listener contract.
        void emitDiagnostic(const timmy::asr::AsrDiagnostic& diagnostic)
        {
            publishDiagnostic(diagnostic);
        }

        int blockCount = 0;

    private:
        // Counts validated audio delivered through the pipeline adapter.
        void processAudioBlock(const timmy::audio::AudioBlockView&) override
        {
            ++blockCount;
        }

        timmy::asr::AsrState state_ = timmy::asr::AsrState::stopped;
    };

    class Observer final : public timmy::core::IRecognitionPipelineObserver
    {
    public:
        // Records state changes synchronously for deterministic assertions.
        void onRecognitionStateChanged(
            timmy::core::RecognitionPipelineState state) override
        {
            states.push_back(state);
        }

        // Records the complete diagnostic value.
        void onRecognitionDiagnostic(
            const timmy::asr::AsrDiagnostic& diagnostic) override
        {
            diagnostics.push_back(diagnostic);
        }

        // Records the complete shared recognition event.
        void onRecognitionTranscript(
            const timmy::core::RecognitionTranscript& transcript) override
        {
            transcripts.push_back(transcript);
        }

        std::vector<timmy::core::RecognitionPipelineState> states;
        std::vector<timmy::asr::AsrDiagnostic> diagnostics;
        std::vector<timmy::core::RecognitionTranscript> transcripts;
    };

} // namespace

// Verifies shared normalization, assembly, diagnostics, and lifecycle behavior.
int main()
{
    auto selector = std::make_shared<timmy::audio::AudioChannelSelector>();
    auto backend = std::make_shared<FakeBackend>();
    auto observer = std::make_shared<Observer>();
    timmy::core::RecognitionPipeline pipeline(selector, backend);
    pipeline.setObserver(observer);

    require(pipeline.start({}).succeeded);
    backend->emit("  Canal 5.  ");
    require(observer->transcripts.size() == 1);
    require(observer->transcripts[0].rawTranscript == "  Canal 5.  ");
    require(observer->transcripts[0].normalizedTranscript == "Canal 5");
    require(observer->transcripts[0].assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    require(observer->transcripts[0].assemblyText == "Canal 5");
    require(observer->transcripts[0].semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    require(!observer->transcripts[0].semanticCommand);

    backend->emit(" al 50 %. ");
    require(observer->transcripts.size() == 2);
    require(observer->transcripts[1].normalizedTranscript == "al 50%");
    require(observer->transcripts[1].assemblyState
        == timmy::core::RecognitionAssemblyState::complete);
    require(observer->transcripts[1].assemblyText == "Canal 5 al 50%");
    require(observer->transcripts[1].semanticState
        == timmy::core::RecognitionSemanticState::produced);
    require(observer->transcripts[1].semanticCommand.has_value());
    require(timmy::command::describe(*observer->transcripts[1].semanticCommand)
        == "SetIntensity(Channel(5), 50%)");

    require(observer->transcripts[1].validation.accepted);
    require(observer->transcripts[1].rendering.text == "Channel 5 At 50");

    backend->emit("Fixture 1 At Full");
    require(observer->transcripts.size() == 3);
    require(observer->transcripts[2].assemblyState
        == timmy::core::RecognitionAssemblyState::complete);
    require(observer->transcripts[2].assemblyText == "Fixture 1 At Full");
    require(observer->transcripts[2].semanticState
        == timmy::core::RecognitionSemanticState::produced);
    require(timmy::command::describe(*observer->transcripts[2].semanticCommand)
        == "SetIntensity(Fixture(1), 100%)");

    backend->emit("background noise");
    require(observer->transcripts.size() == 4);
    require(observer->transcripts[3].assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    require(observer->transcripts[3].assemblyText.empty());
    require(observer->transcripts[3].semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    require(!observer->transcripts[3].semanticCommand);

    backend->emit("Channel 1 At 101");
    require(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    require(observer->transcripts.back().semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    require(!observer->transcripts.back().semanticCommand);

    const auto expectProduced = [&](const std::string& phrase) {
        backend->emit(phrase);
        const auto& transcript = observer->transcripts.back();
        if (transcript.semanticState != timmy::core::RecognitionSemanticState::produced)
            std::cerr << "Unaccepted recognition phrase: " << phrase << '\n';
        require(transcript.assemblyState == timmy::core::RecognitionAssemblyState::complete);
        require(transcript.semanticState == timmy::core::RecognitionSemanticState::produced);
        require(transcript.semanticCommand.has_value());
    };
    const auto expectModification = [&](const std::string& phrase,
                                        timmy::command::TargetType type,
                                        timmy::command::SelectionModification operation,
                                        timmy::command::TargetRange term) {
        expectProduced(phrase);
        const auto* edit = std::get_if<timmy::command::ModifySelection>(
            &*observer->transcripts.back().semanticCommand);
        require(edit != nullptr);
        require(edit->modification == operation);
        require(edit->target.type == type);
        require(edit->target.included.size() == 1);
        require(edit->target.included[0].first == term.first);
        require(edit->target.included[0].last == term.last);
        require(edit->target.excluded.empty());
    };
    const auto expectRejected = [&](const std::string& phrase) {
        backend->emit(phrase);
        require(observer->transcripts.back().assemblyState
            == timmy::core::RecognitionAssemblyState::rejected);
        require(!observer->transcripts.back().semanticCommand);
    };
    using timmy::command::TargetType;
    using timmy::command::SelectionModification;

    // Elided edits use only the last successfully parsed explicit target type.
    pipeline.discardPendingAssembly();
    expectRejected("Súmale el 4");
    expectProduced("Dame los canales del 1 al 10");
    expectModification("Súmale el 4", TargetType::channel, SelectionModification::add, {4, 4});
    expectModification("Quita del 5 al 8", TargetType::channel, SelectionModification::remove, {5, 8});
    expectRejected("Fixture 7 at 101");
    expectModification("Add 4", TargetType::channel, SelectionModification::add, {4, 4});
    backend->emit("Fixture 7");
    require(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    expectRejected("background noise");
    expectProduced("Clear selection"); // Supersede assembly without seeding a type.
    expectModification("Remove 10", TargetType::channel, SelectionModification::remove, {10, 10});

    expectProduced("Selecciona grupo 2");
    expectModification("Menos el 10", TargetType::group, SelectionModification::remove, {10, 10});
    expectProduced("Añade fixture 7");
    expectModification("Sumale el 4", TargetType::fixture, SelectionModification::add, {4, 4});
    expectProduced("Limpia selección");
    require(std::holds_alternative<timmy::command::ClearSelection>(
        *observer->transcripts.back().semanticCommand));
    expectProduced("Libera todo");
    require(std::holds_alternative<timmy::command::ReleaseEditor>(
        *observer->transcripts.back().semanticCommand));
    expectProduced("Cler all");
    require(std::holds_alternative<timmy::command::ClearEditor>(
        *observer->transcripts.back().semanticCommand));
    expectModification("Añade el 4", TargetType::fixture, SelectionModification::add, {4, 4});

    // Disarm/re-arm and profile/session transitions discard linguistic context.
    pipeline.discardPendingAssembly();
    expectRejected("Remove 10");
    expectProduced("Select group 2");
    require(pipeline.stop().succeeded);
    backend->emit("Select fixture 9"); // Stopped backends cannot seed a later session.
    require(pipeline.start({}).succeeded);
    expectRejected("Add 4");
    expectProduced("Select fixture 9");
    require(pipeline.start({}).succeeded); // Restart also drops the old type.
    expectRejected("Quita el 10");

    // Every representative recognition phrase must reach the same Core semantics.
    const auto hints = timmy::core::commandRecognitionHints();
    require(hints.constraintIntent == timmy::asr::RecognitionConstraintIntent::commandPhrase);
    require(hints.vocabulary == timmy::command::spokenCommandVocabulary());
    for (const auto& phrase : hints.expectedPhrases)
    {
        pipeline.discardPendingAssembly();
        expectProduced("Select channels 1");
        expectProduced(phrase);
        const auto& transcript = observer->transcripts.back();
        require(transcript.rawTranscript == phrase);
        require(transcript.assemblyState == timmy::core::RecognitionAssemblyState::complete);
        require(transcript.semanticState == timmy::core::RecognitionSemanticState::produced);
        require(transcript.semanticCommand.has_value());
        require(!timmy::command::describe(*transcript.semanticCommand).empty());
    }

    // Exercise the channel-check workflow from fake ASR through exact Hydra plans.
    const timmy::transport::Hydra2OscCommandProfile hydra;
    const auto expectPlan = [&](const std::string& phrase,
                               const std::vector<std::string>& expectedAddresses) {
        backend->emit(phrase);
        const auto& transcript = observer->transcripts.back();
        require(transcript.assemblyState == timmy::core::RecognitionAssemblyState::complete);
        require(transcript.semanticState == timmy::core::RecognitionSemanticState::produced);
        require(transcript.semanticCommand.has_value());
        const auto mapped = hydra.map(*transcript.semanticCommand);
        require(mapped.plan.has_value());
        std::vector<std::string> actual;
        for (const auto& step : *mapped.plan)
        {
            require(std::holds_alternative<timmy::transport::OscKeyPulse>(step));
            actual.push_back(std::get<timmy::transport::OscKeyPulse>(step).address);
        }
        require(actual == expectedAddresses);
    };
    expectPlan("Test canal 21", {"/keyCH", "/key2", "/key1", "/keyTEST"});
    expectPlan("Siguiente", {"/keyTEST"});
    expectPlan("Siguiente", {"/keyTEST"});
    expectPlan("Canales 30 a 40 al 50",
        {"/keyCH", "/key3", "/key0", "/keyTHRU", "/key4", "/key0",
         "/key@", "/key5", "/key0"});
    expectPlan("Libera canales 30 a 40",
        {"/keyCH", "/key3", "/key0", "/keyTHRU", "/key4", "/key0", "/keyREL"});
    expectPlan("Fixtures 1 a 6 a home",
        {"/keyFXT", "/key1", "/keyTHRU", "/key6", "/keyHOME"});
    expectPlan("Limpia editor", {"/keyRST"});

    backend->emit("Canales 1 a 10");
    require(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    require(!observer->transcripts.back().semanticCommand);
    expectPlan("al 50", {"/keyCH", "/key1", "/keyTHRU", "/key1", "/key0",
        "/key@", "/key5", "/key0"});
    backend->emit("Canal 10");
    require(!observer->transcripts.back().semanticCommand);
    expectPlan("test", {"/keyCH", "/key1", "/key0", "/keyTEST"});
    expectPlan("test", {"/keyTEST"});

    // A transcript-ending list comma cannot prematurely select the first item.
    backend->emit("Selecciona canales 1,");
    require(observer->transcripts.back().normalizedTranscript == "Selecciona canales 1,");
    require(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    require(!observer->transcripts.back().semanticCommand);
    backend->emit("3,");
    require(observer->transcripts.back().normalizedTranscript == "3,");
    require(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    require(!observer->transcripts.back().semanticCommand);
    expectPlan("5", {"/keyCH", "/key1", "/keyOK", "/key3", "/keyOK",
        "/key5", "/keyOK"});

    backend->emitDiagnostic({
        timmy::asr::AsrDiagnosticCode::recognitionFailure, "kept message" });
    require(observer->diagnostics.front().code
        == timmy::asr::AsrDiagnosticCode::recognitionFailure);
    require(observer->diagnostics.front().message == "kept message");

    std::vector<float> audio(480, 0.1F);
    selector->onAudioBlock({ audio, 480, { 48000, 1 } });
    require(backend->blockCount > 0);

    backend->emit("Group 2");
    require(pipeline.stop().succeeded);
    const int stoppedCount = backend->blockCount;
    selector->onAudioBlock({ audio, 480, { 48000, 1 } });
    require(backend->blockCount == stoppedCount);
    require(pipeline.start({}).succeeded);
    backend->emit("At 25%");
    require(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    require(observer->transcripts.back().assemblyText.empty());
    require(observer->transcripts.back().semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    require(!observer->transcripts.back().semanticCommand);

    backend->emit("Channel 7");
    require(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    require(observer->transcripts.back().semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    require(!observer->transcripts.back().semanticCommand);
    pipeline.discardPendingAssembly();
    backend->emit("At 70%");
    require(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    require(observer->transcripts.back().assemblyText.empty());
    for (const auto phrase : {"enciende canal 1", "apaga canal 10", "fuera todo",
            "enciende los canales del 2 al 40", "canales del 2 al 40 al 50", "apaga 10"}) {
        backend->emit(phrase);
        const auto& transcript = observer->transcripts.back();
        require(transcript.assemblyState == timmy::core::RecognitionAssemblyState::complete);
        require(transcript.semanticCommand.has_value());
        require(transcript.validation.accepted);
        require(transcript.rendering.text.has_value());
    }
    pipeline.discardPendingAssembly();
    backend->emit("enciende canales 1,");
    require(observer->transcripts.back().assemblyState == timmy::core::RecognitionAssemblyState::pending);
    require(!observer->transcripts.back().semanticCommand);
    require(!observer->transcripts.back().validation.accepted);
    require(!observer->transcripts.back().rendering.text);
    backend->emit("3 y 5");
    require(observer->transcripts.back().assemblyState == timmy::core::RecognitionAssemblyState::complete);
    const auto& prefixList = std::get<timmy::command::SetIntensity>(*observer->transcripts.back().semanticCommand);
    require(prefixList.target.included.size() == 3 && prefixList.level.percent() == 100);
    require(pipeline.stop().succeeded);
}
