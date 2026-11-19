# GMenuNX+ Modern

**A modernized launcher & zero-flicker performance HUD for RetroFW 2.3 handhelds (RS-97 Plus, RS-97, RG-300, LDK, Retro Arcade Mini).**

[![Platform](https://img.shields.io/badge/platform-RetroFW%202.3%20(MIPS32)-blue)](https://github.com/madcatsowega/gmenunx-plus/releases)
[![License: GPL v2](https://img.shields.io/badge/license-GPL%20v2-green.svg)](COPYING)
[![Release](https://img.shields.io/github/v/release/madcatsowega/gmenunx-plus)](https://github.com/madcatsowega/gmenunx-plus/releases/latest)

GMenuNX+ Modern is a community continuation of [`retrofw/gmenunx`](https://github.com/retrofw/gmenunx) (v2.3) focused on bringing modern handheld launcher ergonomics to classic Ingenic JZ4760/JZ4760B devices — without modifying your stock `rootfs.squashfs`.

---

## Features

### Live Unified Quick Search (`X`)
- **Split-screen 320×240 interface**: Compact 4×10 D-pad keyboard at the bottom with live ranked results updating on every keystroke right above it.
- **Searches your entire device**: Finds apps, games, favourites, emulators, **and your entire ROM library** (auto-discovered from emulator `selectordir`/`selectorfilter`).
- **Automatic incremental indexing**: Tracks ROM folder modification timestamps (`mtime`) in `romindex.cache` (`v2`) — opens instantaneously when nothing changed, and automatically refreshes only folders where you added or removed ROMs.
- **Multi-token & system filtering**: Search by partial title or console (e.g., `mario 3`, `gba metroid`), cycle scope with **L/R** (`ALL` / `APPS` / `ROMS`), launch ROMs directly into their emulator with **A**, or add any result to **Favourites** with **X**.

### In-Game Performance HUD (`libperfhud.so`)
- **Zero-flicker `LD_PRELOAD` overlay** (16 KB, zero SDL library linkage) hooking SDL 1.2 (`SDL_Flip`, `SDL_UpdateRect`, `SDL_UpdateRects`) and SDL 2 (`SDL_RenderPresent`).
- Displays live **FPS** (color-coded green / amber / red), **CPU %**, and live hardware **MHz** (read-only from JZ4760B CPM) — ideal for dialing in per-emulator underclocks to extend battery life.
- Toggle anytime via **SELECT → Perf HUD** or choose any corner in **Settings → Performance HUD**.

### Playtime & Launch-Count Tracker
- Zero-overhead session tracking using monotonic `/proc/uptime` (accurate even when the handheld RTC is unset).
- Accumulates total playtime and launch counts in `playstats.conf`, displayed in the status bar, list view, and **Settings → Playtime**.

### Favourites Superpowers & Recents Tab
- **Clean ROM display names**: Strips leading ROM-pack prefixes (`007 Donkey Kong Country 1.smc` → `Donkey Kong Country 1`) while preserving real numbered titles (`1942`, `1943`, `2048`), without renaming files on disk.
- **Flexible Favourites sorting**: Switch between **A–Z**, **Most Played**, **Recently Played**, or **Custom** (**Pin to Top / Move Earlier / Move Later**, persisted in `favourites.order`).
- **Recents Tab**: Automatically logs the last 24 launched games and ROMs (most recent first) for instant relaunching.
- **Bulletproof boot restore**: Saves selection by section name and title so booting up reliably returns to your last selected game.

### Quality-of-Life Polish
- **Smooth Marquee Scrolling**: Highlighted long titles gently scroll horizontally across Main Menu (grid & list), ROM Selectors, and Quick Search results (zero idle CPU use when titles fit).
- **Smart Letter Jump**: **L/R** in ROM lists jumps cleanly to the first item of each `# A–Z` group with a floating HUD badge; **SELECT → Jump to letter** opens a 2-tap `# A–Z` picker; **Y** filters the current folder.
- **Battery Percentage & Low-Battery Alerts**: Smoothed Li-ion percentage beside the battery icon (amber ≤ 20%, red ≤ 10%) with optional warning toasts at 15% and 5%.
- **Safe CPU Clock Cap**: Preserves stock-safe PLL clock writes (`264–702 MHz`) so the slider cannot wrap the JZ4760B's 7-bit `PLLM` multiplier.

---

## Controls Quick Reference

| Context | Button | Action |
| :--- | :--- | :--- |
| **Main Menu** | **X** | Open Live Quick Search |
| **Main Menu** | **SELECT** | Context menu (Search, Perf HUD, Favourites sort/pin/reorder, Edit) |
| **Main Menu** | **START** | Settings (Perf HUD, Favourites sort, Battery %, Alerts, Clean ROM names) |
| **Quick Search (Typing)** | **A** / **B** / **X** | Type key / Backspace (or Close) / Space |
| **Quick Search (Typing)** | **Y** / **START** / **UP** | Jump to full-screen results list |
| **Quick Search (Results)** | **A** / **X** / **L·R** | Launch directly / Add to Favourites / Switch scope (`ALL`/`APPS`/`ROMS`) |
| **ROM Selector** | **L / R** | Jump to previous / next letter group (`# A–Z`) |
| **ROM Selector** | **Y** | Filter current folder by name |
| **ROM Selector** | **SELECT** | Jump to letter grid, Random game, Toggle clean ROM names, Add to Favourites |

---

## Installation (Non-Destructive — Zero Firmware Risk)

1. Download **`gmenunx-plus-retrofw2.3.zip`** from the [**Releases**](https://github.com/madcatsowega/gmenunx-plus/releases/latest) page.
2. Copy the `apps/gmenu2x-plus` folder onto the root of your `RETROFW` SD partition (so you have `/home/retrofw/apps/gmenu2x-plus/gmenunx` on the handheld).
3. Back up any existing `autoexec.sh` on the root of `RETROFW`, then copy `autoexec.sh.sample` to `autoexec.sh`.
   - If your links/skins live in `.gmenunx/` (stock RetroFW 2.3), keep `GMENUNX_HOME=/home/retrofw/.gmenunx`.
   - If your links/skins live in `apps/gmenu2x/`, change it to `GMENUNX_HOME=/home/retrofw/apps/gmenu2x`.
4. Safely eject and boot! Open **Settings → About** to confirm **GMenuNX+ Modern** is active.

**To uninstall**: Delete `autoexec.sh` (or restore your backup) and delete `apps/gmenu2x-plus`. Your stock `rootfs.squashfs` is never modified.

---

## USB File Transfer & Instant Deploy (`scripts/retrofw-usb.py`)

RetroFW 2.3 already runs FTP and Telnet over USB Ethernet (`169.254.1.1`) when you connect via USB and pick **Charger**. The included zero-dependency Python 3 tool lets you transfer ROMs/OPKs and deploy builds over USB without pulling the SD card:

```bash
./scripts/retrofw-usb.py status                      # Check link, kernel, I/O sched, RAM, SD space
./scripts/retrofw-usb.py push game.sfc roms:SFC/     # Upload a ROM or folder (+ auto sync)
./scripts/retrofw-usb.py push port.opk sd2:apps/     # Upload an OPK directly to SD card 2
./scripts/retrofw-usb.py screenshots                 # Grab START-hold screenshots from device
./scripts/retrofw-usb.py deploy                      # Push new gmenunx + libperfhud.so & respawn
```

---

## Building from Source

Requires the official **RetroFW 2.3 SDK** (Buildroot 2021.11 / GCC 11.2 / SDL 1.2):

```bash
source ~/retrofw-dev/env.sh
make -j$(nproc) PLATFORM=retrofw CC=mipsel-linux-gcc CXX=mipsel-linux-g++ STRIP=mipsel-linux-strip dist
./scripts/make-release.sh
```

See [ROADMAP.md](ROADMAP.md) for planned Phase 2–4 work (including dual-SD2 auto-sync and a community RetroFW 2.4 userspace & kernel fork).

---

## Credits & License

Released under the **GNU General Public License v2.0** ([COPYING](COPYING)).

- Built directly upon [**RetroFW / GMenuNX**](https://github.com/retrofw/gmenunx) (v2.3) by Pingflood, Jutley, TonyJih, Steward-Fu, and the RetroFW team.
- Originally based on **GMenu2X** by Massimiliano "Ryo" Torromeo.
