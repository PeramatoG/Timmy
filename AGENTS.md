# TIMMY agent guidance

## Source of truth

* Read [`docs/PROJECT_CHARTER.md`](docs/PROJECT_CHARTER.md) before architectural or cross-module changes.
* The charter defines intended ownership boundaries and architecture.
* Do not put temporary roadmaps or task checklists into repository architecture documentation.
* Major intentional architecture changes should update the charter or add a focused ADR under `docs/decisions/`.

## Scope and change discipline

* Keep changes small and focused on the requested responsibility.
* Prefer incremental changes over broad rewrites.
* Do not create empty placeholder modules or targets solely because the charter anticipates them.
* New modules should own a real current responsibility.

## Module boundaries

* `timmy_audio` owns audio devices, capture, routing, metering, and audio-format adaptation.
* The physical microphone must have one owner: ASR backends must never enumerate or open capture devices.
* The current ASR input boundary is continuous mono float PCM at 16 kHz produced by the audio pipeline.
* ASR must remain replaceable behind backend-neutral contracts.
* Whisper-specific types or settings must not leak into generic ASR, command, GUI, or transport APIs.
* Command normalization and validation must remain outside ASR.
* Transport and OSC logic must remain outside command normalization.
* wxWidgets types must not leak into reusable modules.
* CLI and GUI are frontends over shared capabilities, not owners of business logic.

## Runtime behavior

* Normal operation must remain offline-first.
* Avoid silent fallback or silent behavior changes; return structured state and diagnostics where practical.
* Never perform ASR inference, GUI work, disk I/O, or network I/O from the real-time audio callback.

## Dependency policy

* Pin third-party dependency versions; never track a moving branch such as `master` or `main` for production integration.
* Keep dependency implementation details behind Timmy-owned boundaries.
* Do not commit large ASR model files to Git.
* New redistributed dependencies must update `THIRD_PARTY_NOTICES.md` and have license compatibility checked.
* Do not require developers to install a dependency globally when a reproducible project-local CMake integration is practical.

## Build and CI

* Keep source ownership explicit in CMake; do not use recursive source globbing for project code.
* Preserve `TIMMY_BUILD_GUI=OFF` headless builds.
* Default deterministic tests and CI must not require a microphone, GPU, model download, lighting console, or network service.
* Hardware and model integration tests must be optional and clearly separated from deterministic tests.
* Project-owned code should build cleanly under the existing warning levels. Fix warnings rather than globally suppressing them where practical.

## Code quality

* New code, comments, and developer documentation must be in English.
* Prefer Timmy-owned interfaces and ordinary C++ types at module boundaries.
* Keep backend- and platform-specific code private to the owning implementation.
* Preserve explicit ownership and lifecycle for devices, worker threads, models, and callbacks.
