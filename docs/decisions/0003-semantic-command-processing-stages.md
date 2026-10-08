# ADR 0003: Explicit semantic command processing stages

## Status

Accepted; extends ADRs 0001 and 0002.

## Context

TIMMY already has structured commands, compositional selections and verified
OSC adapters. The Charter still described normalization as producing console
text. Accumulating independent complete-phrase replacements would duplicate
selection grammar and couple language to a console. Intent policy and rendered
text also lacked a shared observable boundary.

## Decision

Keep the existing semantic variant as `CommandIntent`, add explicit all-target
selection, and compose on/off prefix vocabulary with the existing selection
grammar. CommandProfile separates recognition guidance, lexical formatting,
semantic vocabulary/defaults, validation policy and rendering configuration.
Core shares command services with both frontends. Every dispatched intent is
validated and rendered before reaching ICommandEmitter's rendered boundary.

Move Hydra console key syntax into the command renderer and preserve its verified
sequences. The Hydra OSC adapter consumes rendered keys, maps them to configured
addresses and executes pulses; it no longer owns semantic-to-console syntax.
Core supplies the same selected profile to frontend recognition and execution,
and execution events expose the actual rendered text.

The boundary also carries the validated intent for existing typed generic OSC
compatibility. Native rendered text is distinct from generic OSC wire payloads.
Text emitters may implement only the rendered boundary. Unsupported all-target
Hydra rendering and generic OSC mappings fail explicitly.

## Consequences

Language combinations reuse deterministic grammar, execution remains low latency
and offline, and meaning is separate from rendering vocabulary. ASR remains
replaceable; backend grammar APIs stay private to ASR. No LLM dependency is added.

An optional future local semantic interpreter may produce candidates only after
deterministic parsing fails. Candidates must enter the same validator, renderer
and emitter boundary; they cannot emit directly. Documentation is sufficient for
this extension point, so no speculative interpreter framework is added.

Broader console grammar and text rendering profiles, persisted profile formats,
ASR grammar generation and local AI interpretation are deferred. TIMMY native text
and actual Hydra key renderers demonstrate the boundary while existing OSC wire
behavior is preserved.
