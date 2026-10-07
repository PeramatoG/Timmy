# ADR 0002: Compact selections and shared command classification

## Status

Accepted; extends ADR 0001.

## Context

Everyday channel checking needs selections, release, TEST progression, navigation,
fixture HOME and deliberate editor clearing. Recognition hints previously
advertised phrases that the independent assembler and parser could not accept.
Large ranges must not create hundreds of commands or a long execution backlog.

## Decision

`timmy_command` represents a selection with a target type and compact inclusive
ranges for included and excluded terms. A single ID is a range with equal
endpoints. Selections are structurally validated and bounded to 32 terms; they
contain no Hydra key names or OSC addresses.

The semantic parser classifies text as complete, pending or invalid. The bounded
assembler consumes this classification instead of maintaining a second grammar.
A bare target selection remains pending, while explicit selection/action verbs
complete it. A pending selection followed by `test` takes precedence over the
standalone AdvanceTest operation. Standalone `siguiente` belongs to AdvanceTest;
NEXT/PREV selection navigation requires an explicit target type.

Core continues to publish raw, normalized, assembly and semantic observations.
CLI and GUI consume the expanded semantic variant without interpreting phrases.
Tests route every representative recognition hint through the shared pipeline.
Recognition remains command-oriented and backend-neutral.

Hydra's transport profile serializes selections without expanding ranges and owns
all key/timing details. ClearEditor is the only operation that emits RST, and
AdvanceTest emits a single TEST pulse. Generic OSC retains its single-target
SetIntensity mappings and diagnoses unsupported operations and selections.

## Consequences

The workflow stays deterministic and inspectable without conversational state,
show programming or console feedback. The shared execution queue defaults to
four waiting commands plus one in flight; overflow rejects new commands with a
visible diagnostic. Disarm/Stop continue discarding waiting work and cannot replay
commands recognized while disarmed. Hardware acceptance is still needed to
verify the console configuration and voice recognition with actual models.
