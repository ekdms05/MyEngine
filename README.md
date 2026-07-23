# MyEngine

A 2.5D pixel-art game engine for **Tales Weaver–style games** — 2D dot/pixel
characters living in a world that mixes 2D layers and 3D geometry, sorted
together in a **single depth buffer**. Built in C++20 on DirectX 11, with a
Dear ImGui editor, Lua scripting, and extensibility as its top design goal.

> 한국어 설계 문서는 [`docs/`](docs/) 아래에 있습니다 (기능·UI·MCP 설계 + 아키텍처 개요).

## Status

The engine is being built milestone by milestone. Each milestone is gated on a
**visible, pixel-verified demo**, not just "code compiles".

| Milestone | Goal | State |
|-----------|------|-------|
| **M0** | Window + DX11 triangle | ✅ Done |
| **M1** | Pixel-perfect sprites, WASD dot character | ✅ Done |
| **M2** | Hybrid scene core — ECS, tilemap (height/bridge), physics, **hybrid depth rendering** | ✅ Done |
| **M3** | 8-direction animation, audio, **Lua scripting**, hot reload | ✅ Done |
| **M4** | Reflection-driven ImGui editor — inspector, scene save, undo, play mode | ✅ Done |
| **M5** | Content tools — tilemap/animation editors, Korean text, in-game UI, pathfinding | ✅ Done |
| **M6** | Vertical slice — dialogue, cutscenes, NPCs → a Tales Weaver-style demo | ✅ Done |

The M2 demo already proves the hardest technical risk: a character can walk
**over a bridge while another walks under it**, and step **behind a 3D statue**
with correct per-pixel occlusion — all 2D sprites and 3D meshes sharing one
depth buffer.

The **M6 vertical slice** (`samples/village_demo`) is the roadmap's final
deliverable: a dot character walks a Tales-Weaver-style village — sloped hill,
a one-way bridge over a creek, 3D fountain/statue props — and talks with three
NPCs (chief, merchant, guard) in **Korean**, complete with branching choices, an
opening cutscene, wandering NPCs, footsteps and BGM. Map, dialogue (localization
keys) and game logic (per-entity Lua) are all editable **without rebuilding the
engine** (editor + Lua hot reload).

## Key features

- **Hybrid 2D + 3D rendering** — pixel-art sprites and 3D meshes sorted in a
  single depth buffer via alpha-cutout depth writes + Y-sort-to-depth encoding
  and anchor-biased mesh depth (bridges, slopes, occlusion behind 3D objects).
- **Pixel-perfect pipeline** — 960×540 internal render target, integer upscale
  with letterbox, pixel-snapped camera with sub-pixel scroll (PPU 48).
- **Own RHI over DirectX 11** — opaque handles, bind groups, monolithic PSOs;
  abstracted so DX12/Vulkan backends can be added later.
- **Sparse-set ECS** — 64-bit entity handles, 5-phase scheduler, command buffer,
  transform hierarchy; runtime component registration for scripts/plugins.
- **Tilemap** — 32×32 chunks, multi-column cells for bridges, integer height +
  slopes.
- **Lightweight 2D physics** — AABB/circle colliders, spatial hash,
  move-and-slide, floor-level filtering, trigger events.
- **Reflection + serialization** — non-intrusive `Reflect<T>`, JSON archive with
  versioning, property paths.
- **Hot-reloadable assets** — file watcher → reimport → in-place handle swap.
- **Content import** — PNG (stb_image), glTF static meshes (cgltf),
  Aseprite/spritesheet + atlas packing, WAV/OGG audio.
- **Animation** — 8-direction directional sets, data-driven state machine,
  animation events (footsteps, hit frames).
- **Audio** — miniaudio backend, buses, cues with polyphony, BGM crossfade,
  positional panning.
- **Lua scripting** (in progress) — single sol2 VM, per-entity script
  components, error isolation, hot reload with state survival, coroutines.
- **MCP dev-tools server** — a Model Context Protocol server (`tools/mcp`) that
  lets AI agents build, test, run the engine and **see rendered frames as
  images**.

## Building

Requirements: **Windows**, **Visual Studio 2022/2026** (C++20 toolchain),
**CMake ≥ 3.26**.

```sh
cmake -S . -B build/dev -G "Visual Studio 18 2026" -A x64
cmake --build build/dev --config Debug
```

Run a demo (from the build tree):

```sh
build/dev/samples/sprite_demo/Debug/sprite_demo.exe      # M1: WASD dot character
build/dev/samples/bridge_demo/Debug/bridge_demo.exe      # M2: bridge over/under, 3D occlusion
build/dev/samples/character_demo/Debug/character_demo.exe # M3: 8-dir anim, Lua, footsteps, hot reload
build/dev/samples/village_demo/Debug/village_demo.exe    # M6: full vertical slice (see below)
```

