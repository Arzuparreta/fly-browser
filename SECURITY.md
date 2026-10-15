# Security Policy

Fly Browser treats security as a core architectural foundation. This document outlines supported versions, release cadence, security architecture, and procedures for reporting vulnerabilities.

---

## 1. Supported Versions & Update Cadence

Security updates and critical patches are actively maintained for the latest stable release. Because Fly Browser operates on a continuous release cycle without legacy long-term support branches, security fixes are not backported to older releases.

Fly tracks upstream Electron and Chromium releases and audits dependencies twice weekly. Upstream Electron and Chromium security patches are evaluated, integrated, and released as soon as they become available. Each release announcement and tagged milestone explicitly lists its corresponding Electron and Chromium runtime versions.

| Version | Supported | Notes |
| :--- | :--- | :--- |
| **Latest stable release** | Yes | Actively maintained with upstream security updates and bug fixes |
| **Development (`main` branch)** | Best effort | Evaluated and resolved in upcoming releases |
| **Prior releases** | No | Unsupported; please upgrade to the latest release |

---

## 2. Reporting a Vulnerability

The maintainer appreciates the efforts of security researchers and users who report vulnerabilities responsibly:

* **Private Reporting:** Please do **not** report security vulnerabilities via public GitHub issues, discussions, or pull requests.
* **Preferred Channel:** Submit vulnerability disclosures privately via [GitHub Security Advisories](https://github.com/Arzuparreta/fly-browser/security/advisories/new).
* **Alternative Contact:** If GitHub Security Advisories are unavailable, contact the project maintainer directly via GitHub profile details.
* **Information to Include:**
  * Detailed description of the vulnerability and its potential impact.
  * Step-by-step reproduction instructions or a minimal proof of concept.
  * Operating system, architecture, and exact Fly Browser release version.
* **Response Timeline:** The maintainer aims to acknowledge reports within 48 hours and provide a remediation timeline within 7 calendar days.

---

## 3. Security Architecture & Threat Model

Fly Browser is designed with multiple defence-in-depth layers:

### A. Process Isolation & Sandbox Hardening
* **Context Isolation:** Strict context isolation (`contextIsolation: true`) ensures renderer scripts cannot access Node.js or Electron internal APIs directly.
* **Zero Node Integration in Webviews:** All `<webview>` elements run with `nodeIntegration: false`, `nodeIntegrationInSubFrames: false`, and `sandbox: true`.
* **Remote Module Disabled:** Electron's deprecated `enableRemoteModule` is strictly disabled across all windows and webviews.
* **Preload API Boundary:** Communication between web pages and the main process occurs exclusively through a minimal, vetted `contextBridge` whitelist in `preload.js`.

### B. Workspace Container Partitions
* Each workspace operates inside a dedicated, isolated Chromium session partition (`persist:<workspace>`).
* Partitions prevent cross-site and cross-workspace cookie leakage, keeping personal, work, and banking sessions completely segregated.

### C. Clipboard-Mediated Terminal Handshake & Optional Oversight Tool
* **Clipboard Boundary:** Fly never executes terminal commands or shell processes automatically in the background. When terminal actions or script executions are triggered, Fly copies the target command to the user's system clipboard and launches an empty, unmanaged external terminal emulator.
* **Optional Oversight Scanner:** Active command scanning, heuristic script inspection, Shannon entropy checks, and interactive confirmation paging are provided exclusively by [Oversight](https://github.com/Rakosn1cek/oversight), an independent, optional external utility created by the same author.
* **Execution at User's Own Risk:** Oversight is not bundled with Fly. Without the standalone Oversight utility installed in the system PATH, commands staged on the clipboard are pasted and run by the user entirely at their own risk, without automated scanning, heuristic checks, or safety prompts.

### D. Permission Sandboxing
* Sensitive hardware capabilities (microphone, camera, geolocation, and MIDI) are denied by default.
* Web notifications require explicit user opt-in via the interface toggle button.

### E. Request Filtering
* Ad and tracker filtering belongs to the included uBlock Origin extension in selected persistent workspaces. Fly leaves network hooks available to it.
* Disabling extensions or uBlock removes ad blocking. The in-memory private session cannot load extensions and has no ad blocker.

### F. Session & Data Storage Architecture
* **Transparent Plaintext User Data:** Configuration (`config.json`), keybindings (`keybinds.json`), workspaces (`session.json`), history (`history.json`), and bookmarks (`bookmarks.json`) are stored in transparent, human-readable JSON files under `~/.config/fly-browser/`, protected by standard user-only filesystem permissions (`0600`/`0700`). Users retain full ownership to inspect, edit, or back up files using standard tools.
* **OS Keyring Cookie Encryption:** Persistent workspace partitions isolate cookies and web caches per container. Sensitive session tokens written by Chromium are encrypted at rest using `safeStorage`, which binds encryption keys directly to the host operating system's credential keyring (`libsecret` / GNOME Keyring / KWallet on Linux, Apple Keychain on macOS, and DPAPI on Windows).
* **Zero Cloud Synchronisation:** No remote telemetry, no cloud profile syncing, and zero analytics.
* **Surgical Cookie Clearing:** Dedicated cache controls allow wiping tracking tokens for specific domains without invalidating sessions in unrelated tabs.

### G. Credential & Password Management Philosophy
* **Zero Embedded Password Manager:** Fly intentionally excludes built-in password saving, autofill prompts, and credential vaults.
* **Elimination of Malware Targets:** Browser-bundled password vaults (such as Chromium's `Login Data` file) represent the primary attack target for infostealers and credential extraction malware. By omitting internal password storage entirely, Fly removes this high-value vulnerability surface.
* **User Autonomy & External Tools:** Users retain full discretion to employ their preferred dedicated password managers (such as Pass, KeePassXC, Bitwarden, or system keychains) outside the browser's execution perimeter.

---

## 4. Responsible Disclosure Commitment

In return for responsible disclosure, the project maintainer commits to:

* Working collaboratively with researchers to validate and resolve reported issues promptly.
* Crediting researchers in release notes and security advisories (unless anonymity is requested).
* Providing reasonable notice prior to public disclosure of vulnerabilities and fixes.
