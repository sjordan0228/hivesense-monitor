---
name: pio-build-test
description: Silent firmware build and native unit test workflow. Use whenever running pio run or pio test — never run them raw.
triggers:
  - "build firmware"
  - "pio run"
  - "pio test"
  - "compile"
  - "native tests"
edges:
  - target: context/setup.md
    condition: when configuring the dev environment for the first time
  - target: context/conventions.md
    condition: when adding a new firmware module that needs verify steps
last_updated: 2026-04-26
---

# PlatformIO Build & Test (Silent)

## Context

PlatformIO's default output (compile spam, Unity banners, per-test PASS lines) is the single biggest avoidable context cost in this codebase. Never run `pio run` or `pio test` without one of the patterns below.

This applies inside agent sessions (Claude, Codex, Cursor) and when a subagent is dispatched to do firmware work. Embed the silencing pattern into every dispatch prompt.

## Steps

**Build (Arduino target compile):**

```bash
pio run -s 2>&1 | tail -5
```

Three lines of size summary + two status lines. Done.

**Native unit tests (host machine):**

```bash
pio test -e native 2>&1 | grep -E "(PASS|FAIL|Tests|Ignored)"
```

Per-test PASS/FAIL lines plus the `N Tests N Failures` footer. No Unity banners, no build noise.

**Upload / device tests (where mid-flash failure is possible):**

```bash
pio test -e <env> > /tmp/pio.log 2>&1 && tail -3 /tmp/pio.log || tail -60 /tmp/pio.log
```

Green run: ~3 lines. Failure: enough log to diagnose.

**Subagent dispatch:** when delegating firmware work, embed:

> "Run `<pio command using one of the silent patterns above>`. Report only pass/fail counts and any failing assertion text, max 5 lines. Do not paste build output or per-test PASS lines."

## Gotchas

- The `-s` flag suppresses scons / progress output but keeps real warnings and errors. It is the right default for builds.
- If a build fails and silent output doesn't reveal the cause, run **once** unfiltered and capture only the error block: `pio run 2>&1 | grep -A 5 -B 2 'error:'`. Do not paste the full log.
- Native tests are host-machine only — they don't require a device. The `native` env in `platformio.ini` builds payload, OTA manifest, OTA decision, sha256, battery math files (see `build_src_filter`).

## Verify

- [ ] Build output is ≤ 5 lines on success
- [ ] Test output is ≤ 50 lines on success (one PASS line per test + footer)
- [ ] Failures show the actual error/assertion, not a wall of compile noise

## Debug

- "memsearch silent test failed but I want to see why": run `pio test -e native -v 2>&1 | tail -80` once to get verbose tail. Don't pipe `-v` into grep — defeats the purpose.
- "Build silent but I suspect a warning": `pio run 2>&1 | grep -E 'warning:|error:' | head -20`.

## Update Scaffold

- [ ] Update `.mex/ROUTER.md` "Current Project State" if test count or env list changes
- [ ] Update `context/setup.md` "Common Commands" if a new silent invocation pattern emerges
- [ ] If a new env is added to `platformio.ini`, list it here