### village_demo — the M6 vertical slice

`village_demo` is the roadmap's final demo: one executable that walks the whole
engine stack (RHI, ECS/scene, tilemap, physics, animation, audio, Lua scripting,
runtime dialogue/cutscene/NPC systems, Korean text, pixel-perfect hybrid render).

```sh
# Interactive: WASD to move, E to talk to an NPC, Esc to quit.
build/dev/samples/village_demo/Release/village_demo.exe

# Deterministic verification scenarios (headless-friendly, auto-exit):
village_demo.exe --scenario walk    --frames 45           # player renders + moves
village_demo.exe --scenario bridge  --frames 15           # player on the bridge (floor 1)
village_demo.exe --scenario talk    --frames 15 --dump-ui talk.bmp   # Korean dialogue box + choices
village_demo.exe --scenario wander  --frames 90           # NPCs wander the plaza
village_demo.exe --scenario behind_prop --frames 15 --dump prop.bmp # occlusion behind the 3D statue
```

CLI flags: `--frames N` (run N frames then exit 0), `--dump path.bmp` (dump the
last frame, no overlay), `--dump-ui path.bmp` (dump including the dialogue box),
`--scenario <name>`, `--headless`. Assets (map, character/tile PNGs, glTF props,
audio) are regenerated deterministically by
`samples/village_demo/tools/make_all.ps1`; map/dialogue/localization JSON and the
Lua scripts under `samples/village_demo/assets/` are hand-authored and
hot-reloadable.

Run the tests:

```sh
build/dev/tests/Debug/mye_tests.exe
```

> Use the `Visual Studio 17 2022` generator instead if that's what you have
> installed. Everything is static-linked; no runtime dependencies to install.

## Repository layout

```
engine/
  core/      mye_core   — math, log, json, config, events, module/app loop, win32, input, jobs
  rhi/       mye_rhi    — RHI abstraction + DirectX 11 backend
  reflect/   mye_reflect— reflection + JSON serialization
  asset/     mye_asset  — asset DB, VFS, importers (PNG/glTF/Aseprite/audio), hot reload
  render/    mye_render — sprite batch, camera, pixel-perfect target, hybrid depth renderer
  scene/     mye_scene  — ECS, tilemap, physics, animation, render extract
  audio/     mye_audio  — miniaudio backend, mixer, buses, cues
  imgui/     mye_imgui  — Dear ImGui DX11/win32 backend + debug overlay
  script/    mye_script — Lua (sol2) runtime + bindings
  ui/        mye_ui     — in-game UI widgets + Korean text (FreeType) stack
  runtime/   mye_runtime— dialogue, cutscene, NPC, save, localization, scene transition
samples/     runnable demos (hello_triangle, sprite_demo, bridge_demo,
             character_demo, village_demo)
tests/       mye_tests  — unit + integration tests
tools/mcp/   Model Context Protocol dev-tools server (TypeScript)
docs/        design documents (Korean) + architecture overview
third_party/ vendored dependencies
```

Conventions: C++20, namespace `mye`, static linking, UTF-8 throughout, no
exceptions in engine code (`Expected<T, Error>`), `/W4`. Coordinate system is
left-handed, +Y up, PPU 48; see `docs/02-rendering.md` for the authoritative
spec.

## Design docs

The `docs/` directory contains the full design (in Korean):
`00-overview.md` (architecture, layers, roadmap), then one document per
subsystem (`01` core, `02` rendering, `03` scene/ECS, `04` assets, `05`
scripting/plugins, `06` runtime systems, `07` editor/UI, `08` MCP).

## Third-party

Vendored under `third_party/`, each under its own license:

- [stb_image](https://github.com/nothings/stb) — PNG decode (public domain / MIT)
- [Dear ImGui](https://github.com/ocornut/imgui) (docking) — editor UI (MIT)
- [cgltf](https://github.com/jkuhlmann/cgltf) — glTF parsing (MIT)
- [miniaudio](https://github.com/mackron/miniaudio) — audio playback (public domain / MIT-0)
- [stb_vorbis](https://github.com/nothings/stb) — OGG decode (public domain / MIT)
- [Lua](https://www.lua.org/) — scripting language (MIT)
- [sol2](https://github.com/ThePhD/sol2) — Lua/C++ binding (MIT)

## License

[MIT](LICENSE). Do what you like with it, keep the copyright notice.

---

*This engine is being developed with [Claude Code](https://claude.com/claude-code).*
