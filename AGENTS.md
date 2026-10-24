# Repository instructions

- Fly is the renamed, independent continuation of Mise. This repository does
  not track or contribute to Rakosn1cek/Mise-browser: never fetch, merge or
  open pull requests against it.
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
  switch the local checkout back to `main` and pull.
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
