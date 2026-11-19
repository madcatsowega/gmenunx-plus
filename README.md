# GMenuNX+ Modern (RS-97 Plus / RetroFW 2.3)

Modernized fork of `retrofw/gmenunx` (v2.3) tailored for the RS-97 Plus running RetroFW 2.3.

## Highlights

- **Live Quick Search (`X` or `SELECT -> Quick Search`)**: Split-screen live filter-as-you-type search across all menu links (Favourites, Games, Emulators, Apps) and your full ROM library (auto-discovered from emulator `selectordir`/`selectorfilter`).
  - **Auto-updating incremental ROM index (`romindex.cache` v2)**: Tracks directory `mtime`s and automatically re-scans only folders that changed (~2 ms check when nothing changed) whenever you add/remove ROMs.
  - Typing mode: `A` type, `B` backspace (or close when empty), `X` space, `L`/`R` cycle scope (`ALL` / `APPS` / `ROMS`), `Y` / `START` / `UP` from top row switches to results list.
  - Results mode: `A` launch directly (ROMs launch straight into their emulator), `X` add to Favourites, `SELECT` context menu (including **Rebuild ROM index**), `Y`/`B` return to keyboard.
- **In-Game Performance HUD (`libperfhud.so`)**:
  - Toggle via **Settings → Performance HUD** (`OFF` / `Top-Left` / `Top-Right` / `Bottom-Left` / `Bottom-Right`) or quickly via **SELECT → Perf HUD**.
  - Zero-flicker `LD_PRELOAD` overlay hooking SDL 1.2 (`SDL_Flip` / `SDL_UpdateRect` / `SDL_UpdateRects`) and SDL 2 (`SDL_RenderPresent`).
  - Shows live **FPS** (color-coded green/amber/red), **CPU %**, and live hardware **MHz** (read-only from JZ4760B CPM) — ideal for dialing in underclocking per emulator to save battery!
- **Playtime & Launch-Count Tracker**:
  - Zero-overhead monotonic `/proc/uptime` session tracking (immune to unset RTC).
  - Accumulates total playtime & launch counts in `playstats.conf`, displayed in status bar, list view, and **Settings → Playtime**.
- **Favourites Tab Superpowers**:
  - Clean display names (hides `004 `, `010 `, `184 ` pack prefixes when `Clean ROM names` is ON).
  - Sort modes via **SELECT → Sort** or Settings: **A-Z**, **Most Played**, **Recently Played**, or **Custom** (`Pin to Top`, `Move Earlier`, `Move Later`, saved in `favourites.order`).
- **Marquee Scrolling**: Smooth horizontal scrolling for long clipped titles in Main Menu (grid & list), ROM Selectors, and Quick Search results.
- **Recents Tab**: Automatically logs the last 24 launched games/ROMs (most recent first) in `recents.conf`.
- **Battery Percentage + Low-Battery Alerts**: Smoothed Li-ion percentage beside the battery icon (color-coded amber ≤20%, red ≤10%) with optional warning toasts at 15% and 5%.
- **Smart Letter Jump & Clean ROM Names**: `L`/`R` jumps cleanly to the first item of each `# A–Z` group with a floating HUD badge, plus `SELECT -> Jump to letter` `# A–Z` picker and `Y` in-folder filter.
- **Safe CPU Cap**: Keeps stock-safe PLL clock handling (`264–702 MHz`) so underclocking and mild overclocking stay safe without wrapping the 7-bit PLLM field.

## Build (RetroFW 2.3 SDK)

```bash
source ~/retrofw-dev/env.sh
cd ~/gmenunx-plus
make -j$(nproc) PLATFORM=retrofw CC=mipsel-linux-gcc CXX=mipsel-linux-g++ STRIP=mipsel-linux-strip dist
```
