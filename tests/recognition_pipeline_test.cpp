#include "timmy/core/recognition_pipeline.h"

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

    backend->emit(" al 50 %. ");
    assert(observer->transcripts.size() == 2);
    assert(observer->transcripts[1].normalizedTranscript == "al 50%");
    assert(observer->transcripts[1].assemblyState
        == timmy::core::RecognitionAssemblyState::complete);
    assert(observer->transcripts[1].assemblyText == "Canal 5 al 50%");

    backend->emit("Fixture 1 At Full");
    assert(observer->transcripts.size() == 3);
    assert(observer->transcripts[2].assemblyState
        == timmy::core::RecognitionAssemblyState::complete);
    assert(observer->transcripts[2].assemblyText == "Fixture 1 At Full");

    backend->emit("background noise");
    assert(observer->transcripts.size() == 4);
    assert(observer->transcripts[3].assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    assert(observer->transcripts[3].assemblyText.empty());

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

    backend->emit("Channel 7");
    assert(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::pending);
    pipeline.discardPendingAssembly();
    backend->emit("At 70%");
    assert(observer->transcripts.back().assemblyState
        == timmy::core::RecognitionAssemblyState::rejected);
    assert(observer->transcripts.back().assemblyText.empty());
    assert(pipeline.stop().succeeded);
}
