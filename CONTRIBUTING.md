# Contributing to Fly Browser

Thank you for your interest in contributing to Fly Browser! Fly is an independent, keyboard-first web browser engineered specifically for low-resource and fanless Linux hardware, featuring container isolation, true tab hibernation, and zero background bloat.

To preserve the speed, safety, and minimalism of the project, all contributions are expected to adhere to the guidelines set out below.

***

## 1. Core Principles

Before proposing features or submitting pull requests, please keep Fly's foundational principles in mind:

* **Low Resource Footprint:** Every feature must respect system resources. Background timers, heavy libraries, and persistent memory allocations are strictly avoided.
* **Keyboard-First Workflow:** Navigation, workspace management, and settings should be fully operable via keyboard shortcuts and modal states (NORMAL, INSERT, PASSTHROUGH).
* **Minimalist Architecture (KISS):** Keep implementations simple and maintainable. Prefer platform primitives and explicit ownership.
* **Minimal Dependencies:** Avoid introducing new external dependencies. The approved [Chromium rework](docs/chromium-rework.md) authorises Chromium and its native build toolchain. Discuss unrelated new dependencies with the maintainer prior to opening a PR.
* **Strict Privacy & Zero Telemetry:** Fly never collects telemetry, user analytics, or background metrics. User data remains strictly local.

***

## 2. Reporting Issues

Please use the structured GitHub issue forms to report problems:

* **Bug Reports:** Use the [Bug Report Form](.github/ISSUE_TEMPLATE/bug_report.yml) to report crashes, unexpected behaviour, or UI glitches. Please include your distribution, window manager (e.g. Hyprland, Sway, i3), display server (Wayland or X11), and relevant logs from the Diagnostics Hub.
* **Website Breakdowns:** If an external site fails due to adblocking shields, Dark Reader colour inversion, or container authentication loops, submit a [Website Compatibility Report](.github/ISSUE_TEMPLATE/site_compatibility.yml).
* **Feature Proposals:** Submit ideas via the [Feature Request Form](.github/ISSUE_TEMPLATE/feature_request.yml), describing the proposed keyboard workflow and performance considerations.
* **Security Disclosures:** Do not report vulnerabilities publicly. Please refer to the [Security Policy](SECURITY.md) and disclose privately via [GitHub Security Advisories](https://github.com/Arzuparreta/fly-browser/security/advisories/new).

***

## 3. Development Setup

### Prerequisites
* Linux (recommended), macOS, or Windows
* Node.js (version 20 LTS or newer)
* npm package manager
* Git

### Local Setup Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/Arzuparreta/fly-browser.git
   cd fly-browser
   ```

2. Install development dependencies:
   ```bash
   npm install
   ```

3. Launch Fly Browser in development mode:
   ```bash
   npm start
   ```

Native Chromium/Views is the default runtime. `npm start` launches a prepared
engine checkout, not Electron; set `FLY_ENGINE_ROOT` and optionally
`FLY_ENGINE_OUTPUT`. Follow [Chromium development](docs/chromium/build.md) for
the explicit control/native builds and tests. Native release packaging currently
supports Linux x64. `npm run start:legacy` and `npm run dist:legacy` preserve
Electron comparison workflows; see [legacy](legacy/electron/README.md).

***

## 4. Coding Standards & Style

Contributions must follow these conventions:

* **Language & Runtime:** Legacy Electron frontend modules use standard ES modules and backend code uses CommonJS. The primary native implementation uses Chromium C++, GN, Views and narrowly scoped typed WebUI/Mojo interfaces. Build tooling may use Python and Node.js. Follow the pinned upstream conventions and keep external source/build outputs outside this repository.
* **UK English:** All code comments, documentation, user-facing text, and issue messages must use British English spelling (e.g. `colour`, `customise`, `initialise`, `behaviour`, `optimise`, `sanitised`).
* **Concise Code Notes:** Keep notes within code human-readable, purposeful, and concise. Avoid redundant commentary restating self-explanatory logic.
* **Security Boundaries:**
  * Webviews must always maintain `contextIsolation: true`, `nodeIntegration: false`, and sandbox protections.
  * IPC messaging must follow existing whitelists in `preload.js` without exposing direct Node.js handles.
  * File and URL inputs must be vetted to prevent scheme traversal or unsanitised execution.
  * Native integration must retain Chromium sandboxing, site isolation and profile-scoped extension services. Internal WebUI capabilities must be origin-bound and typed. Do not replace web visibility APIs or reassign a live tab to another profile.

***

## 5. Verification & Testing

Before opening a pull request:

1. Run the regression suite, which also checks generated files and that every copy of the version agrees:
   ```bash
   npm test
   git diff --check
   ```

2. If you touched the documentation, build it:
   ```bash
   npm run docs:build
   ```

3. Run Fly (`./launch.sh`) and exercise what you changed. Without a local engine build, `./launch.sh` starts the published release. When the maintainer has published a test build of a pull request ([test builds](docs/releasing.md#test-builds-of-pull-requests)), `./launch.sh --pr N` starts it on any Linux x64 computer. Say in the pull request which platform and display server you tested on, and what you could not test.

***

## 6. Submitting Pull Requests

Opening a pull request by hand is the normal path, and it needs one thing: **an impact label**. Add it from the Labels box while creating the pull request, or right after:

| Label | Use it when |
| --- | --- |
| `impact:patch` | it fixes something (the usual one) |
| `impact:minor` | it adds something new |
| `impact:major` | people must do something by hand after upgrading |
| `impact:none` | nobody using Fly would notice (docs, CI, tests) |

That label is how the next version number is chosen; see [Versioning and releases](docs/releasing.md). The template's sections are there to help, not required. Nothing else is asked of you:

* Any branch name works except `release/v*`, which is reserved for version bumps.
* Merge with whichever button you like (squash or merge commit).
* CI runs `npm test` only. A missing label shows a red `impact_label` check until you add one; nothing blocks merging.
* Never edit a version number by hand: releases set it.
* The What's New page Fly opens after an upgrade is written from merged pull requests when a release is prepared. Your title is what users see there; to say more, add an optional `## What's New` section to the description ([format](docs/releasing.md#the-whats-new-page)).

***

## 7. Community & Questions

For questions, configuration problems or ideas, open an [issue](https://github.com/Arzuparreta/fly-browser/issues).
