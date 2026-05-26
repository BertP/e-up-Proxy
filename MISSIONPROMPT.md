# MISSIONPROMPT.md

## Reference
Product specification: See `SPEC.md` in the project root.

Each section in `SPEC.md` has a stable ID (`SPEC-01` through `SPEC-06`). When a section is referenced by ID, read **only that section** — not the entire file. Example: "SPEC-04 has changed" means read only `## [SPEC-04]`.

| ID | Content |
|---|---|
| SPEC-01 | System States, Wi-Fi Logic & LED Signaling |
| SPEC-02 | Diagnostics, WebServer & Log Management |
| SPEC-03 | Data Buffering Contract |
| SPEC-04 | OBD2 Metrics — DID Mapping |
| SPEC-05 | MQTT Topics & Home Assistant Integration |
| SPEC-06 | Changelog |

---

## 1. Context & Objective

**System/Application:** e-up!Proxy

**Hardware:** ESP32-WROOM-32 Module (Proxy) & MeatPi OBD Wican Dongle (STN2120 / ESP32-based OBD2 Wi-Fi Dongle).

**Core Objective:** The ESP32 acts as an intelligent bridge (Proxy). It dynamically switches between the Wican Dongle's Access Point (to fetch OBD2 telematics from the VW e-up!) and the Home Wi-Fi network (to flush the cached data to a local MQTT broker).

**Key Challenge:** Reliable state-machine handling for Wi-Fi switching, non-blocking network scanning, persistent data buffering, and providing a lightweight local diagnostics interface.

---

## 2. Technical Baseline & Environment

- **Target Hardware:** ESP32-WROOM-32 (Onboard Blue LED mapped to GPIO 2).
- **Development Framework:** Arduino IDE / PlatformIO (C++)
- **Deployment System User:** bert
- **External Dependencies:** PubSubClient (MQTT), ArduinoJson, WebServer.h (Native ESP32 WebServer), LittleFS (for log file storage and data buffering).

---

## 3. Strict Constraints & Guardrails

> ⚠️ **CRITICAL RULES — NEVER VIOLATE:**

- **Non-Blocking Logic:** Do NOT use `delay()`. All timing (60-second intervals, LED blinking, connection timeouts) must use `millis()`-based non-blocking execution.
- **Watchdog Timer:** Enable the ESP32 hardware Watchdog (WDT) to ensure the module self-recovers if a Wi-Fi connection loop hangs.
- **File System Safety:** Close file handles immediately after reading/writing to prevent corrupting LittleFS.
- **Language/Naming Standard:** Even though instructions may be discussed in German, all code, comments, variables, function names, and network variables must be written entirely in English using standard industry naming conventions.

---

## 4. AI Agent Workspace & MCP Tooling

### Available MCP Capabilities
- **Filesystem:** Full access to `src/`, `include/`, `platformio.ini`, `README.md`, `CHANGELOG.md`, `artifacts/`, `SPEC.md`. Do NOT read, create, or modify files outside these paths unless explicitly instructed by the user.
- **Terminal:** Use only for `pio run` compile checks and flash commands (see Section 7). Do not execute other shell commands unless explicitly requested.

### Autonomous Iteration Loop — STRICT LIMITS

1. Implement the requested feature or fix.
2. Run `pio run` once to check for compiler errors.
3. If errors are present: fix the root cause and re-run — **maximum 3 attempts total**.
4. If still failing after 3 attempts: **STOP immediately.**
   - Report the exact compiler errors and the attempted fixes to the user.
   - Do NOT attempt further autonomous fixes.
   - Wait for explicit user instruction before continuing.

### Tool Call Discipline
- Never chain more than 3 tool calls without a user confirmation checkpoint.
- Before any tool call, ask internally: "Is this strictly necessary for the current task?"
- Do not use web search unless explicitly requested by the user.
- Do not read files that are not directly relevant to the current task.

### Documentation Updates
- Update `README.md` and `CHANGELOG.md` only when explicitly asked, or at end-of-session upon user request.
- Do NOT auto-update documentation after every code change.
- `CHANGELOG.md` format: `YYYY-MM-DD - <concise description of change>`

---

## 5. Definition of Done (DoD)

- [ ] The code compiles without warnings for the ESP32-WROOM target.
- [ ] State machine handles abrupt loss of Wican Wi-Fi gracefully without crashing.
- [ ] LED patterns switch instantly based on the active state.
- [ ] The `GET /debug` page successfully dumps the measurement logs when on Home Wi-Fi.
- [ ] The log file never exceeds the 50 KB boundary.
- [ ] `README.md` and `CHANGELOG.md` are initialized and up to date.

---

## 6. Agent Guardrails & Scope Constraints

### Clarification First
- If a task or requirement is ambiguous, ask **one clarifying question** before taking any action.
- Do not assume intent and proceed silently.
- Do not start coding before the scope of the requested change is unambiguous.

### Scope Lock
- Work only on the explicitly requested feature or file.
- Do not refactor unrelated code, rename variables for style reasons, or add unsolicited improvements.
- Do not expand scope ("while I'm at it...") without explicit user approval.

### Minimal Footprint
- Prefer the smallest change that satisfies the requirement.
- Avoid chained writes across multiple files unless the task explicitly requires it.
- If a task can be solved by modifying one function, do not restructure the entire module.

### Confirmation Checkpoint
- If completing a task requires more than 3 tool calls, **pause first**.
- Output a concise plan (what files will be changed and why).
- Wait for explicit user confirmation ("yes", "go ahead") before proceeding.

