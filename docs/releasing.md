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
release workflow nor the official repository publication runs, and Fly's update check and
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

## Official Linux distribution

Fly publishes its own packages and repositories. There is no AUR, third-party
store or external package build service in the delivery path. The Chromium
payload is compiled once per supported architecture. `distribution/linux.json`
declares the common package identity, dependency mappings and public URLs.
`scripts/distribution/linux.py` derives DEB, RPM and pacman packages from that
same native tarball; it checks the payload version and binary provenance first.

The release's final job calls `official-packages.yml`. It creates the native
packages, signs them, signs the tarball/checksums, attaches the results to the
release and publishes repository metadata. A release is not fully delivered
until this job and the public repository checks pass. Release candidates do
not replace stable repositories.

### Hosting

GitHub Releases stores the versioned package bodies. A separate prerelease
named `linux-repository` serves a flat signed APT repository and the pacman
database. It never becomes the latest browser release. Old versioned package
bodies remain available for clients holding a previous signed index.

The `gh-pages` branch of `Arzuparreta/fly-browser.github.io` stores small
files under `packages/`: instructions, public keys and signed RPM metadata.
Its own Pages workflow builds a deployment artifact and downloads the current
RPM and two previous signed releases into it, verifying their signed hashes.
Large binaries never enter Git history. Both DNF and libzypp then see a normal
RPM repository with relative package locations; libzypp cannot use the external
package URLs accepted by DNF. The build refuses to exceed its Pages size budget
and leaves the previous deployed site working on failure.

The publisher changes only `packages/`, its two owned Pages workflow/helper
files, and creates a minimal index only if no landing exists. It preserves a
future static landing. Pages must use the Actions build type. Its SSH deploy key
is scoped to that repository only; the browser repository never receives a
general account token. The browser release job waits until the matching public
RPM metadata and package body are available. All public hosting addresses are
in `distribution/linux.json`.

### Signing and key custody

`distribution/release-key.gpg` and `.asc` are public. `fly-install` embeds the
same key so it never obtains its trust root from the package download. The
browser uses the key already installed beside its executable. `gpgv` verifies
signed checksums before a direct installation/update is unpacked. Native
packages use APT's signed Release/InRelease, RPM signatures and signed repomd,
and pacman's package/database signatures. No signature check is disabled.

The signing primary key certifies a separate release signing subkey. Only the
protected signing subkey belongs in GitHub secrets:

- `FLY_RELEASE_SIGNING_KEY`: ASCII-armored secret signing subkey export.
- `FLY_RELEASE_SIGNING_PASSPHRASE`: its passphrase.
- `FLY_PACKAGES_DEPLOY_KEY`: SSH private deploy key for the site repository.

Keep the primary secret key and revocation certificate outside all repositories,
with a protected offline backup. Never put either into Actions. Signing subkeys
expire and must be renewed and shipped to clients in a release signed by the still-valid previous subkey before expiry. Replacing a root key needs a staged
release that distributes both trusted public keys while still signed by the
old key; do not replace the download key unilaterally. Compromise requires
revocation and an independently communicated recovery procedure.

The workflow removes ephemeral signing material even on failure. Weekly runs
reuse and verify published native packages, then renew expiring APT metadata.
They do not rebuild different package contents under the same version.

### Local rehearsal

Build the packaging container, package a native archive, and generate repositories
using a disposable signing key for tests (or the release subkey for production):

```sh
docker build -t fly-linux-packaging -f scripts/distribution/Dockerfile .
# See linux.py --help and its packages/repositories/sign subcommands.
python3 scripts/distribution/validate.py --help
```

Validation must cover clean installation, package-manager updates, removal that
preserves profiles, dependency resolution, desktop entries, sandbox permissions,
and rejection of modified packages and metadata. Container launch checks do not
prove X11/Wayland, GPU, DRM or immutable-desktop acceptance. Keep these limits
explicit instead of describing an untested distribution as supported.

Ordinary PR CI remains `npm test`. Container/package rehearsals and the native
rebuild are release/local work, not new mandatory pull-request checks.
