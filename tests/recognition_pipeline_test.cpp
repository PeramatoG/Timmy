#include "timmy/core/recognition_pipeline.h"
#include "timmy/core/command_recognition_profile.h"
#include "timmy/transport/hydra2_osc_command_profile.h"

#include <cassert>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

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

    assert(pipeline.start({}).succeeded);
    backend->emit("  Canal 5.  ");
    assert(observer->transcripts.size() == 1);
    assert(observer->transcripts[0].rawTranscript == "  Canal 5.  ");
    assert(observer->transcripts[0].normalizedTranscript == "Canal 5");
    assert(observer->transcripts[0].assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    assert(observer->transcripts[0].assemblyText == "Canal 5");
    assert(observer->transcripts[0].semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    assert(!observer->transcripts[0].semanticCommand);

    backend->emit(" al 50 %. ");
    assert(observer->transcripts.size() == 2);
    assert(observer->transcripts[1].normalizedTranscript == "al 50%");
    assert(observer->transcripts[1].assemblyState
        == timmy::core::RecognitionAssemblyState::complete);
    assert(observer->transcripts[1].assemblyText == "Canal 5 al 50%");
    assert(observer->transcripts[1].semanticState
        == timmy::core::RecognitionSemanticState::produced);
    assert(observer->transcripts[1].semanticCommand);
    assert(timmy::command::describe(*observer->transcripts[1].semanticCommand)
        == "SetIntensity(Channel(5), 50%)");

    backend->emit("Fixture 1 At Full");
    assert(observer->transcripts.size() == 3);
    assert(observer->transcripts[2].assemblyState
        == timmy::core::RecognitionAssemblyState::complete);
    assert(observer->transcripts[2].assemblyText == "Fixture 1 At Full");
    assert(observer->transcripts[2].semanticState
        == timmy::core::RecognitionSemanticState::produced);
    assert(timmy::command::describe(*observer->transcripts[2].semanticCommand)
        == "SetIntensity(Fixture(1), 100%)");

    backend->emit("background noise");
    assert(observer->transcripts.size() == 4);
    assert(observer->transcripts[3].assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    assert(observer->transcripts[3].assemblyText.empty());
    assert(observer->transcripts[3].semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    assert(!observer->transcripts[3].semanticCommand);

    backend->emit("Channel 1 At 101");
    assert(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    assert(observer->transcripts.back().semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    assert(!observer->transcripts.back().semanticCommand);

    // Every representative recognition phrase must reach the same Core semantics.
    const auto hints = timmy::core::commandRecognitionHints();
    assert(hints.constraintIntent == timmy::asr::RecognitionConstraintIntent::commandPhrase);
    for (const auto& phrase : hints.expectedPhrases)
    {
        backend->emit(phrase);
        const auto& transcript = observer->transcripts.back();
        assert(transcript.rawTranscript == phrase);
        assert(transcript.assemblyState == timmy::core::RecognitionAssemblyState::complete);
        assert(transcript.semanticState == timmy::core::RecognitionSemanticState::produced);
        assert(transcript.semanticCommand);
        assert(!timmy::command::describe(*transcript.semanticCommand).empty());
    }

    // Exercise the channel-check workflow from fake ASR through exact Hydra plans.
    const timmy::transport::Hydra2OscCommandProfile hydra;
    const auto expectPlan = [&](const std::string& phrase,
                               const std::vector<std::string>& expectedAddresses) {
        backend->emit(phrase);
        const auto& transcript = observer->transcripts.back();
        assert(transcript.assemblyState == timmy::core::RecognitionAssemblyState::complete);
        assert(transcript.semanticState == timmy::core::RecognitionSemanticState::produced);
        assert(transcript.semanticCommand);
        const auto mapped = hydra.map(*transcript.semanticCommand);
        assert(mapped.plan);
        std::vector<std::string> actual;
        for (const auto& step : *mapped.plan)
        {
            assert(std::holds_alternative<timmy::transport::OscKeyPulse>(step));
            actual.push_back(std::get<timmy::transport::OscKeyPulse>(step).address);
        }
        assert(actual == expectedAddresses);
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
    assert(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    assert(!observer->transcripts.back().semanticCommand);
    expectPlan("al 50", {"/keyCH", "/key1", "/keyTHRU", "/key1", "/key0",
        "/key@", "/key5", "/key0"});
    backend->emit("Canal 10");
    assert(!observer->transcripts.back().semanticCommand);
    expectPlan("test", {"/keyCH", "/key1", "/key0", "/keyTEST"});
    expectPlan("test", {"/keyTEST"});

    // A transcript-ending list comma cannot prematurely select the first item.
    backend->emit("Selecciona canales 1,");
    assert(observer->transcripts.back().normalizedTranscript == "Selecciona canales 1,");
    assert(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    assert(!observer->transcripts.back().semanticCommand);
    backend->emit("3,");
    assert(observer->transcripts.back().normalizedTranscript == "3,");
    assert(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    assert(!observer->transcripts.back().semanticCommand);
    expectPlan("5", {"/keyCH", "/key1", "/keyOK", "/key3", "/keyOK",
        "/key5", "/keyOK"});

    backend->emitDiagnostic({
        timmy::asr::AsrDiagnosticCode::recognitionFailure, "kept message" });
    assert(observer->diagnostics.front().code
        == timmy::asr::AsrDiagnosticCode::recognitionFailure);
    assert(observer->diagnostics.front().message == "kept message");

    std::vector<float> audio(480, 0.1F);
    selector->onAudioBlock({ audio, 480, { 48000, 1 } });
    assert(backend->blockCount > 0);

    backend->emit("Group 2");
    assert(pipeline.stop().succeeded);
    const int stoppedCount = backend->blockCount;
    selector->onAudioBlock({ audio, 480, { 48000, 1 } });
    assert(backend->blockCount == stoppedCount);
    assert(pipeline.start({}).succeeded);
    backend->emit("At 25%");
    assert(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    assert(observer->transcripts.back().assemblyText.empty());
    assert(observer->transcripts.back().semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    assert(!observer->transcripts.back().semanticCommand);

    backend->emit("Channel 7");
    assert(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    assert(observer->transcripts.back().semanticState
        == timmy::core::RecognitionSemanticState::notAttempted);
    assert(!observer->transcripts.back().semanticCommand);
    pipeline.discardPendingAssembly();
    backend->emit("At 70%");
    assert(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    assert(observer->transcripts.back().assemblyText.empty());
    assert(pipeline.stop().succeeded);
}
