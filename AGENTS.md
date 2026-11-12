# Repository instructions

- Fly is the renamed, independent continuation of Mise. This repository does
  not track or contribute to Rakosn1cek/Mise-browser: never fetch, merge or
  open pull requests against it.
- For every requested task, create a dedicated git worktree with its own branch
  from the latest `origin/main` before making changes, and do all task work in
  that worktree. A separate branch in the shared checkout is not sufficient.
  Give each parallel agent its own worktree and branch. Never switch branches,
  edit files, reset, clean, or remove worktrees belonging to the owner or another
  agent. Reuse a task's worktree only when continuing that same task.
- Every completed implementation in Fly ends with a git commit. The owner does
  not create these commits manually. Commit only the changes that belong to
  the completed task unless the owner explicitly asks to include others.
- All work reaches `main` through a pull request from a branch, never by
  committing to `main` directly. Any branch name works except `release/v*`,
  which is reserved for version bumps (merging one tags a release).
- Opening or pushing a PR does not complete the task. Wait for every GitHub
  check on its latest remote head to complete successfully. Inspect failures,
  fix their causes, commit and push, then wait again for that new head. Local
  validation alone never satisfies this gate. Once the checks pass, squash-merge
  the PR without asking for further confirmation, delete its branch, then
  clean up only this task's worktree and branch. Update the primary checkout
  to `main` and pull only if it is idle and safe to do so; never switch or modify
  a checkout another agent or the owner is using.
- Before opening a PR, run `git fetch origin` and
  `git merge-base --is-ancestor origin/main HEAD`. If the base moved, bring it
  into the branch, resolve conflicts without dropping either side's changes,
  rerun the checks the combined change needs and push. Repeat the check before
  merging.
- Every pull request carries exactly one impact label, set when you open it:
  `gh pr create ... --label impact:minor`. It answers "what does merging this
  do to someone who upgrades?": `major` they must act by hand, `minor` a new
  capability that still upgrades cleanly, `patch` a fix, `none` nothing a user
  could observe. Release numbers are derived from these labels, so an
  unlabelled pull request ships as a patch whatever it actually did. CI
  rejects a pull request without one.
- A pull request with a `major`, `minor` or `patch` label you open includes a
  `## What's New` section in its description, written for users: what
  changes for them and anything they must do. The release writes the What's
  New page from it (`docs/releasing.md`). It stays optional for pull
  requests the owner opens by hand, which fall back to their title.
- Never write a version number by hand, anywhere. `package.json` declares it,
  `scripts/version-sync.js` copies it where it is needed, and releases are cut
  with `scripts/release.js` (see `docs/releasing.md`, or `/release`).
- The PR description says what changed and why, how it was validated and what
  remains unverified or limited. Report failures and gaps honestly.
- After every runtime or visual change, including each iteration the owner is
  about to try, update the local native build before handing it back so the
  owner can test with the usual `./launch.sh`. Sync this task's overlay into
  the engine checkout with `scripts/chromium/native.py refresh-overlay`,
  configure as required and rebuild `chrome`, including its runtime resources.
  Verify that the default launcher selects that updated build. Updating Git or
  passing `npm test` alone does not make the local browser current. If the build
  is unavailable or fails, report that explicitly. Preserve the owner's running
  session and say when a full restart is needed to load the changes.
  Documentation-only changes do not require a native rebuild.
- New Fly sources and dependencies go into
  `chromium/overlay/fly/integration.gni`, not into new hunks in upstream
  `BUILD.gn` files. Chromium updates are automatic
  (`docs/chromium/engine-updates.md`); when main moved the Chromium pin, bring
  `origin/main` into your branch and run
  `python3 scripts/chromium/engine_update.py upgrade --root /mnt/storage/fly-chromium`.
- Run `npm test` and `git diff --check` before pushing (and `npm run docs:build` when docs change). Actions, their default
  bindings and the Command Centre entries live in
  `chromium/overlay/fly/commands/actions.json` and `commands.json`; after
  changing either, run `node scripts/chromium/generate-commands.js`, which
  writes the C++ catalogue and the keyboard reference. For keyboard UX
  changes, verify the recorder and editable-page behaviour in a Fly build with
  a disposable profile (`--user-data-dir`).
- The owner also opens pull requests by hand, and that must stay effortless:
  one impact label is the only requirement. Keep CI light (`npm test` only),
  and do not add required checks, branch rulesets, mandatory PR template
  sections or workflows that act on ordinary pull requests without asking
  the owner first. Whitespace and the documentation build are local checks.
- Finishing a release includes deleting the local artifacts it left on the
  build host, once the GitHub release and its packages are public: the package
  temporary directory, preserved copies of the release output and, after a
  release that moved Chromium, the candidate slot's `out/FlyRelease`. Keep the
  active root's `out/FlyRelease`: the next release builds incrementally from it.
  Keep `out/FlyNative` and `out/FlyControl` in both roots. See
  `docs/releasing.md#cleaning-up-a-published-release`.

- For questions about Chromium/security updates, paused tracking, or pending
  patches, read `docs/chromium/engine-updates.md` first and follow its status
  checklist. Report Stable freshness and Beta patch compatibility separately;
  `engine_update.py status` alone cannot establish that no work is pending.
  The workflow detects Beta conflicts and opens an issue; it does not repair
  them.

- **“Pon Chromium al día”** (also “actualiza Chromium” or “encárgate del
  seguimiento de Chromium”) invokes the complete maintenance task in
  `docs/chromium/engine-updates.md#pon-chromium-al-día-complete-maintenance-task`.
  It is an instruction to inspect, repair, validate, supervise, merge and
  release as applicable, not merely to report status or enable the workflow.
  It authorizes the task's pushes, PR merges and release publication under the
  existing repository gates, unless the owner explicitly narrows that scope.
