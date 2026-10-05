# ADR 0001: Minimal semantic lighting command boundary

## Status

Accepted

## Context

The recognition pipeline already normalizes final ASR transcripts and assembles
supported fragments into complete command text. Frontends need a shared,
transport-neutral interpretation of that complete text before any future safety
or emission layer can act on it. Passing console syntax or OSC strings through
Core would couple command meaning to a particular output adapter.

## Decision

`timmy_command` owns a deliberately small typed model containing `Channel`,
`Fixture`, and `Group` targets and a `SetIntensity` command. Intensity is stored
as a validated percentage in the inclusive range 0 through 100.

A deterministic semantic parser accepts only complete target-connector-level
shapes already recognized by `CommandAssembler`. It returns an explicit result
that either contains a semantic command or identifies the text as unsupported
or invalid. Core invokes it only after assembly reports `complete` and publishes
both the parse state and optional semantic command to all observers.

The diagnostic description of a semantic command is human-readable only. It is
not console syntax, an OSC payload, or an execution request.

## Consequences

CLI and GUI consumers observe the same semantic result without implementing
their own parsers. Pending and rejected assembly results cannot become semantic
commands. Future transports can translate the typed model behind their own
boundaries, while broader lighting syntax remains unsupported until a concrete
requirement expands the model.
