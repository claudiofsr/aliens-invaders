# Aliens Invaders

[![Standard: C++20](https://img.shields.io/badge/C%2B%2B-20-purple.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Library: SDL3](https://img.shields.io/badge/Library-SDL3-red.svg)](https://www.libsdl.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Display: Wayland Native](https://img.shields.io/badge/Display-Wayland%20Native-orange.svg)](https://wayland.freedesktop.org/)
[![Audio: 2.1 Stereo](https://img.shields.io/badge/Audio-2.1%20Stereo%20Procedural-blue.svg)](#sound-design--21-stereo)
[![Build: CMake & Make](https://img.shields.io/badge/Build-CMake%20%7C%20Make-brightgreen.svg)](#how-to-enlist--compile)
[![Platforms: Linux | FreeBSD | macOS | Windows](https://img.shields.io/badge/Platforms-Linux%20%7C%20FreeBSD%20%7C%20macOS%20%7C%20Windows-blue.svg)](#how-to-enlist--compile)

<p align="center">
  <img src="screenshot.png" alt="Aliens Invaders" />
</p>

---

## Mission Briefing: Attention, Fellow Earthlings! 🌍🛸

While humanity was busy arguing on social media, perfecting artisanal coffee foam, and debating whether pineapple belongs on pizza, deep-space sensors detected a massive extraterrestrial armada descending upon our solar system.

Their diplomatic delegation arrived in synchronized wedge formation deploying high-yield plasma mortars. In universal galactic etiquette, that translates roughly to: *"Hand over the planet; your lease expired three minutes ago."*

You have just been promoted to Lead Planetary Interceptor Pilot (mostly because everyone else called in sick today). Your mission is simple:
1. **Climb into the cockpit** of an experimental Earth defense vessel.
2. **Blast through synchronized dive-bombing formations.**
3. **Grab tactical nuke warheads** and supercharged multi-cannons.
4. **Remind these extraterrestrial tourists** why they should have taken that left turn at Alpha Centauri.

---

## Ship Hangar & Experimental Arsenal

Select your combat vessel in the menu by pressing <kbd>T</kbd>:
- **The Cruiser (Standard Model):** The battle-hardened workhorse of Earth's defense fleet. Balanced aerodynamics, reinforced titanium hull, and dual high-frequency plasma dissipation rails.
- **The Vanguard (Apex Prototype):** Next-generation experimental interceptor featuring dual swept-wing ion thrusters, tighter lateral drift, and high-visibility cockpit canopy.

### Battlefield Power-Ups:
- 🔵 **Speed Boost:** Overclocks sub-light maneuvering thrusters (+10% agility, max 120%).
- 🔴 **Rapid Fire:** Maximizes plasma cannon cyclic rate (+10% fire rate, max 120%).
- 🟣 **Multi-Cannon:** Quad-plasma spread firing solution (up to 3 simultaneous shots).
- 🟢 **Shield Matrix:** Restores reinforced electromagnetic shielding (+1 hull life point, max 8).
- 🟡 **Tactical Nuke:** Screen-clearing atomic blast vaporizing all active hostiles in a flash of nuclear glory (max 1 per 15-wave stage).

---

## Flight Controls

| Key | Gamepad (Xbox / Standard) | Tactical Action |
| :--- | :--- | :--- |
| <kbd>Space</kbd> | <kbd>A</kbd> / <kbd>RT</kbd> | Fire Primary Plasma Cannons |
| <kbd>←</kbd> / <kbd>→</kbd> | Left Analog / D-Pad | Maneuver Vessel Horizontally |
| <kbd>T</kbd> | <kbd>Y</kbd> | Toggle Ship Model (*Cruiser* vs *Vanguard*) |
| <kbd>LT</kbd> / <kbd>RT</kbd> | <kbd>LT</kbd> / <kbd>RT</kbd> | Cycle Menu Pages (Left / Right) |
| <kbd>-</kbd> / <kbd>+</kbd> | <kbd>LB</kbd> / <kbd>RB</kbd> | Adjust Particle Density (Levels 0 to 4) |
| <kbd>F</kbd> | — | Toggle Fullscreen Mode (Native monitor resolution up to 4K UHD) |
| <kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd> | — | Preset Resolution Scales (`1280x720`, `1600x900`, `1920x1080`) |
| <kbd>I</kbd> / <kbd>B</kbd> | <kbd>B</kbd> | Toggle Telemetry Overlay (CPU%, RAM%, FPS) |
| <kbd>H</kbd> | <kbd>Back</kbd> | Hall of Fame Leaderboards & Tactical Dossiers |
| <kbd>P</kbd> | <kbd>Start</kbd> | Pause / Resume Active Combat |
| <kbd>S</kbd> | <kbd>Start</kbd> | Launch Match (Start game from menu) |
| <kbd>C</kbd> | — | Cheat Mode (Instant max firepower; disables leaderboard saving) |
| <kbd>Q</kbd> | — | Quit Combat or Return to Desktop |

---

## Technical Highlights

- **Fixed-Step Simulation (60 Hz):** Simulation physics runs on a rigid, deterministic 60 Hz clock decoupled from monitor refresh rates.
- **Subpixel Render Interpolation (alpha):** Continuous state interpolation guarantees tear-free, silky-smooth presentation across 60 Hz, 120 Hz, 144 Hz, 240 Hz, 360 Hz, and 600+ Hz displays.
- **Anti-Hitch Bounded Accumulator:** The simulation clock clamps scheduling stalls (compositor lag, wallpaper transitions, background I/O) to a maximum of 33 ms (2 steps), completely preventing catch-up display freezes.
- **Sound Design (2.1 Stereo):** Procedural physical audio synthesis in RAM with a dedicated in-phase sub-bass channel (22 Hz to 75 Hz) for physical subwoofer response, musical tape-style limiter saturation (`std::tanh`), and zero digital hissing or static.
- **Fair-Play Anti-Scissor Ballistics:** Real-time trajectory prediction guarantees an evasion corridor of at least 118 px, eliminating inescapable converging cross-fire traps.
- **Barry-Goldman Centripetal Catmull-Rom Splines:** Resampled at uniform arc-length intervals to eliminate velocity whip, cusps, and angular stair-stepping during dive-bombing runs.
- **Zero-Allocation Hot Loop:** Compile-time embedded binary assets and contiguous entity pooling (`std::array`) ensure zero heap allocation during combat.
- **Asynchronous Persistence:** High-score transactions use non-blocking background threads (`std::async`) and atomic file replacement (`.tmp` to final).
- **Headless C++20 Test Suite:** Independent test runner validating AABB collisions, subpixel transforms, PRNG determinism, and high-score serialization without initializing a display window.

---

## Local Arcade Hall of Fame (Multi-User)

*Aliens Invaders* honors the classic arcade spirit. On Linux/FreeDesktop systems, scores are saved to `/var/games/aliens-invaders.scores` with shared permissions (`0666`), enabling all local user accounts on your computer to compete on the exact same leaderboard.

When running portably, scores gracefully save to standard sandboxed storage (`~/.local/share/aliens-invaders/` on Linux, `%APPDATA%\aliens-invaders\` on Windows, and sandboxed containers on macOS, Android, and iOS).

---

## How to Enlist & Compile

Built with modern **C++20** and native **SDL3**, the engine compiles effortlessly across all major operating systems using either **CMake** or **Make**:

### Prerequisites

- **Arch Linux / Manjaro:**
    sudo pacman -S sdl3 cmake ninja pkgconf gcc

- **Debian / Ubuntu / Linux Mint:**
    sudo apt install libsdl3-dev cmake ninja-build pkg-config build-essential

- **Fedora / RHEL:**
    sudo dnf install sdl3-devel cmake ninja-build pkgconfig gcc-c++

- **macOS (Homebrew):**
    brew install sdl3 cmake ninja pkg-config

- **Windows (MSYS2 UCRT64):**
    pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-cmake ninja make

---

### Option A: Build with CMake (Recommended for Cross-Platform)

    cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
    cmake --build build -j$(nproc)
    ./build/unit_tests              # Run automated headless C++20 test suite
    ./build/aliens-invaders          # Launch game

---

### Option B: Build with Makefile

    make -j$(nproc)                 # Compile release binary (-O2)
    make test                       # Run automated C++20 test suite
    make sanitize                   # Compile with AddressSanitizer and UBSan
    sudo make install               # Install binary, icons, and desktop entries to system

---

## Author & Copyright

**Claudio Fernandes de Souza Rodrigues**  
Lead Engine Architecture, C++20 Systems, Procedural 2.1 Audio, Aerodynamics & Gameplay.  
GitHub: [@claudiofsr](https://github.com/claudiofsr)

Copyright (c) 2026 Claudio Fernandes de Souza Rodrigues. All Rights Reserved.

Licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.
