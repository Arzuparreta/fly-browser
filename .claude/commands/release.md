---
description: Cut a Fly release from the impact labels of what has merged
---

Release Fly. The version number is derived, never invented: it comes from the
`impact:` labels on the pull requests merged since the last tag.

$ARGUMENTS may contain `--rc` (cut a release candidate) or `--version X.Y.Z`
(override the derived number, only for deliberate moves like reaching 1.0).

1. `node scripts/release.js plan $ARGUMENTS`: show the user the number and
   what is going into it. If any merged pull request has no impact label, say
   so plainly: it is being counted as a patch and that may be wrong.
2. If `assets/whats-new.html` still describes an older version, say so: it is
   the page Fly opens after an upgrade and nothing writes it automatically.
3. `node scripts/release.js prepare $ARGUMENTS`: opens the bump pull request,
   waits for its checks and merges it. Run it in the background; the checks
   take a few minutes.
4. Merging is the release: `release-tag.yml` tags the merge commit and starts
   `release.yml` on that tag. Check it with
   `gh run list --workflow release-tag.yml --limit 1`, then wait for the
   release run. Only if the tagging job did not run,
   `node scripts/release.js finish` tags from this checkout instead.
5. Return the checkout to `main` and pull.

Report the release URL when it exists. Do not hand-edit a version anywhere:
every copy of it is written by `scripts/version-sync.js`.
