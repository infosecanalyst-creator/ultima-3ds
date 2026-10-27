# Ultima IV & VI on the New 3DS — with stereoscopic 3D

Both games already run on the 3DS through ScummVM's Ultima engine (xu4 and
Nuvie merged upstream). This project adds the part ScummVM can't do by
itself: **real stereoscopic depth**, so creatures and the party float above
the terrain, roofs/text sit in front, and Ultima IV's first-person dungeons
actually recede into the screen.

**Status: engine side verified with the real game; untested on hardware.**
ScummVM was built natively with this patch and only the Ultima engine, and
Ultima VI was played through it (Journey Onward, walking, combat at the
shrine) under Xvfb with scripted input. The engine's own depth maps were
captured and composited into a stereo pair with the same rules the 3DS
backend uses — see `docs/preview-u6-real-engine-stereo.png`: floor level 0,
altar/objects 1, party and gargoyles 2, the parchment GUI 3, exactly as
designed. Ultima IV's intro resisted scripted input (its menu eats keys
during the title animation), so its in-game frames are not captured yet;
its stamping code is the same mechanism plus the dungeon-distance rule.

## How the 3D works

ScummVM engines hand the backend one flat 320×200 frame, so a depth channel
is threaded alongside it:

1. `common/stereo3ds.h` — a tiny side-channel: a `DepthPlane` (one byte per
   pixel, level 0–7) and a `setDepthMap` hook the 3DS backend fills in.
   On every other platform the hook is null and the calls cost nothing.
2. **Engines tag what they draw.** Nuvie stamps the current level in
   `Screen::blit` (per pixel, honouring tile transparency) and `MapWindow`
   sets the level per pass: terrain = 0, objects = 1, actors = 2,
   roofs/cursor/GUI = 3. Ultima IV stamps in `Image::drawOn/drawSubRectOn`;
   `TileView` marks base terrain 0 (solid terrain such as mountains 1) and
   anything stacked on it (party, creatures, ships) 2; `DungeonView` maps
   draw distance to depth (adjacent walls 6, then 4, 2, far = 0).
3. **The backend composites layers.** `backends/platform/3ds/stereo3ds.cpp`
   adds a right-eye render target, splits the game texture into one RGBA
   layer per level, inpaints the holes objects leave in the ground layer, and
   draws the stack twice with a per-eye horizontal shift of
   `(level − 1) × 2.5 px × slider`. Level 1 is the screen plane; ground sinks
   slightly in, actors and text pop out. The 3D slider controls strength;
   at 0 it's plain 2D.

Everything is in `tools/patch_scummvm.py` (anchored edits, idempotent) plus
three new files. Upstream diff is ~150 lines across 11 files.

## Compositor test (no 3DS needed)

```bash
cd tools && g++ -std=c++11 -I../source/backends/platform/3ds test_compose.cpp -o test_compose
./test_compose /tmp/out    # prints shifts per layer, hole/fringe counts; writes eye_left/right.ppm
```

The pixel logic (`stereo_compose.h`: layer split, ground inpainting incl.
full-width text rows, per-eye shift) is shared with the backend and passes
this test with zero holes.

## Real-engine test on a desktop

```bash
./configure --disable-all-engines --enable-engine=ultima --enable-engine=ultima6 --enable-engine=ultima4
make -j8
STARTUP=12 EVERY=3 tools/desktop/drive.py ./scummvm ultima6 /path/to/U6 /tmp/u6 \
   "Escape 3 Escape 3 click:0.54:0.88 8 shot Right 1 Up 1 shot"
tools/desktop/make_stereo.py /tmp/u6/game.png /tmp/u6/depth_00379.pgm /tmp/u6/stereo 3
```

`drive.py` runs the game under Xvfb, sends xdotool keys/clicks, and takes
screenshots; with `STEREO3DS_DUMP` set (it sets it) the depth hook in
`common/stereo3ds.cpp` writes the engine's depth map as PGMs, and
`make_stereo.py` composites a screenshot + depth map into left/right eyes.

## Controls and in-game manual (3DS port)

