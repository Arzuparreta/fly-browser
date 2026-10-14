# Terminal Security & Oversight

Fly includes a clipboard-mediated terminal handshake designed to protect users from malicious web scripts, hidden pastejacking commands, and unverified remote code execution.

***

## The Risk of Web Terminal Execution

Many developer documentation sites and tutorials feature one-click terminal commands (such as `curl | bash` or complex setup one-liners). Malicious websites frequently employ pastejacking techniques, injecting invisible newlines or hidden payloads into the system clipboard that execute immediately upon pasting into a terminal window.

***

## How the Clipboard-Mediated Handshake Works

When you select a script or run command within Fly:

1. **Script Extension & Risk Inspection**:
   Fly's internal parser identifies commands containing executable script extensions or high-risk system prefixes:
   ```text
   .sh, .bash, .zsh, .ksh, .fish, .py, .rb, .pl, .lua, .c, .rs, .go, .js, .ts
   sudo, curl, wget, raw web URLs
   ```

2. **Decoupled Clipboard Staging**:
   - Rather than executing the script directly in a background shell process or passing it blindly to a child process, Fly stages the command onto your system clipboard.
   - If risky patterns are detected, Fly prefixes the staged clipboard text with `oversight` to trigger the external scanner if present.
   - Fly then spawns a fresh, unmanaged, empty terminal instance.

3. **Manual Execution**:
   - Because the terminal opens empty, no code executes automatically.
   - You must deliberately paste the clipboard contents into your terminal prompt and press Enter.

***

## Standalone Oversight Utility (Optional)

The Command & Script security scanner and interactive pager used in this workflow is an independent external utility created by the same author:

* Repository: [https://github.com/Rakosn1cek/oversight](https://github.com/Rakosn1cek/oversight)

### Important: Optional Tool & User Responsibility

Oversight is **strictly optional** and is **not bundled** into the Fly Browser application package:

* **With Oversight Installed**: When you paste and execute a command prefixed with `oversight`, the tool intercepts the command, analyzes Shannon entropy for obfuscated payloads, checks for CVE vulnerabilities via the OSV.dev database, and renders an interactive TUI pager allowing line-by-line inspection before confirming execution.
* **Without Oversight Installed (Running at Your Own Risk)**: If you choose not to install the standalone Oversight tool, any commands staged to the clipboard and executed in the terminal will run without safety checks, warnings, or heuristic auditing. You execute them entirely at your own risk.

### Installation & Integration

If you wish to enable active security scanning and paging:

1. Clone the repository:
   ```bash
   git clone https://github.com/Rakosn1cek/oversight.git
   ```
2. Build and place the `oversight` executable within your system `PATH` (e.g. `~/.local/bin/` or `/usr/local/bin/`).

***

## Terminal Emulator Detection

Fly detects your host platform and terminal emulator dynamically:

- **Linux**: Scans for installed emulators in priority order:
  `kitty` -> `alacritty` -> `foot` -> `st` -> `xterm` -> `xdg-terminal-exec`.
- **macOS**: Automatically interfaces with the native `Terminal.app`.
- **Windows**: Prefers `Windows Terminal` (`wt.exe`) with graceful fallback to `cmd.exe`.
