# CS 3 — Tactical Strike

A complete, playable top-down tactical shooter written in **C++17** with **SDL2**,
inspired by the round-based bomb-defusal gameplay of *Counter-Strike*.

> **Scope note:** A literal, 1:1 recreation of Counter-Strike (full 3D engine,
> online netcode, thousands of art assets) is far beyond what a single project
> can deliver. CS 3 instead implements the *core game* faithfully as a 2D
> top-down shooter: two teams, an economy, a bomb objective, AI bots, weapons,
> rounds and a win condition — all in self-contained C++ with no external art
> or font assets.

![gameplay](https://img.shields.io/badge/C%2B%2B17-SDL2-blue)

## Features

- **5v5 Counter-Terrorists vs Terrorists** — you play a Counter-Terrorist; every
  other slot is filled by an AI bot.
- **Bomb-defusal objective** — terrorists carry and plant the bomb on site A or
  B; counter-terrorists must defuse it or eliminate the enemy team.
- **AI bots** with grid pathfinding (BFS), line-of-sight detection, reaction
  delays, strafing, engagement-distance management and objective play
  (planting / defusing / rotating to sites).
- **Weapon sandbox** — knife, pistol, SMG, rifle and sniper, each with their own
  damage, fire rate, magazine, reload time, spread, range and damage falloff.
- **Economy & buy menu** — earn money from kills, round wins, plants and
  defuses; spend it on weapons and kevlar between rounds. Includes loss bonuses.
- **Round flow** — freeze/buy phase, live phase with a round timer, bomb timer,
  round-end summary and a first-to-8 match.
- **HUD** — health, armor, money, weapon/ammo, round timer, score, plant/defuse
  progress bars, a live **minimap** and a **TAB scoreboard** with K/D and money.
- **Hitscan combat** with bullet tracers, muzzle flashes, kevlar damage
  absorption and distance falloff.
- **Zero asset dependencies** — text is drawn with a built-in 5×7 bitmap font,
  so the game needs only SDL2 to build and run.

## Build

### Dependencies

- A C++17 compiler (**g++** recommended)
- CMake ≥ 3.16
- SDL2 development libraries

On Debian/Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libsdl2-dev
```

### Compile

The easiest way:

```bash
./build.sh
```

Or manually:

```bash
CXX=g++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

> **Note:** the script forces `g++` because on some distributions the default
> `clang` toolchain fails to link libstdc++ (`cannot find -lstdc++`). If your
> clang is set up correctly you can build with it too.

### Run

```bash
./build/cs3
```

## Controls

| Input | Action |
|------|--------|
| `W` `A` `S` `D` | Move |
| Mouse | Aim |
| Left click | Shoot (hold for automatic weapons) |
| `R` | Reload |
| `1`–`5` | Select knife / pistol / SMG / rifle / sniper |
| Mouse wheel | Cycle weapons |
| `Shift` | Walk (slower, for accuracy) |
| `E` | Hold to plant (as a T carrier) or defuse (as a CT) |
| `B` | Toggle buy menu (during freeze time) |
| `TAB` | Hold to show the scoreboard |
| `Enter` | Start match / continue from menus |
| `Esc` | Close buy menu / quit from main menu |

## How to win a round

- **Counter-Terrorists** win by eliminating all terrorists, defusing a planted
  bomb, or surviving until the round timer expires.
- **Terrorists** win by eliminating all counter-terrorists or by detonating the
  planted bomb.

First team to **8 rounds** wins the match.

## Project layout

```
src/
  Vec2.h        2D vector math
  Config.h      Tunable constants, colors, team enum
  Font.{h,cpp}  Self-contained 5x7 bitmap text renderer
  Weapon.{h,cpp}Weapon catalogue & stats
  Map.{h,cpp}   Tile map, collision, raycasting, BFS pathfinding, rendering
  Game.{h,cpp}  Entities, player control, bot AI, combat, bomb, rounds, HUD
  main.cpp      Entry point
CMakeLists.txt  Build configuration
build.sh        One-command build helper
```

## Headless simulation / testing

The game can run without a window to stress-test AI, combat and round logic.
It drives every slot with the bot AI and prints round results:

```bash
SDL_VIDEODRIVER=dummy CS3_HEADLESS=120000 ./build/cs3
```

`CS3_HEADLESS` is the maximum number of 60 fps simulation frames to run.
