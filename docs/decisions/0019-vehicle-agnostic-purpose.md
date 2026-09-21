# ADR-0019: Purpose — vehicle-agnostic framework, not commercial-transport-focused

Status: **accepted**, 2026-09-20
Owner: Joe
Related/supersedes: supersedes ADR-0000's Purpose paragraph only (one clause); no other row of
ADR-0000's decision table is touched.

## Context

`docs/decisions/0000-founding-decisions.md`'s Purpose paragraph read: "A vehicle-agnostic,
high-fidelity six-degree-of-freedom simulation **framework and architecture** focused on commercial
transport aircraft." `docs/architecture.md` §1 echoed this with a user story: "clone the repo,
implement three classes for your aircraft (mass properties, aero, propulsion)." Joe's stated intent
(`docs/refs/astra_framework_intent_and_state_audit.md`, main checkout, gitignored, captured from a
Cowork review session 2026-09-19): "ASTRA is a vehicle-agnostic, high-fidelity 6-DoF simulation
framework. The framework and architecture are the product ... The shipped commercial-transport model
is the simplest possible worked example. Its fidelity and its numbers do not matter." The audit names
the concrete failure mode this causes today: every session reads ADR-0000 first and designs an
aircraft framework — this session's own initial PR 4 planning was itself an instance, built without
questioning whether the underlying interfaces were vehicle-agnostic enough.

## Decision

Supersede the Purpose paragraph's scope clause. New text:

> A vehicle-agnostic, high-fidelity six-degree-of-freedom simulation **framework and architecture**.
> The framework and its interfaces are the product: it must let a user build a new vehicle, at
> whatever fidelity they need, without editing `core/`. The framework ships no vehicle of its own;
> `aircraft/notional_transport` is the simplest possible worked example proving the public
> interfaces end to end (template → models → trim → linearize → fly against external software), not
> a claim of commercial-transport focus or fidelity — its numbers are notional and do not matter.
> The distinguishing requirement is unchanged: no software models of avionics in the sim — the
> user's real embedded software (C++) runs against the sim, first all-digital on one machine, later
> hardware-in-the-loop on the actual FPGA/Jetson target.

Everything else in ADR-0000's Purpose paragraph and decision table is unchanged (name, distinguishing
requirement, all other rows).

**Part of this decision's own acceptance, not deferred:** `docs/architecture.md` §1's framing
sentence is reworded away from "clone the repo, implement three classes for your aircraft" toward the
vehicle-agnostic framing above (a vehicle plugs in whatever models it needs, at whatever fidelity it
needs) — §1 is the first document a session reads after ADR-0000 and today independently states the
same aircraft-specific framing this decision supersedes. The exact enumeration of what a minimal
vehicle must still implement (today: mass properties, aero, propulsion) is left as-is for now,
revisited once ADR-0021's model-owned-state generalization is reflected in architecture.md — that
count is a consequence of how many model *kinds* exist, not of this decision's own purpose-statement
scope.

## Rejected alternatives

- **Leave ADR-0000 as-is, treat the phrase as informally superseded by convention.** Rejected:
  `AGENTS.md`'s own rule ("Accepted ADRs are immutable: propose a superseding record") exists
  exactly so a reader of ADR-0000 alone gets the true current scope without tribal knowledge of a
  later conversation.
- **Edit ADR-0000 in place instead of a superseding record.** Rejected: violates the
  immutable-accepted-ADR rule directly; ADR-0000 predates the ADR series and is the index record
  every other ADR cites — editing it in place breaks that provenance.

## Consequences and validation

Documentation/intent only — no code, no interface, no behavior change. Benefits: closes the "every
session designs an aircraft framework" failure mode at the root — both places it lived (ADR-0000 and
architecture.md §1), not just the ADR. Costs: the §1 framing edit above, plus any other prose found to
assume commercial-transport framing during that pass. Acceptance: the diff touches this record and
`docs/architecture.md` §1's framing sentence only (not its "three classes" enumeration, deferred per
above); no executable check applies (Tier 0/1, `docs/process/workflow.md`).

## Approval

**Accepted by Joe, 2026-09-20**, after one Codex plan critique (one medium finding: the original
draft's acceptance criteria left architecture.md §1's conflicting framing standing indefinitely,
deferred to an unscheduled follow-up — fixed by folding the §1 edit into this decision's own
acceptance, above). Full critique/revision record: `docs/plans/m3-framework-realignment.md`.
