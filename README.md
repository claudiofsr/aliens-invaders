# Aliens Invaders

[![Standard: C++20](https://img.shields.io/badge/C%2B%2B-20-purple.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Library: SDL3](https://img.shields.io/badge/Library-SDL3-red.svg)](https://www.libsdl.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Display: Wayland Native](https://img.shields.io/badge/Display-Wayland%20Native-orange.svg)](https://wayland.freedesktop.org/)
[![Graphics: Vulkan & OpenGL](https://img.shields.io/badge/Graphics-Vulkan%20%7C%20OpenGL-blueviolet.svg)](#technical-highlights)
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
1. **Climb into the cockpit** of an experimental Earth defense spaceship.
2. **Blast through synchronized dive-bombing formations.**
3. **Grab tactical nuke warheads** and supercharged multi-cannons.
4. **Remind these extraterrestrial tourists** why they should have taken that left turn at Alpha Centauri.

---

## Ship Hangar & Experimental Arsenal

Select your combat spaceship in the menu by pressing <kbd>T</kbd>:
- **The Cruiser (Standard Model):** The battle-hardened workhorse of Earth's defense fleet. Balanced aerodynamics, reinforced titanium hull, and dual high-frequency plasma dissipation rails.
- **The Vanguard (Apex Prototype):** Next-generation experimental interceptor featuring dual swept-wing ion thrusters, tighter lateral drift, and high-visibility cockpit canopy.

### Battlefield Power-Ups:
*In each 15-wave stage, a maximum of 5 supply pods can appear across all waves, with each bonus type dropping at most once per stage (zero duplicate bonus drops within the same stage).*

- 🔵 **Speed Boost:** Overclocks sub-light maneuvering thrusters (+30% agility, max 250%).
- 🔴 **Rapid Fire:** Maximizes plasma cannon cyclic rate (+10% fire rate, max 120%).
- 🟣 **Multi-Cannon:** Quad-plasma spread firing solution (up to 3 simultaneous shots).
- 🟢 **Shield Protection:** Restores reinforced electromagnetic shielding (+1 hull shield point, max 8).
- 🟡 **Tactical Nuke:** Screen-clearing atomic blast vaporizing all active hostiles in a flash of nuclear glory (max 1 per 15-wave stage).

---

## Flight Controls

| Key | Gamepad (Xbox / Standard) | Tactical Action |
| :--- | :--- | :--- |
| <kbd>Space</kbd> | <kbd>A</kbd> / <kbd>RT</kbd> | Fire Primary Plasma Cannons |
| <kbd>←</kbd> / <kbd>→</kbd> | Left Analog / D-Pad | Fly The Spaceship Horizontally |
| <kbd>T</kbd> | <kbd>Y</kbd> | Toggle Ship Model (*Cruiser* vs *Vanguard*) |
| <kbd>LT</kbd> / <kbd>RT</kbd> | <kbd>LT</kbd> / <kbd>RT</kbd> | Cycle Menu Pages (Left / Right) |
| <kbd>-</kbd> / <kbd>+</kbd> | <kbd>LB</kbd> / <kbd>RB</kbd> | Adjust Particle Density (Levels 0 to 4) |
| <kbd>F</kbd> | — | Toggle Fullscreen Mode (Native monitor resolution up to 4K UHD) |
| <kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd> | — | Preset Resolution Scales (`1280x720`, `1600x900`, `1920x1080`) |
| <kbd>I</kbd> / <kbd>B</kbd> | <kbd>B</kbd> | Toggle Telemetry Overlay (CPU%, RAM%, FPS, SDL Driver) |
| <kbd>H</kbd> | <kbd>Back</kbd> | Hall of Fame Leaderboards & Tactical Dossiers |
| <kbd>P</kbd> | <kbd>Start</kbd> | Pause / Resume Active Combat |
| <kbd>S</kbd> | <kbd>Start</kbd> | Launch Match (Start game from menu) |
| <kbd>C</kbd> | — | Cheat Mode (Instant max firepower; disables leaderboard saving) |
| <kbd>Q</kbd> | — | Quit Combat or Return to Desktop |

---

## Local Arcade Hall of Fame (Multi-User)

*Aliens Invaders* honors the classic arcade spirit. On Linux/FreeDesktop systems, scores are saved to standard sandboxed storage (`~/.local/share/aliens-invaders/` on Linux, `%APPDATA%\aliens-invaders\` on Windows, and sandboxed containers on macOS, Android, and iOS).

---

## How to Enlist & Compile

Built with modern **C++20** and native **SDL3**, the engine compiles effortlessly across all major operating systems using either **CMake** or **Make**:

### Command-Line Driver & Audio Options

| Command | Action |
| :--- | :--- |
| `aliens-invaders` | Launch game (prioritizes Vulkan with automatic OpenGL fallback) |
| `aliens-invaders -driver <name>` | Launch with specific graphics driver (`vulkan`, `opengl`, `opengles2`, `software`) |
| `aliens-invaders -drivers` | List all available 2D graphics drivers supported on current display |
| `aliens-invaders -playlist` | Print 15-track classical symphonic soundtrack anthology and exit |
| `aliens-invaders -play <track>` | Play soundtrack movement (1 to 15) in headless terminal mode |
| `aliens-invaders -help` | Display command-line manual and option reference |

---

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

    # 1. Configure and compile

    cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
    cmake --build build -j$(nproc)

    # 2. Run automated test suite & launch

    ./build/unit_tests              # Run automated headless C++20 test suite
    ./build/aliens-invaders         # Launch game

    # 3. System Installation (Optional)

    sudo cmake --install build

    # 4. Uninstallation

    Cleanly remove all installed files using CMake's generated manifest:
    sudo xargs rm -vf < build/install_manifest.txt
---

### Option B: Build with Makefile
    make -j$(nproc)        # Compile release binary (-O2)
    make test              # Run automated C++20 test suite
    make sanitize          # Compile with AddressSanitizer and UBSan
    sudo make install      # Install binary, icons, and desktop entries to system
---

## Author & Copyright

**Claudio Fernandes de Souza Rodrigues**
Lead Engine Architecture, C++20 Systems, Procedural 2.1 Audio, Aerodynamics & Gameplay.
GitHub: [@claudiofsr](https://github.com/claudiofsr)

Copyright (c) 2026 Claudio Fernandes de Souza Rodrigues. All Rights Reserved.

Licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.
