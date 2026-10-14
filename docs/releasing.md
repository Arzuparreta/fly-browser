# Versioning and releases

Fly has one version number. The Linux, macOS and Windows packages are built from the same commit and released together, so `0.16.0` means the same thing however you installed it, and a bug report only ever has one number to quote.

## What the number promises

The version describes what happens to **you** when you upgrade, not how much code changed.

| Change | Means |
| --- | --- |
| **MAJOR** | Upgrading requires you to do something by hand: move a profile, rewrite a configuration file, or relearn a removed workflow |
| **MINOR** | New capability. Upgrading is still installing the new package and nothing else |
| **PATCH** | Fixes only |

While Fly is below 1.0 there is no major number to spend, so **minor carries what major will carry later**: a 0.x minor may ask something of you. A patch never does.

`X.Y.Z-rc.N` is a release candidate: built and published like any other release, but marked as a pre-release. The in-app update check reads GitHub's latest release, which never returns a pre-release, so nobody is offered a candidate they did not go looking for.

## Where the version lives

`package.json` declares it. That is the only place it is decided. Electron reports it through `app.getVersion()`, so Preferences, the Welcome view, diagnostics reports and the update check all read it from there, and electron-builder names every package after it.

Two files cannot read it and hold a copy, written by `scripts/version-sync.js`:

```
package-lock.json            (twice: the root, and packages[""])
modules/userScriptShim.js    (GM_info.version, injected into pages)
```

Copies drift, so `npm test` runs `node scripts/version-sync.js --check`, and fails if any copy disagrees. The release workflow refuses a tag that names a different version from the tree it points at.

## How the number is chosen

By the pull requests, as they merge. Every pull request carries exactly one impact label, added when it is opened:

```
impact:major   they have to do something by hand to upgrade
impact:minor   new capability, upgrading still needs nothing else
impact:patch   a fix
impact:none    nothing a user could observe (docs, CI, tests)
```

The `impact_label` check rejects a pull request without one. Releasing takes the highest label merged since the last tag and moves the number accordingly: nobody rereads a month of diffs trying to remember whether something was a feature. A pull request merged without a label counts as a patch, and the release plan lists it by name so the claim can be checked.

## Cutting a release

```bash
node scripts/release.js plan       # what would go out, and as what number
node scripts/release.js prepare    # opens the bump PR, waits for its checks, merges it
```

Merging the bump pull request is what publishes. `release-tag.yml` tags the merge commit with `vX.Y.Z` and starts `release.yml` on that tag. A tag pushed with `GITHUB_TOKEN` starts nothing by itself, so it dispatches the workflow. To hold a release for review, pass `--hold` to `prepare`: the pull request then goes out when someone merges it.

If the tagging job fails part-way, rerun it: it starts the release workflow only if that has no run on the tag yet. `node scripts/release.js finish` is the manual fallback for when it did not run at all: it tags the merged commit from a checkout, and its push starts the release workflow.

In Claude Code, `/release` runs these steps and waits between them.

`prepare` runs from a checkout rather than from Actions because a push made with `GITHUB_TOKEN` starts no workflows: a bot-authored bump would never receive its checks.

`--rc` cuts a release candidate. `--version X.Y.Z` overrides the derived number, which is how 1.0 will eventually happen: a label cannot decide that.

## What a tag builds

`release.yml` checks the tag against `package.json`, builds the Linux AppImage and tarball, the macOS disk image and zip, and the Windows installer and portable executable, and publishes one GitHub Release with all of them, a `SHA256SUMS.txt` and notes generated from the merged pull requests, grouped by impact. The packages are unsigned.
