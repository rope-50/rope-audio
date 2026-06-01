---
name: Bug report
about: Report a crash, glitch, or incorrect behavior
title: "[bug] "
labels: bug
---

**What happened**
A clear description of the bug and what you expected instead.

**Environment**
- rope version / commit:
- Platform & arch (e.g. Windows 11 x64, macOS arm64, Android arm64-v8a):
- Backend (miniaudio / RtAudio / ASIO / Null):
- Consumed via (C++ find_package / FetchContent, Flutter plugin, C# bindings):
- Sample rate & buffer size (if an audio glitch):

**Reproduction**
Steps, and ideally a minimal repro. Audio issues are easiest to pin down
headlessly via the **Null backend** + `renderOffline()` — if you can reproduce
it there, paste that snippet.

**Logs / output**
Any `[rope] ...` stderr lines, crash dumps, or rendered values.
