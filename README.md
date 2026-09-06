# Barrier — Clipboard Auto-Sync fork

**Instant, two-way clipboard synchronization between your Windows machines —
no mouse movement required.**

[![Release](https://img.shields.io/badge/release-1.0.0-blue)](../../releases)

This is a maintained fork of [debauchee/barrier](https://github.com/debauchee/barrier)
(upstream is unmaintained since 2021). It focuses on one thing upstream never
got right: the clipboard.

## Why this fork exists

With upstream barrier, the clipboard only traveled between machines when you
physically moved the mouse across screens. If you control the server over
RDP/VNC and never see the second screen, you had to fall back to the remote
client's own "send clipboard" menu — every single time.

This fork makes the clipboard **always synchronized**:

* copy on the server → already on the laptop;
* copy on the laptop → already on the server;
* immediately, in the background, in both directions.

Works over any headless access (RDP, Radmin, VNC): the copy you make on the
remote desktop arrives on your other machine without any interaction.

## What's changed vs upstream

1. **Clipboard sync is immediate and unconditional** (both directions):
   `MSWindowsScreen` now reports every non-barrier clipboard change the moment
   it happens, `Client` pushes its clipboard on every grab, and `Server`
   broadcasts the new clipboard to every connected client right away — the
   mouse-leave-only path is gone.
2. **Fixed the long-standing missequenced/ignored clipboard bug**: the server
   used one shared sequence number per clipboard while every screen has its
   own; after the primary screen grabbed, legitimate client copies were
   rejected as "missequenced" (the classic `ignored screen ... grab of
   clipboard` log spam and intermittent lost copies). Sequence numbers are
   now tracked **per screen** in `BaseClientProxy`.
3. **Always-on full logging**: nodes write a complete rolling log to
   `%APPDATA%\Barrier\logs\<exe>.log` even when launched without `-l`, so the
   full history is always on disk for diagnostics (the GUI's "Show Log" only
   holds a small stdout buffer).
4. **`tools/barriertray`** — a tiny standalone tray helper (pure Win32, one
   static exe, no Qt): an always-visible tray icon with a menu to copy the
   entire current log to the clipboard in one click, open the log, open
   barrier.conf, or restart the Barrier service. Re-adds its icon if explorer
   restarts, and survives the Windows 10/11 hidden-overflow problem with a
   retry loop.
5. **Build**: MSVC 2022-compatible (C++17, `std::filesystem` instead of the
   unmaintained `ghc::filesystem` polyfill), static CRT (`/MT`) so binaries
   run on a bare machine with no VC++ redistributable. Verified working on
   Windows 7 and Windows 11.

## Getting the binaries

Grab the zip from [Releases](../../releases). Unpack, stop the Barrier
service, copy the executables over your existing barrier install (usually
`C:\Program Files\Barrier\`), start the service again — your existing
`barrier.conf` and settings stay untouched.

Requirements: Windows 7/8/8.1/10/11 x64. The node binaries and the tray
helper in this package are statically linked (no VC++ redistributable
needed); barrier's GUI uses Qt and is unchanged from upstream.

## Building from source

Same as upstream barrier (CMake), e.g. on Windows with VS 2022:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target barriers barrierc barrierd
```

The tray helper builds standalone, see the comment at the top of
`tools/barriertray/barriertray.cpp`.

## Layout of this fork

| Path | What it is |
|------|-------------|
| `src/lib/barrier/App.cpp` | always-on file logging |
| `src/lib/client/Client.cpp` | immediate client→server clipboard push |
| `src/lib/platform/MSWindowsScreen.cpp` | un-gated clipboard change detection |
| `src/lib/server/BaseClientProxy.{h,cpp}` | per-screen clipboard sequence numbers |
| `src/lib/server/Server.cpp` | immediate broadcast to all clients |
| `tools/barriertray/` | standalone tray helper (source + icon) |

## Credits

Based on [barrier](https://github.com/debauchee/barrier) by Debauchee
(originally from Synergy by Chris Schoeneman). See LICENSE (GPLv2).
