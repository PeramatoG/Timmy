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

An exact lexical layer in `timmy_command` canonicalizes known Spanish and English
spoken aliases before semantic classification. It handles only declared tokens
and phrases, including supported accented/unaccented Spanish forms and the
explicit ASR alias `cler`; it does not guess by spelling distance. The same alias
inventory supplies backend-neutral recognition vocabulary. `TranscriptNormalizer`
continues to handle transcript formatting rather than semantic interpretation.
It preserves numeric trailing commas as possible list continuations; the shared
grammar identifies sentence commas on completed intensity commands.

The semantic parser classifies text as complete, pending or invalid. The bounded
assembler consumes this classification instead of maintaining a second grammar.
A bare target selection without an explicit edit clause remains pending, while
explicit selection/action verbs complete it. A bare selection with an explicit
add/exclusion clause, such as `canales 1 al 10 excepto 6`, completes a
`SelectSelection`; use `pon` or `set` to keep a multi-fragment intensity command
open until its level arrives. A pending selection followed by `test` takes precedence over the
standalone AdvanceTest operation. Standalone `siguiente` belongs to AdvanceTest;
NEXT/PREV selection navigation requires an explicit target type.

`ModifySelection` adds or removes compact terms from the console's current
selection. `ClearSelection`, abrupt `ClearEditor` and fading `ReleaseEditor`
remain separate operations. Absolute selections can combine additive and excluded
terms within one utterance without expanding ranges.

Core retains only an optional last explicit `TargetType` for elided relative
phrases such as `súmale el 4`. Classification and parsing share the same context
snapshot; only a successfully parsed command with an explicit target updates it.
Pending or invalid fragments do not update context, and an elided edit without a
known target is invalid. This independently testable, bounded linguistic context
contains no selected IDs and does not mirror console state. Stop, start and
discarding pending assembly reset it; GUI disarm/re-arm also discards it. Profiles
can be replaced only while stopped, so their replacement begins a fresh session.

Core continues to publish raw, normalized, assembly and semantic observations.
CLI and GUI consume the expanded semantic variant without interpreting phrases.
Tests route every representative recognition hint through the shared pipeline.
Recognition remains command-oriented and backend-neutral.

Hydra's transport profile serializes selections without expanding ranges and owns
all key/timing details. ClearEditor is the only operation that emits RST, and
AdvanceTest emits a single TEST pulse. ClearSelection emits C C, while
ReleaseEditor emits REL REL. These distinct plans follow the
[Hydra II v7.04 manual](https://lt-light.com/en/download/system-ii-manuals/?filename=ManualSysII704_EN.pdf&ind=17260914681211&wpdmdl=5453),
sections 5.1.1 (selection editing) and 5.2.12 (editor release/reset).
Relative edits skip optional pre-clear so the existing selection survives.
Generic OSC retains its single-target SetIntensity mappings and diagnoses
unsupported operations and selections.

## Consequences

The workflow stays deterministic and inspectable with only the minimal
last-target-type linguistic context, without show programming or console
feedback. The shared execution queue defaults to four waiting commands plus one
in flight; overflow rejects new commands with a
visible diagnostic. Disarm/Stop continue discarding waiting work and cannot replay
commands recognized while disarmed. Hardware acceptance is still needed to
verify the console configuration and voice recognition with actual models.
