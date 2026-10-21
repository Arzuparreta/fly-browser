# Security policy

Report vulnerabilities privately through [GitHub Security Advisories](https://github.com/Arzuparreta/fly-browser/security/advisories/new).
Include reproduction steps, the Fly product version, engine version, operating
system and whether the issue affects native Chromium or Electron legacy. Avoid
posting credentials, cookies or personal browsing data in public issues.

## Runtime and boundaries

Fly's primary runtime is the full Chromium browser with native Views UI.
Chromium provides process sandboxing, site isolation, permissions, extension
security and profile storage. Launchers do not disable the sandbox. Native code
and WebUI bridges are privileged and must validate data from pages/extensions.
Each workspace owns a separate Chromium profile; private workspaces use
off-the-record profiles. Isolation does not protect against a compromised OS.

Fly bundles uBlock Origin Lite using native declarative network rules. Local
user scripts/styles run through a generated component extension and can access
matching pages; install only trusted content. Network grants and destinations
must be checked, including redirects and local addresses.

## Updates and local data

The engine source is pinned in `chromium/upstream.lock.json`; product metadata
comes from `package.json`. Engine updates need native build and acceptance
validation. Do not equate a passing npm audit with a patched Chromium engine.
Native Linux archives carry SHA-256 checksums. Verify downloaded packages;
checksums do not replace authenticated distribution or code signing.

Native Fly stores data under `$XDG_CONFIG_HOME/fly-browser`. Chromium
manages profile preferences, cookies and extension storage; encryption depends
on the host and Chromium configuration. Backups may contain sensitive browsing
data. The legacy importer only reads Electron source directories and writes to
the separate native profile. Do not run tests against a live personal profile.

Electron remains for comparison and recovery, with explicit legacy commands.
It is no longer distributed by the active release workflow. Native macOS and
Windows packages are not yet supported. See the implementation record for
remaining validation gaps; do not infer platform support from legacy downloads.
