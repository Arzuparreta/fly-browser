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

Three files cannot read it and hold a copy, written by `scripts/version-sync.js`:

```
package-lock.json            (twice: the root, and packages[""])
modules/userScriptShim.js    (GM_info.version, injected into pages)
chromium/overlay/fly/fly_version.h   (native Fly: update check, What's New, diagnostics)
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

### Opening a pull request by hand

Add one impact label from the Labels box, and that is all. CI runs `npm test` and nothing more, no check is required to merge, and either merge button works. A pull request merged without a label is not lost: it counts as a patch and is listed by name when the next release is planned. The only reserved branch names are `release/v*`.

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

## The What's New page

After an upgrade Fly opens `assets/whats-new.html`, listing every release since the version that profile last ran. Opened by hand (command palette → Show What's New) it lists the latest release.

Nobody writes it. `prepare` records the release in `assets/whats-new-releases.js` from the pull requests going out, with `scripts/whats-new.js`, and the bump pull request carries that change. Each pull request labelled `major`, `minor` or `patch` becomes an entry under its title: `major` ones are marked *Action needed*, `minor` and `major` ones get a card each, and `patch` ones are gathered under *Fixes*. `impact:none` is left out.

To say more than the title, a pull request adds an optional section to its description:

```markdown
## What's New

### Websites follow the interface theme

Switching between dark and light also switches websites that have their own dark mode.

- Turn it off in Preferences → Appearance → Match websites to the interface.
- Pages without a dark mode are never changed.
```

The `###` line replaces the title, plain lines become the description and `- ` lines the points. Text in backticks shows as a key. The section ends at the next `#` or `##` heading. `plan` lists the pull requests that have no such section, so the title is what readers will see. A release absorbs the entries of its own release candidates, and preparing the same version again replaces its entry.

## What a tag builds

`release.yml` checks the tag against `package.json`, builds every package and publishes one GitHub Release with all of them, a `SHA256SUMS.txt` and notes generated from the merged pull requests, grouped by impact. The packages are unsigned.

| Platform | Packages |
| --- | --- |
| Linux x64 | `Fly-X.Y.Z-x86_64.AppImage`, `Fly-X.Y.Z-x64.tar.gz`, `latest-linux.yml` |
| macOS Apple Silicon | `Fly-X.Y.Z-arm64.dmg`, `Fly-X.Y.Z-arm64.zip` |
| macOS Intel | `Fly-X.Y.Z-x64.dmg`, `Fly-X.Y.Z-x64.zip` |
| Windows x64 | `Fly-Setup-X.Y.Z.exe`, `Fly-X.Y.Z-portable.exe` |

Names carry no spaces, because GitHub renames uploaded files that have them and `SHA256SUMS.txt` would no longer match.

`latest-linux.yml` is what a running AppImage reads to update itself in place: the new AppImage's name, size and SHA-512. A release without it still notifies AppImage users, who then download the file by hand.

The Linux packages carry the desktop entry and the theme icons in `resources/` (`build.linux.extraResources`), for the AUR package to install.

## The AUR package

Once a stable release is published, `release.yml` calls `aur.yml`, which writes `fly-browser-bin`'s `PKGBUILD` and `.SRCINFO` with `scripts/aur.js`, attaches the `PKGBUILD` to the release (so Arch users can `makepkg -si` it without the AUR) and pushes both to the AUR. Both are generated from the version in `package.json` and the tarball's line in `SHA256SUMS.txt`, so nothing is edited per release. Release candidates are not published there.

Pushing needs the `AUR_SSH_PRIVATE_KEY` repository secret, whose public half is registered to the AUR account that maintains the package, and the `AUR_PUBLISH` repository variable set to `true`. Until the variable is set, the job writes the package, notes that it was not pushed and succeeds, so a release is never held back or marked failed by the AUR. Once it is set, a failed push fails the job, because something has broken.

### Activating the AUR package

The secret is already set; the package goes live once the account exists. AUR registration was paused when this was set up (October 2026); it is announced on aur-general and the Arch news feed when it reopens.

1. Register at <https://aur.archlinux.org/register>.
2. In **My Account → SSH Public Key**, add the release workflow's key:

   ```
   ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIMngmlcK3MfT93rjMMQ72gopn2PSJlqginRVwLgWBMws fly-browser release workflow (AUR)
   ```

3. Turn publishing on and publish the current release:

   ```bash
   gh variable set AUR_PUBLISH --body true
   gh workflow run aur.yml -f tag="$(gh release view --json tagName -q .tagName)"
   ```

The first push creates `fly-browser-bin` on the AUR. If the name has been taken in the meantime, change `PKGNAME` in `scripts/aur.js`. If the key is ever lost, generate a new one with `ssh-keygen -t ed25519`, store the private half with `gh secret set AUR_SSH_PRIVATE_KEY`, and register the public half instead of the one above.

To publish a release again, for example after the secret was added or when only the packaging changed, dispatch the workflow: `gh workflow run aur.yml -f tag=vX.Y.Z`, adding `-f pkgrel=2` (and so on) when the version is already on the AUR.

To test a packaging change before it reaches a release, run the workflow on its branch: `gh workflow run release.yml --ref <branch>`. It builds every package and keeps them as workflow artefacts for three days, and publishes nothing.