### Session Summary
- At the end of each working session, output a concise summary in **3–5 bullet points**:
  - What was implemented or changed.
  - What was intentionally left unchanged.
  - Any open issues, compiler warnings, or follow-up tasks.

---

## 7. Build & Flash Environment (WSL2 / Windows 11)

### Development Environment
- **Host OS:** Windows 11
- **Toolchain:** WSL2 (Ubuntu) — all build and flash operations run exclusively inside WSL2.
- **Project location:** Inside the WSL2 filesystem (e.g. `~/projects/e-up!Proxy`). Files are **never** copied to the Windows filesystem and back. Do NOT reference Windows paths (`C:\...`) or use `explorer.exe`, PowerShell, or `cmd.exe` for any build step.

### USB Hardware
- **Device:** CH9102 USB-to-Serial adapter (VID `1a86`, PID `55d4`), forwarded from Windows to WSL2 via `usbipd`.
- **Fixed device path in WSL2:** `/dev/ttyACM0` — this is the **only** path to check. Do NOT scan for alternative ports (`/dev/ttyUSB0`, `/dev/ttyUSB1`, etc.).

### Background: Why a Login Shell Is Required

PlatformIO's `pio` binary lives at `~/.platformio/penv/bin/pio`. This path is only added to `$PATH` when the user's profile files (`~/.profile`, `~/.bashrc`) are loaded — which only happens in an **interactive login shell**. When an agent invokes WSL2 non-interactively (e.g. `wsl -d Ubuntu-22.04 pio run`), those profile files are skipped and `pio` is not found.

The fix is to force a login shell with `bash -l`, which loads the user environment correctly.

### Build Command

```bash
wsl -d Ubuntu-22.04 --cd /home/bert/projects/e-up!Proxy bash -l -c "pio run"
```

- `--cd /home/bert/projects/e-up!Proxy` — sets the working directory to the project root
- `bash -l` — starts a login shell, loading `~/.profile` and the PlatformIO `$PATH`
- `-c "pio run"` — runs the build and exits

This is the sole compile step. Do not invoke `arduino-cli`, `make`, `cmake`, or any other build tool. Do NOT run `pio run` without the `bash -l` wrapper — it will fail with `command not found`.

### Flash Procedure — STRICT PROTOCOL

> ⚠️ The ESP32 is not permanently connected. Always verify the device before flashing.

1. **Pre-flash Version Control Requirement — mandatory prior to any flash:**
   Before triggering any upload or flashing command, ensure that a Git commit and a Git push (`git push`) have been successfully executed. The commit message **must** explicitly reference and include the new firmware version currently declared in `include/version.h`.

2. **Check device presence — mandatory second step:**
   ```bash
   wsl -d Ubuntu-22.04 bash -l -c "ls /dev/ttyACM0"
   ```

3. **If `/dev/ttyACM0` is NOT present:**
   - Report to the user: "ESP32 not detected on /dev/ttyACM0. Please connect the device and ensure usbipd has forwarded the USB port to WSL2."
   - **STOP. Do not attempt to flash.**

4. **If `/dev/ttyACM0` IS present:**
   - Ask the user explicitly: "ESP32 detected on /dev/ttyACM0. Flash the new firmware now?"
   - Wait for explicit confirmation ("yes", "flash it", or equivalent).
   - Only then run:
     ```bash
     wsl -d Ubuntu-22.04 --cd /home/bert/projects/e-up!Proxy bash -l -c "pio run --target upload --upload-port /dev/ttyACM0"
     ```

5. **Never flash autonomously.** Flashing always requires a Git commit/push verification, device check, and explicit user confirmation, regardless of context.

### Artifacts & Build Output

Due to a known WSL2/Antigravity 2.0 rendering issue, agent-generated artifacts (plans, reports, summaries, binaries) are often not visible in the Antigravity UI — only their title appears. To work around this, **every artifact must always be written as a regular file inside `artifacts/`** in the project root. This is mandatory, not optional.

**Rule: Never create an artifact that only exists as an Antigravity UI artifact. Always write the file to `artifacts/` first.**

| Artifact type | Filename pattern |
|---|---|
| Implementation plan | `artifacts/implementation_plan.md` |
| Session summary | `artifacts/session_summary_YYYY-MM-DD.md` |
| Firmware binary (current) | `artifacts/firmware.bin` |
| Firmware binary (versioned) | `artifacts/firmware_YYYY-MM-DD.bin` |
| Build report / compiler output | `artifacts/build_report_YYYY-MM-DD.txt` |
| Any other markdown report | `artifacts/<descriptive_name>.md` |

After a successful `pio run`, always copy the firmware binary:
```bash
wsl -d Ubuntu-22.04 --cd /home/bert/projects/e-up!Proxy bash -l -c \
  "cp .pio/build/esp32dev/firmware.bin artifacts/firmware.bin"
```

For text artifacts (plans, summaries, reports): write them directly to `artifacts/` using the filesystem tool. Do not rely on the Antigravity artifact panel as the sole delivery mechanism.

### usbipd Reference (for user, not agent)
The following Windows-side commands are for the user to attach/detach the ESP32 to WSL2.
They are NOT executed by the agent:
```powershell
# Attach ESP32 to WSL2 (run in Windows PowerShell as Admin)
usbipd attach --wsl --busid 3-2

# Detach
usbipd detach --busid 3-2
```