The game fills the touch screen; the top screen is kept free. **Start opens
a built-in manual** (text on top, section buttons on the touch screen; the
game waits). Its text is `docs/manual.txt`, compiled into
`backends/platform/3ds/manual_text.h`; a `manual.txt` in `sdmc:/3ds/scummvm/`
replaces it without rebuilding.

| Button | Action |
|---|---|
| Touch / Circle Pad / A / B | click / cursor / left click / right click |
| D-pad | walk (Ultima IV and VI) |
| X | talk (U4 menus: confirm) |
| Y | cancel / Escape (U6: game menu) |
| ZL / ZR | attack / U4 enter, U6 use |
| L | ScummVM menu (save, load, quit) — moved here from Start |
| R | drag / hover touch mode |
| Select | on-screen keyboard |
| Start | instruction manual |

## Build

### On a Windows PC with devkitPro (the route used here)

`scummvm-src.tgz` is a trimmed ScummVM master checkout with the stereo patch
already applied: Ultima engine + 3DS and SDL backends + the engine data it
needs (~45 MB packed instead of 560 MB). In the devkitPro MSYS2 shell:

```bash
bash /c/devkitPro/3ds-projects/ultima-3ds/build-ultima.sh
```

It configures with `--host=3ds --disable-all-engines --enable-engine=ultima,ultima4,ultima6`
(the sub-engines must be named explicitly, or ScummVM reports "game id not supported"),
builds `scummvm.3dsx` (romfs holds the GUI themes and `ultima.dat`) and copies
it to `SD-CARD/3ds/scummvm/`. Needs the portlibs `3ds-libpng 3ds-libjpeg-turbo
3ds-libmad 3ds-libogg 3ds-libvorbisidec 3ds-zlib`; FreeType is disabled on
purpose (no `fonts.dat` needed, the built-in GUI font is used).

### Docker (alternative)

```powershell
cd ultima-3ds
docker build -t ultima-3ds .
docker run --rm -v "${PWD}:/work" ultima-3ds ./build-3ds.sh
```

First run clones ScummVM, applies the patch, configures for the 3DS with
**only the Ultima engine** (static — no plugin loading, smaller, faster to
start) and builds `out/3ds/scummvm/scummvm.3dsx`.

### On the card

Copy `scummvm.3dsx` to `sdmc:/3ds/scummvm/`. Put the GOG/Steam `ULTIMA4` and
`ULTIMA6` folders anywhere on the card and add them from the ScummVM launcher
like any other game. The 3D slider controls the depth strength.

## What to expect on first boot

- Launcher and menus: flat (no depth map yet) — that's correct.
- In U6, push the slider up: the Avatar and NPCs should stand off the ground,
  trees and furniture slightly, roofs and the command bar text in front.
- In U4 overworld: creatures/party pop; mountains a hair above the plains.
  Enter a dungeon: the corridor should have real depth per wall segment.
- Fringes: the ground layer is inpainted horizontally so parallax doesn't
  show black edges. If you see haloes around sprites, lower `kShift` in
  `stereo3ds.cpp` or reduce the slider.

## Tuning knobs

| Where | What |
|---|---|
| `stereo3ds.cpp` → `kShift` | parallax per level at full slider (2.5 px) |
| `map_window.cpp` level calls | which Nuvie pass gets which level |
| `tileview.cpp` | U4 level rule (base/opaque/stacked) |
| `dungeonview.cpp` → `u4DungeonLevel` | dungeon distance → depth curve |
| `Stereo3DS::kLevels` | max layers (8); more layers = more texture uploads |

## Known gaps

- The overlay (ScummVM GUI, virtual keyboard) and mouse cursor are drawn at
  the screen plane in both eyes — fine, just not "in front".
- Nuvie's lighting overlay (`blitalphamap8`) and roofs drawn via surface
  blits don't stamp depth; they inherit whatever was under them.
- U4 stamps whole 16×16 cells for stacked tiles. That matches the game:
  Ultima IV draws a creature tile *instead of* the terrain (black
  background), so the cell really is the creature.
- Performance: 4–7 extra 320×200 RGBA texture uploads per frame. Trivial on
  the New 3DS; untested on Old 3DS (and Old 3DS can't run U6 well anyway).
- `--enable-release-mode` strips asserts; drop it while debugging.
