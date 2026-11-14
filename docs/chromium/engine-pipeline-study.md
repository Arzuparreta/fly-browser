# Engine pipeline performance: deferred investigation

**Status: research backlog, not an approved implementation.** The owner asked
on 14 November 2026 to leave this handoff for another agent to investigate,
design and test **after the current maintenance/release work is finished**.
Do not start a pipeline rewrite as part of closing the current Chromium repair.
This document changes no automation, validation requirement or release gate.

## Objective

Reduce the time to discover Chromium incompatibilities and the unnecessary
work repeated between repair attempts. Preserve Fly behavior, independent
review, complete final native acceptance, isolation and release provenance.
Measure elapsed time and avoided work before claiming a saving.

Read [Engine updates](./engine-updates.md), [Releasing](../releasing.md),
`AGENTS.md` and current merged source first. The implementation may have moved
since this handoff; an open PR or historical log is not repository truth.

## Evidence motivating the study

The November maintenance sequence exposed several distinct costs:

| Observation | Evidence and interpretation |
| --- | --- |
| First preparation of a separate Beta control was expensive | The local pristine control log recorded 57,485 steps and about 2 h 14 min. This establishes a cold preparation cost, not a cost for every later attempt. |
| A Fly API error appeared late in a large Beta build | [Run 43591113964](https://github.com/Arzuparreta/fly-browser/actions/runs/43591113964) failed on renamed window-disposition members after 53,803 completed steps and about 1 h 56 min, with 4,986 remaining. Patch replay had already passed. |
| Compiling Fly callers together exposed adjacent errors earlier | A later local pass requested all Fly UI object files and the browser-test translation unit, with keep-going enabled. It found the disposition migration, a pointer-container rule, a removed sidebar-host API and a test string-conversion mismatch without waiting for another complete browser build. |
| Successful Stable acceptance was repeated across repair cycles | The run artifacts contain passing Stable native suites followed by a Beta failure. Some repeats were necessary because shared source changed; determine which had identical effective Stable inputs rather than assuming every repeat was waste. |
| Warm outputs substantially reduced later compilation work | A later local Beta build recorded 3,551 steps in about 17 min 27 s. It followed repairs and earlier partial builds, so it is not a controlled cold/warm benchmark or a predicted saving. |
| Separate local and workflow validation can duplicate acceptance | Manual investigation used disposable combined candidates; the workflow subsequently validates the actual PR/base. Those results are not automatically interchangeable without an exact provenance contract. |

Historical diagnostics are in the `engine-repair-diagnostics-*` artifacts of
[run 43582525333](https://github.com/Arzuparreta/fly-browser/actions/runs/43582525333),
[run 43587061461](https://github.com/Arzuparreta/fly-browser/actions/runs/43587061461)
and run 43591113964. Artifacts have limited retention. On the build host,
`/mnt/storage/fly-codex-repair-evidence/` contains saved copies, local native
logs and experiment scripts; inventory them before relying on their presence.
Do not treat those experimental scripts as production controllers.

## Current implementation to inspect

- `.github/workflows/engine-update.yml` and
  `scripts/chromium/engine-update-ci.sh`: Stable/Beta orchestration, statuses,
  proposal attempts, merge/freshness checks, issue closure and publication.
- `scripts/chromium/agent_repair.py`: scope, strict replay, independent review,
  native validation and the complete-result certificate.
- `scripts/chromium/native.py`: overlay journals, fingerprints, object builds,
  native builds and verification of expected browser results.
- `scripts/chromium/engine_update.py`: source preparation, incremental upgrade,
  root leases, control/native stages and the owned test display.
- `scripts/chromium/repair_agents.py`, `repair_tools.py` and
  `repair_sandbox.py`: provider policy, tool isolation and native namespaces.

At this handoff, `validation_key()` includes full HEAD/base trees and trusted
validator hashes. A complete certificate is written only after both channels
pass. A later Beta failure therefore leaves no reusable complete certificate
for the successful Stable phase. The explicit repair-retry setting also
bypasses complete-certificate reuse. Verify these details in current main.

## Options to investigate

### 1. Compile the compatibility boundary before the full browser

After trusted source preparation and configuration, compile Fly's actual
translation units, affected tests and the upstream translation units changed
by its patches before requesting the full browser/link. Collect independent
compiler errors in one bounded pass so related API changes can be repaired
together. This is a diagnostic stage; final full builds and tests still run.

Use the existing pin's `out/FlyNative` configuration and Siso graph so useful
work remains incremental. Investigate GN/Siso target discovery rather than
hard-coded object paths, enum names or Chromium versions. Include generated
header prerequisites and distinguish configuration, compile and link failures.
Compare this with the existing `test-objects` mechanism: it uses a separate
output and is not automatically a replacement for the final native build.

### 2. Retain successful proof separately for each phase

Design controller-owned receipts for strict replay, independent review,
per-pin native builds and test phases. Reuse a success only when all relevant
immutable inputs and assumptions match. Investigate at least:

- Frozen pin, full dependency/toolchain state, exact effective patched sources
  and overlay, GN arguments and runtime resources.
- Tests, expected case inventory, filters, launcher configuration, validator
  code, isolation policy and relevant host/test environment.
- Review scope and base: a review of a different effective diff is not approval.
- Root ownership, source verification and post-run input consistency.

Record results outside model/build-writable locations; publish success
atomically only after controller verification. Failed, partial, stale,
interrupted or forged receipts must not authorize a skip or a green status.
Investigate whether documentation-only base movement can retain native proof
while still rerunning GitHub checks and live merge/base checks.

Separate “allow another bounded proposal” from “force native acceptance again”
only if that distinction is explicit, tested and visible in diagnostics.
Changed shared C++ must invalidate both affected channels. Reuse Stable after a
Beta-only repair only when Stable's relevant effective inputs really match.

### 3. Make local investigation and workflow acceptance use one contract

Explore one trusted controller for the final immutable combined candidate.
It should identify what is under test before expensive work begins and expose
the same phase evidence locally and in Actions. A later workflow could reuse
that evidence only after checking the exact contract; manually marking
`engine-validation` green is not an alternative.

During iteration, use targeted builds/tests to answer concrete questions and
group related source/test adaptations. Run complete Stable/Beta acceptance
on the final candidate. Preserve baseline-test protection: a bounded model
must still stop when a fix requires protected files; that is a distinct manual
repair path, not permission to weaken assertions or widen the model's scope.

## Experiments and acceptance criteria

Capture a reproducible baseline and proposed result for each case:

| Case | Required result |
| --- | --- |
| Cold pin, first build | Full preparation/acceptance runs; report its unavoidable cost separately. |
| Unchanged candidate, warm outputs | Receipts skip only phases whose complete contract matches. |
| Stable passed, Beta failed | A later attempt reuses Stable only if its effective inputs are identical. |
| Shared overlay, patch, test or resource changed | Every affected phase is invalidated; deliberate failures remain visible. |
| Toolchain, GN args, validator or sandbox changed | Stale proof cannot be accepted. |
| Documentation-only main movement | Live base/head checks still run; any retained native proof is justified explicitly. |
| Interrupted build or controller | Incremental outputs survive safely; incomplete receipts never pass. |
| Corrupted or model-written receipt | Fail closed and record why it was rejected. |
| Busy root or overlapping workflow | Defer through leases; preserve the browser session and avoid duplicate heavy jobs. |
| Quota/availability failure or rejected review | Preserve the provider policy and rejection semantics; caching cannot turn either into approval. |
| Beta advances, active Stable lags, or checks/reviews block merge | Freshness and remote gates block merge regardless of cached native proof. |
| Final merge/publication | All current gates, issue lifecycle, active-build verification and release provenance remain satisfied. |

Measure time to first actionable failure, configuration/compilation/link/test
time, repeated translation units, repeated test phases, review duration, cache
hits/misses with reasons, and total wall time. Compare equal inputs and build
conditions. Report cold and warm behavior separately; do not infer savings from
different revisions or partial failed builds.

Use focused controller/cache tests with local fixtures, deliberate invalidation
and tampering cases, then representative real native Stable/Beta runs. Do not
run duplicate long jobs against occupied roots just to obtain measurements.

## Constraints and rollout

- Keep Stable security updates independent of a blocked Beta preparation.
  Beta never becomes the shipping pin merely to clear a warning.
- Preserve strict scope/replay, independent review, full native acceptance,
  live freshness checks, all latest-head remote checks and safe issue closure.
- Keep Claude Opus 5.5 high first and Codex Sol 6.1 high through the existing
  configured proxy as the availability fallback. Rejected code/tests are not
  a provider-fallback signal. Preserve logins and global T3/proxy settings.
- Keep engine roots isolated and leased. Control, native and release outputs
  have distinct configurations; their binaries cannot simply be exchanged.
- Preserve owner worktrees, running sessions, profiles and incremental caches.
  Keep ordinary PR CI light and add no required checks or branch rules.
- Do not claim a hardware/job-count/compiler-cache saving without comparable
  measurements. Siso already provides incremental builds.

Start with an investigation report and a concrete design, including cache
invalidation/trust boundaries and measured expected benefit. Introduce changes
in small reviewed PRs. Consider recording proposed cache decisions without
skipping work first, compare with full validation, then enable reuse only after
the negative tests and native experiments establish correctness. Every cache
decision must remain explainable and a full-validation mode must remain usable.

The next agent should deliver the report, experiment data, design alternatives,
regression tests, rollout/recovery plan and limitations. This handoff authorizes
documenting the future study; implementation remains for that later task.
