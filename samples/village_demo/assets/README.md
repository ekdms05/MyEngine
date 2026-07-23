# village_demo assets — content spec (M6-B)

Tales-Weaver-style village vertical-slice content. All binary assets are produced by deterministic
generators in `../tools`; data files (JSON + Lua) are hand-authored. The **integration agent** owns
the C++ (`samples/village_demo/src/main.cpp` + CMake) and loads everything below.

## Regenerate binaries
```
powershell -ExecutionPolicy Bypass -File ../tools/make_all.ps1
```
(tiles+chars+audio via System.Drawing / WAV synth; props via `python make_props.py`.)

## Coordinate convention (docs/00, bridge_demo)
1 world unit == 1 cell == PPU48 px, left-handed, **+Y up**. HybridRenderer maps cell `(cx,cy)` →
world `(x=cx, yBottom=-cy)`. **To place at world Y=W, use cellY = -W.** 960×540 pixel-perfect.

## Assets

### Tiles (48×48, one PNG per role — bind as a distinct tileset texture per layer)
| file | role | walkable | notes |
|------|------|----------|-------|
| `tile_grass.png`  | ground | yes | base field |
| `tile_path.png`   | ground | yes | dirt road |
| `tile_water.png`  | water  | **no** | the creek (impassable) |
| `tile_bridge.png` | bridge | yes | one-way top deck, layer 1 (floor1 band) |
| `tile_slope.png`  | slope  | yes | hill depth ramp |
| `tile_plaza.png`  | ground | yes | stone plaza around fountain/statue |
| `white.png`       | —      | —   | 1×1 white, mesh albedo tint base |

### Characters (96×256 sheets = 4 cols × 8 rows, cell 24×32, foot pivot (12,32))
Row = Dir8 `0 down,1 down_left,2 left,3 up_left,4 up,5 up_right,6 right,7 down_right`;
col = walk frame 0..3. Build clips with `SpriteSheetImporter::SliceGrid` (row-major), walk 0..3 loop
@0.12s, idle = frame 0; attach `footstep` AnimEventMarkers at slots **1 and 3** (cue `cue_footstep`).
- `player_sheet.png` (blue), `npc_chief.png` (purple), `npc_merchant.png` (orange), `npc_guard.png` (steel).

### Props (glTF 2.0 `.glb`, right-handed → MeshImporter converts to LH)
- `mesh/statue.glb` — plinth+pillar monument (~1×2 units), origin at ground (y=0), grows +Y.
- `mesh/fountain.glb` — low wide basin (~2×0.6 units). Render as `MeshRenderer` + `AnchorBiased`.

### Audio (WAV, PCM S16 mono 44100 Hz)
- `audio/village_bgm.wav` — 8s looping BGM (`Music().Play`, crossfade-friendly).
- `audio/footstep.wav` — footstep SFX (AudioCue `cue_footstep`, bus SFX, random pitch).
- `audio/dialogue_blip.wav` — dialogue text-advance blip.

### Map — `maps/village.json` (schema `village_map/v1`)
Hand schema for the integration agent to build `tilemap::TilemapWorld` + entities. Contains:
`tilesets`, `cellBounds`, `layers[]` (ground layer with `fill`+`rects`+`slopes`; bridge layer with
`bridgeCells` for the floor1 deck), `props[]`, `spawns` (player + 3 npcs with sheet/script/dialogue),
`camera`, `audio`. Layout: grass field, N-S + E-W dirt roads, a vertical creek at worldX 5..6 crossed
E-W by a bridge at worldY 0, a west hill slope (worldX −13..−11, worldY −2..+2, maxRise 2u), and a
plaza (worldX −9..−6, worldY 3..6) holding the fountain (−8,3.5) and statue (−7,5).

### Dialogue — `dialogue/*.json` (matches `mye::runtime::DialogueScript` JsonArchive schema)
`chief.json` (greet→ask→[village|bridge|leave] menu loop), `merchant.json` (greet→[buy|rumor|leave]
loop), `guard.json` (greet + pass/stay branch). Every `speaker`/`body`/`choice.text` is a
`LocalizedText{key,literal}` addressing a loc key. Load with `LoadDialogueJson`. These JSON files
double as the canonical dialogue source; the Lua NPC scripts drive the same keys for hot-reload.

### Localization — `loc/ko.json`, `loc/en.json` (flat key→string, `LoadTableJson`)
Korean is the primary locale. Includes all `dialogue.*`, `npc.*.name`, and `ui.*` keys. Load ko into
`Locale::Ko` and en into `Locale::En`; `SetLocale(Ko)`.

### Scripts — `scripts/*.lua` (docs/05 contract, hot-reloadable)
- `player.lua` — WASD 8-dir controller, drives animator (`isMoving`/`speed`), keeps audio listener,
  publishes `_player_pos`, freezes while `mye.dialogue.is_active()`.
- `npc_common.lua` — module: `proximity`, `want_interact` (E key), localized `say_key`/`choose_keys`,
  `run`/`pump` coroutine helpers.
- `npc_chief.lua` / `npc_merchant.lua` / `npc_guard.lua` — component classes; on E within
  `interactRadius`, face player and run a cutscene coroutine mirroring the matching dialogue JSON.
- `intro_cutscene.lua` — module with `play(entityIds)`: camera focus → chief greeting → optional
  chief `move_to` → camera back to player. Launch once on boot via `mye.co.start`.

## Integration checklist (for the code agent)
1. Mount `assets://` → `samples/village_demo/assets`. Register Texture + Mesh importers.
2. Parse `maps/village.json`; build ground `TilemapWorld` (layer 0) + separate bridge `TilemapWorld`
   (extract to `FloorLevelToSortLayer(1)`), like bridge_demo. Apply `tile_*` textures per role/layer.
3. Spawn player + NPCs from `spawns` (SpriteRenderer + SpriteAnimator + ScriptComponent + FloorLevel;
   pivot (12,32)). Place statue/fountain as MeshRenderer (AnchorBiased) from `props`.
4. Load `loc/ko.json`→Ko, `loc/en.json`→En; `SetLocale(Ko)`. Load `dialogue/*.json` if consuming the
   data path (else Lua keys suffice). Wire `cue_footstep` AudioCue + `village_bgm.wav` via Music.
5. Complete `RuntimeModule::OnPostInitialize` wiring (DialogueBox/DialogueSystem/MoveController/
   CameraFocus/AudioListener), register `RuntimeBindings`, add phase ticks. Kick `intro_cutscene`.
