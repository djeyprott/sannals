# Stellar Annals

Milestone 1 (headless sim) and milestone 2 (basic viewer).

The simulation core generates a galaxy from a seed, runs it forward day by day and prints the event log as events happen. Runs never end unless you pass `--years`.

## Download

Ready-to-run builds for Windows, macOS (Apple Silicon and Intel) and Linux are on the [Releases page](https://github.com/djeyprott/sannals/releases). Each download has two programs:

- `stellar_annals_viewer`: the galaxy map and chronicle.
- `stellar_annals`: the text-only runner, for long or batch runs.

On macOS the builds aren't signed yet. The first time, right-click the program and choose Open, or run `xattr -dr com.apple.quarantine .` in its folder.

To publish a new release: open the **Actions** tab, choose **Build and release**, click **Run workflow** and enter a version such as `v0.3.0`. Pushing a `v*` tag does the same.

## Build from source

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Needs CMake 3.20+ and a C++20 compiler (GCC 11+, Clang 14+ or MSVC 2022). The first configure downloads GLFW 3.4 and Dear ImGui 1.91.5 from GitHub.

- **Linux:** install the X11 and OpenGL headers first, e.g. on Ubuntu: `sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`
- **Windows and macOS:** nothing extra.
- **Headless only:** add `-DSTELLAR_BUILD_VIEWER=OFF` to skip the viewer and its downloads.

## Viewer

```
./build/stellar_annals_viewer --seed 42 --size medium
```

- **Map:** drag to pan, mouse wheel to zoom, F to fit. Stars are coloured by class; claimed systems get their empire's colour. Colony ships move along the lanes.
- **Time:** Space pauses; keys 1-6 set the speed from 1 day to 5 years per second.
- **Chronicle:** every name is a link. Click one, or a star, to open it in Details.
- **Details:** stats for the selected system, world, species or empire, plus a History list of every event that mentions it.

The viewer only reads the world and asks the sim to step; it never changes world state itself.

## Run

```
./build/stellar_annals                                  # random seed, small galaxy, runs forever
./build/stellar_annals --seed 42 --years 400 --summary  # a repeatable 400-year history
./build/stellar_annals --size huge --detail major       # only the big events
./build/stellar_annals --detail minor --links           # everything, names as [Name](kind/id) links
```

## Layout

| File | Role |
| --- | --- |
| `src/rng.h` | Deterministic random numbers (xoshiro256**), with independent streams per subsystem |
| `src/names.*` | Syllable-based names for systems, species and bodies |
| `src/world.*` | All world data: systems, bodies, species, colonies, empires, ships |
| `src/event_log.*` | Event records with entity references, rendered as plain text or links |
| `src/galaxy_gen.*` | Spiral galaxy, bodies, jump lanes (always connected), species homeworlds |
| `src/sim.*` | Updates on three schedules: daily, monthly and yearly |
| `src/main.cpp` | Headless runner: command-line options, live event printing, end-of-run summary |
| `viewer/main.cpp` | Window, OpenGL and Dear ImGui setup, main loop |
| `viewer/viewer.*` | Galaxy map, time controls, chronicle, empires table, details panel |

## What the sim does now

- **Daily:** colony ships travel along jump lanes.
- **Monthly:** population grows toward each world's limit for that species; colonies mine minerals into the empire's stockpile, and deposits run dry.
- **Yearly:** species before spaceflight advance toward it and found empires when they get there; empires spend minerals on colony ships aimed at the best reachable world; population milestones are logged.

## Known gaps (on purpose, for later milestones)

- Empires stop expanding once nearby good worlds are taken, and minerals pile up. That surplus is meant for milestones 2 to 4: people, politics and fleets.
- There's no conflict yet, so empires never fall. That's milestone 3.
