# Mise fork working agreements

Follow CONTRIBUTING.md and the user's workflow instructions. This fork does not inherit upstream restrictions on AI-assisted development. Complete authorised commits, pushes, pull requests and merges as requested.

## Personal branch workflow

The owner uses pull requests as a personal work log. For every completed task targeting `personal`, automatically commit the work on a `codex/` branch, push it to `fork`, open a pull request against `personal`, and merge it without requesting further confirmation. Return the local checkout to `personal` and synchronise it after merging. Record validation failures and remaining limits honestly in the PR; failed checks or closed PRs are still useful work-log entries. Do not push completed work directly to `personal` before creating its PR, as that removes the reviewable diff.

For every change in this fork:

- Record each solved problem, including inherited upstream defects, in docs/fork-fixes.md. Explain the original behaviour, the fix and its validation. Keep remaining limits explicit.
- Include the applicable fixes in every PR description using the Fork fixes section of the PR template. Link the ledger entries so that reviewers can distinguish inherited fixes from new features.
- Keep keyboard defaults, the generated guest grammar and the keyboard reference in sync. Run npm test and git diff --check. For keyboard UX changes, verify the recorder and editable-page behaviour in Electron with a disposable profile.
