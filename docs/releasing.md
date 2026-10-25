# Versioning and releases

Fly has one product version, independent of the pinned Chromium engine version.
A release ships Fly for Linux x64.

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

`package.json` declares it. The packager reads it there directly.

Two files cannot read it and hold a copy, written by `scripts/version-sync.js`:

```
package-lock.json                    (twice: the root, and packages[""])
chromium/overlay/fly/fly_version.h   (the browser: Preferences, update check,
                                      What's New, diagnostics, GM_info)
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

After an upgrade Fly opens its What's New page (`chromium/overlay/fly/resources/whats_new/`), listing every release since the version that profile last ran. Opened by hand (command palette → Show What's New) it lists the latest release.

Nobody writes it. `prepare` records the release in `whats-new-releases.js` there from the pull requests going out, with `scripts/whats-new.js`, and the bump pull request carries that change. Each pull request labelled `major`, `minor` or `patch` becomes an entry under its title: `major` ones are marked *Action needed*, `minor` and `major` ones get a card each, and `patch` ones are gathered under *Fixes*. `impact:none` is left out.

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

`release.yml` checks the tag against `package.json`, builds the Linux
package and publishes it with `SHA256SUMS.txt`, a per-archive `.sha256` and notes
from merged pull requests. The archive is `Fly-<version>-native-linux-x64.tar.gz`.
It contains `fly-browser/app/` plus `install.sh`. The in-app updater uses
the same archive name and its `.sha256`; `fly-install` checks `SHA256SUMS.txt`.

The build compiles in the Google API key from the repository secret
`FLY_GOOGLE_API_KEY` (`google_api_key` in `out/FlyRelease/args.gn`, never in
the repository). Google Safe Browsing and page translation need it; `release.py
--require-google-api-key` fails the release when the secret is missing. The key
belongs to the owner's Google Cloud project with only the Safe Browsing API
enabled; it is non-commercial and has a daily request quota, visible in that
project's console.

### Build host

Chromium cannot be built within standard hosted runner resource limits. The
release build requires a trusted, dedicated GitHub runner labelled
`self-hosted`, `linux`, `x64`, `fly-chromium`, with Python, Node, Git, the pinned
Chromium host dependencies, sufficient disk/RAM and `FLY_ENGINE_ROOT` pointing
to a dedicated external engine checkout. No runner is provisioned by the workflow.
Without one, releases wait in the build queue and publish nothing. Do not run
untrusted pull requests on this host. Ordinary PR checks remain `npm test`.

The local Linux engine host now has the repository runner
`fly-chromium-local`, labelled `fly-chromium`. Its files are outside the repo at
`/mnt/storage/fly-actions-runner`; the engine checkout is
`/mnt/storage/fly-chromium`. It runs as the owner's user through
`fly-chromium-runner.service`, with `FLY_ENGINE_ROOT` set by that service.
User lingering was already enabled, so the service can start without an
interactive login. The machine must be powered on and connected to receive jobs.

Manage it locally:

```sh
systemctl --user status fly-chromium-runner.service
systemctl --user restart fly-chromium-runner.service
systemctl --user stop fly-chromium-runner.service
journalctl --user -u fly-chromium-runner.service -n 50 --no-pager
```

The user service unit is at
`~/.config/systemd/user/fly-chromium-runner.service`. To disable automatic
startup, run `systemctl --user disable --now fly-chromium-runner.service`.
Runner status and registration are visible in the repository's
[Actions runner settings](https://github.com/Arzuparreta/fly-browser/settings/actions/runners).
Release jobs use it; ordinary PR workflows continue on GitHub-hosted runners.

Prepare the pinned checkout, dependencies, hooks, upstream control build and
full Fly overlay using [Chromium development](./chromium/build.md). The release
job refreshes the managed overlay from the release commit, verifies its pins,
dependencies and fingerprint, regenerates `out/FlyRelease`, builds `chrome` and
`chrome_sandbox`, and invokes `package-linux.py`. It does not upload a preexisting
binary without rebuilding it. Provisioning the host and validating a packaged
browser are separate from the lightweight PR checks.

Local equivalent after preparing the overlay:

```sh
python3 scripts/chromium/native.py refresh-overlay --include-upstream --root /path/to/fly-chromium
python3 scripts/chromium/release.py --root /path/to/fly-chromium --dist /path/to/packages
```

`npm run dist -- --root /path/to/fly-chromium` packages an already built
`out/FlyRelease`; `npm run dist:dir -- --root /path/to/fly-chromium` rebuilds and
packages it. These commands require Linux x64.

Before publishing a change to packaging, rehearse an upgrade using a disposable
installed profile, launch the installed package and verify desktop
integration. Passing `npm test` does not establish that the package works.

## Test builds of pull requests

A pull request can be tried on any Linux x64 computer without building
Chromium there. On the engine host, check out the pull request's branch, pushed
and without local changes, and run:

```sh
python3 scripts/chromium/test-build.py --root /mnt/storage/fly-chromium
```

It builds and packages the branch exactly as a release does and publishes the
package as the GitHub pre-release `test-pr-N` (N is the pull request number),
replacing an earlier test build of that pull request. Anywhere else,
`./launch.sh --pr N` downloads it, checks its SHA-256 and starts it with the
development profile. Export `FLY_GOOGLE_API_KEY` first for a test build with
Safe Browsing; without it the build works but does not check sites.

Test builds never reach users: their tags do not start with `v`, so neither the
release workflow nor the AUR publication runs, and Fly's update check and
`fly-install` read `releases/latest`, which never returns a pre-release. Each
publication, and `python3 scripts/chromium/test-build.py --pr N --remove`
(which deletes that pull request's test build), also deletes the test builds of
pull requests that are no longer open.

The build runs the pull request's code on the engine host with your GitHub
credentials. Pull requests from forks are refused unless `--allow-fork` is
given after reading their code. The checkout must have no uncommitted or
untracked files, because the overlay and the package include them.

On the computers that start test builds, `launch.sh` keeps one download per
pull request in `~/.cache/fly-browser-launch/pr-N` and deletes it once the
test build is gone from GitHub or has not been started for 30 days. A download
that a running Fly uses is never deleted. While a development Fly from one
build is running, starting another build is refused: Chromium would only open
a window in the running one.

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

To test a packaging change before it reaches a release, run the workflow on its branch: `gh workflow run release.yml --ref <branch>`. It builds the Linux package and keeps it as a workflow artefact for three days, and publishes nothing.
