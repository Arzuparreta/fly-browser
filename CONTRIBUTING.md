# Contributing to Mise Browser

Thank you for your interest in contributing to Mise Browser! Mise is an independent, keyboard-first web browser engineered specifically for low-resource and fanless Linux hardware, featuring container isolation, true tab hibernation, and zero background bloat.

To preserve the speed, safety, and minimalism of the project, all contributions are expected to adhere to the guidelines set out below.

***

## 1. Core Principles

Before proposing features or submitting pull requests, please keep Mise's foundational principles in mind:

* **Low Resource Footprint:** Every feature must respect system resources. Background timers, heavy libraries, and persistent memory allocations are strictly avoided.
* **Keyboard-First Workflow:** Navigation, workspace management, and settings should be fully operable via keyboard shortcuts and modal states (NORMAL, INSERT, PASSTHROUGH).
* **Minimalist Architecture (KISS):** Keep implementations simple and maintainable. Avoid complex abstractions when straightforward web or Electron primitives suffice.
* **Minimal Dependencies:** Avoid introducing new external dependencies. If a new dependency is required, discuss it with the maintainer prior to opening a PR.
* **Strict Privacy & Zero Telemetry:** Mise never collects telemetry, user analytics, or background metrics. User data remains strictly local.

***

## 2. Reporting Issues

Please use the structured GitHub issue forms to report problems:

* **Bug Reports:** Use the [Bug Report Form](.github/ISSUE_TEMPLATE/bug_report.yml) to report crashes, unexpected behaviour, or UI glitches. Please include your distribution, window manager (e.g. Hyprland, Sway, i3), display server (Wayland or X11), and relevant logs from the Diagnostics Hub.
* **Website Breakdowns:** If an external site fails due to adblocking shields, Dark Reader colour inversion, or container authentication loops, submit a [Website Compatibility Report](.github/ISSUE_TEMPLATE/site_compatibility.yml).
* **Feature Proposals:** Submit ideas via the [Feature Request Form](.github/ISSUE_TEMPLATE/feature_request.yml), describing the proposed keyboard workflow and performance considerations.
* **Security Disclosures:** Do not report vulnerabilities publicly. Please refer to the [Security Policy](SECURITY.md) and disclose privately via [GitHub Security Advisories](https://github.com/Rakosn1cek/Mise-browser/security/advisories/new).

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
   git clone https://github.com/Rakosn1cek/Mise-browser.git
   cd Mise-browser
   ```

2. Install development dependencies:
   ```bash
   npm install
   ```

3. Launch Mise Browser in development mode:
   ```bash
   npm start
   ```

***

## 4. Coding Standards & Style

Contributions must follow these conventions:

* **Language & Runtime:** Pure JavaScript. Frontend browser modules in `modules/` use standard ES modules. Node.js backend processes (`main.js`, `preload.js`, `security.js`, `keybinds.js`) use standard CommonJS.
* **UK English:** All code comments, documentation, user-facing text, and issue messages must use British English spelling (e.g. `colour`, `customise`, `initialise`, `behaviour`, `optimise`, `sanitised`).
* **Concise Code Notes:** Keep notes within code human-readable, purposeful, and concise. Avoid redundant commentary restating self-explanatory logic.
* **Text Formatting:** Do not use double dashes in text, comments, or documentation files.
* **Security Boundaries:**
  * Webviews must always maintain `contextIsolation: true`, `nodeIntegration: false`, and sandbox protections.
  * IPC messaging must follow existing whitelists in `preload.js` without exposing direct Node.js handles.
  * File and URL inputs must be vetted to prevent scheme traversal or unsanitised execution.
* **Verification:** Changes must be reviewed and validated with relevant tests. State any unverified desktop or platform behaviour clearly.

***

## 5. Verification & Testing

Before submitting a Pull Request, verify that your changes meet quality checks:

1. **Syntax Validation:**
   Ensure modified JavaScript files parse cleanly without syntax errors:
   ```bash
   node -c main.js
   node -c renderer.js
   node -c preload.js
   ```

2. **Documentation Build:**
   If modifying documentation, verify that the VitePress documentation builds cleanly:
   ```bash
   npm run docs:build
   ```

3. **Manual Verification:**
   Test your changes on a physical desktop environment (such as Linux under Wayland or X11) to confirm that layout rendering, keyboard shortcuts, and webview tabs operate properly.

***

## 6. Submitting Pull Requests

1. Create a descriptive feature branch from `main`:
   ```bash
   git checkout -b feat/your-feature-name
   ```

2. Commit your changes using Conventional Commits:
   * `feat:` for new capabilities
   * `fix:` for bug fixes
   * `docs:` for documentation updates
   * `style:` for formatting or visual adjustments
   * `refactor:` for code restructurings
   * `perf:` for performance or memory optimisations
   * `ci:` for continuous integration updates

3. Push your branch and open a Pull Request against `main`. Complete the checklist in the [Pull Request Template](.github/PULL_REQUEST_TEMPLATE.md).

***

## 7. Community & Questions

For general discussion, questions regarding configuration, or sharing tips, please visit [GitHub Discussions](https://github.com/Rakosn1cek/Mise-browser/discussions).
