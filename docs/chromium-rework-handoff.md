# Implementation agent handoff: native Chromium Fly

Status: implementation started at M0. See the
[execution record](./chromium/implementation-status.md) for current source pins,
commands, results and remaining work. The runtime is still Electron.

## Copyable agent brief

Implement Fly's rework onto the complete Chromium browser end to end. Read
`AGENTS.md` and `docs/chromium-rework.md` before making changes. The architecture
decision is Chromium's full browser layer with native Views UI and narrowly
scoped internal WebUI. Do not reopen platform selection or substitute Electron,
CEF, Qt, an iframe shell or `content_shell`.

The owner explicitly accepts a substantial rewrite. Optimise for the resulting
browser's correctness, native Chromium compatibility, resource use and long-term
updates. Reusing current code is optional; preserving its intended product
behaviour, isolation and user data is required.

Execute M0 through M6 in the plan. Do not stop after a spike, an upstream build,
basic navigation, an initial PR or passing JavaScript tests. Routine design and
implementation decisions do not need repeated confirmation. If source inspection
requires an architecture adjustment, implement the appropriate native adaptation,
record the reason and maintain the product invariants.

The owner's explicit rework request covers introducing Chromium, native C++/GN
and its required tooling despite the current contribution guide's JavaScript/
Electron conventions. Update those conventions to describe the finished runtime;
do not ask the owner to approve the already selected platform again. Preserve all
other applicable repository rules, including lightweight ordinary-PR CI.

Resolve these early: one native Fly window with profile-isolated workspaces;
native split-pane visibility and focused-tab routing; extension tab/window IDs
and profile state; and safe legacy data migration. Do not simulate success with
shared cookie stores, forced document visibility, disabled security or a stock
Chromium UI carrying only Fly branding.

Use official Chromium source, an exact supported source pin, an external/ignored
checkout, a reviewable Fly overlay/patch stack and reproducible scripts. Never
fetch or contribute to the former Mise browser repository. Keep product version
metadata generated from `package.json`, separate from the actual engine version.

Build deterministic native/browser/desktop acceptance tests and run real packaged
applications with disposable profiles. Test the reported Twitch/Pie pause issue,
native visibility, keyboard recorder/editable behaviour, workspace isolation,
extensions, filtering, data migration, private mode, recovery and delivery. Retain
the original profile; never test against or migrate the owner's live data without
authorisation. Do not put secrets or personal browsing data in logs or commits.

Preserve current Fly workflows. Handle removed legacy extension support honestly:
implement and verify a maintained native-rule blocker, preserve disabled/removed
choices and original settings, and disclose unsupported imports or required
re-login. Do not freeze an insecure engine to claim extension parity.

Ship buildable, installable native packages and working launcher/installer/update
integration for the platforms Fly currently releases. Rehearse both a product
update from installed Electron Fly and a Chromium engine update. Keep ordinary PR
CI light and follow all repository requirements; this brief does not authorise
new required checks, branch rules, paid infrastructure or public release publishing.

Commit coherent changes on non-release implementation branches. Before every push
run the required tests; before opening and merging PRs fetch origin and check that
`origin/main` is an ancestor. Wait for every check on the latest remote head,
repair failures, squash-merge without another confirmation, delete the branch and
finish on pulled `main`. Use exactly one impact label and user-facing What's New
notes when applicable. Never hand-write a product version bump.

The definition of done is section 10 of the plan. Report actual artefacts,
commands, test/benchmark evidence, migration results and remaining limitations.
If externally blocked, preserve the implementation and exact restart instructions;
state what cannot be completed. Do not call incomplete platform, migration or
package validation a finished end-to-end port.

## Execution record to create and maintain

Create `docs/chromium/implementation-status.md` during M0. It must contain enough
state for a different agent to resume without this chat:

- Current implementation branch/commits and PR URLs.
- Actual pinned upstream/depot_tools source SHAs and the generated lock location.
- Engine checkout, output directory and relevant running build/test processes.
- Exact setup/build/run/test/package commands that have actually been exercised.
- Complete action/preference/feature parity ledger mapped to source and tests.
- Workspace/window/profile topology and decisions that changed this plan.
- Migration schema, transaction state, test inputs and rollback procedure.
- Per-milestone and A01–A22 results: pending, pass, fail or blocked, with evidence.
- Packaged artefact/checksum/provenance locations and tested platforms.
- Baseline/control/native benchmark inputs, raw outputs and limitations.
- Next concrete action and any external blocker; no credentials or live user data.

The following checklist is initially unimplemented; mark work complete only with
evidence in that execution record.

- [ ] M0: upstream control, reproducible tooling and complete baseline contracts.
- [ ] M1: native Fly shell, isolated single-window workspaces, split and extension topology.
- [ ] M2: complete commands, keyboard/mouse UI, preferences and local product data.
- [ ] M3: native services, extensions/blocking, scripts/styles and lifecycle.
- [ ] M4: transactional import, interruption/crash recovery and rollback.
- [ ] M5: all shipped-platform packages, installer/update handoff and engine update rehearsal.
- [ ] M6: completed acceptance evidence, documentation, default cutover and merged delivery.

## First actions

1. Inspect the current checkout and applicable instructions; preserve unrelated
   work. Compare it with the planning baseline named in the plan.
2. Read the product inventory and current actions/settings/persistence loaders.
   Create the parity ledger and execution record before replacing runtime code.
3. Inspect available build resources, choose a supported exact Chromium revision,
   and implement reproducible bootstrap/control build commands.
4. Build the unmodified control, then prove M1's multi-profile native topology.
   Continue into the remaining implementation, using that evidence to guide it.

This document is a handoff, not a claim that the proposed scripts, components or
native tests already exist. Existing repository checks must remain valid while
the new tooling and native coverage are implemented.
