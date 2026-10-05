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

        // Records the raw and normalized transcript pair.
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

// Verifies attachment, normalization, diagnostics, and repeatable lifecycle.
int main()
{
    auto selector = std::make_shared<timmy::audio::AudioChannelSelector>();
    auto backend = std::make_shared<FakeBackend>();
    auto observer = std::make_shared<Observer>();
    timmy::core::RecognitionPipeline pipeline(selector, backend);
    pipeline.setObserver(observer);

    assert(pipeline.start({}).succeeded);
    backend->emit("  Canal 5 al 50 %.  ");
    backend->emitDiagnostic({
        timmy::asr::AsrDiagnosticCode::recognitionFailure, "kept message" });
    assert(observer->transcripts.size() == 1);
    assert(observer->transcripts.front().rawTranscript == "  Canal 5 al 50 %.  ");
    assert(observer->transcripts.front().normalizedTranscript == "Canal 5 al 50%");
    assert(observer->diagnostics.front().code
        == timmy::asr::AsrDiagnosticCode::recognitionFailure);
    assert(observer->diagnostics.front().message == "kept message");

    std::vector<float> audio(480, 0.1F);
    selector->onAudioBlock({ audio, 480, { 48000, 1 } });
    assert(backend->blockCount > 0);
    assert(pipeline.stop().succeeded);
    const int stoppedCount = backend->blockCount;
    selector->onAudioBlock({ audio, 480, { 48000, 1 } });
    assert(backend->blockCount == stoppedCount);
    assert(pipeline.start({}).succeeded);
    assert(pipeline.stop().succeeded);
}
