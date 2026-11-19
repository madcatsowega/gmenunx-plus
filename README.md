# GMenuNX+ Modern (RS-97 Plus / RetroFW 2.3)

Modernized fork of `retrofw/gmenunx` (v2.3) tailored for the RS-97 Plus running RetroFW 2.3.

## Highlights

- **Live Quick Search (`X` or `SELECT -> Quick Search`)**: Split-screen live filter-as-you-type search across all menu links (Favourites, Games, Emulators, Apps) and your full ROM library (auto-discovered from emulator `selectordir`/`selectorfilter` and cached in `romindex.cache`).
  - Typing mode: `A` type, `B` backspace (or close when empty), `X` space, `L`/`R` cycle scope (`ALL` / `APPS` / `ROMS`), `Y` / `START` / `UP` from top row switches to results list.
  - Results mode: `A` launch directly (ROMs launch straight into their emulator), `X` add to Favourites, `SELECT` context menu (including **Rebuild ROM index**), `Y`/`B` return to keyboard.
- **Recents Tab**: Automatically logs the last 24 launched games/ROMs (most recent first) in `recents.conf`. Selecting an item relaunches that exact game/ROM directly. Toggleable in Settings.
- **Bulletproof Selection Persistence**: Saves `sectionName` and `linkTitle` in addition to numeric indices so booting up reliably returns to your last selected game on Favourites.
- **Battery Percentage**: Smoothed Li-ion percentage readout right next to the existing battery icon (hides while charging/AC). Toggleable in Settings.
- **Smart Letter Jump & Clean ROM Names**:
  - Clean ROM sorting/display strips leading pack numbers (`007 Donkey Kong Country 1.smc` -> `Donkey Kong Country 1`) while preserving titles like `1942` / `1943` / `2048`.
  - `L`/`R` in ROM selectors jump cleanly to the first item of the previous/next letter (`#`, `A`–`Z`) with a floating badge HUD.
  - `SELECT -> Jump to letter` opens a 2-tap `# A-Z` picker grid.
  - `Y` filters the current folder; `SELECT -> Random game` picks a random ROM.
- **Safe CPU Cap**: Keeps stock-safe PLL clock handling and caps `cpu_max` at `702 MHz` to prevent the 7-bit PLLM wrap bug.
- **Configurable Paths**: Respects `GMENUNX_HOME` and `GMENUNX_DATA` environment variables for non-destructive testing alongside an existing `apps/gmenu2x` setup.

## Build (RetroFW 2.3 SDK)

```bash
source ~/retrofw-dev/env.sh
cd ~/gmenunx-plus
make -j$(nproc) PLATFORM=retrofw CC=mipsel-linux-gcc CXX=mipsel-linux-g++ STRIP=mipsel-linux-strip dist
```
