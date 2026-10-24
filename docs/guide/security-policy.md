# Security policy

Report vulnerabilities privately through [GitHub Security Advisories](https://github.com/Arzuparreta/fly-browser/security/advisories/new).
Include reproduction steps, the Fly product version, engine version and
operating system. Avoid posting credentials, cookies or personal browsing data
in public issues.

## Runtime and boundaries

Fly is the full Chromium browser with its own Views interface. Chromium
provides process sandboxing, site isolation, permissions, extension security
and profile storage. Launchers do not disable the sandbox. Fly's code and
WebUI bridges are privileged and must validate data from pages and extensions.
Each workspace owns a separate Chromium profile; private workspaces use
off-the-record profiles. Isolation does not protect against a compromised OS.

Fly bundles uBlock Origin Lite using declarative network rules. Local user
scripts/styles run through a generated component extension and can access
matching pages; install only trusted content. Network grants and destinations
must be checked, including redirects and local addresses.

## Updates and local data

The engine source is pinned in `chromium/upstream.lock.json`; product metadata
comes from `package.json`. Engine updates need a build and acceptance
validation. Do not equate a passing npm audit with a patched Chromium engine.
Linux archives carry SHA-256 checksums. Verify downloaded packages; checksums
do not replace authenticated distribution or code signing.

Fly stores data under `$XDG_CONFIG_HOME/fly-browser` (default
`~/.config/fly-browser`). Chromium manages profile preferences, cookies and
extension storage; encryption depends on the host and Chromium configuration.
Backups may contain sensitive browsing data. Do not run tests against a live
personal profile.

Releases are built for Linux x86_64 only.
