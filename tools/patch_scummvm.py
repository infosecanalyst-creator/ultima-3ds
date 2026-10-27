#!/usr/bin/env python3
"""Apply the ultima-3ds stereoscopic patch to a ScummVM checkout.

Usage: patch_scummvm.py <path to scummvm repo root>

Idempotent (each edit checks its marker). Anchors are short upstream
lines; if one has moved, the script stops and names it.

What it does:
  common/stereo3ds.h            depth side-channel (new file)
  common/stereo3ds.cpp          definition of the hook variable (new)
  backends/platform/3ds/*       right-eye render target + layer compositor
  engines/ultima/nuvie/*        depth stamping in Screen::blit + MapWindow levels
  engines/ultima/ultima4/*      depth stamping in Image blits + tile/dungeon levels
"""
import os
import shutil
import sys

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.join(HERE, "..", "source")


def rw(fname, old, new, marker):
    path = os.path.join(ROOT, fname)
    text = open(path, encoding="utf-8").read()
    if marker in text:
        print("  = %-45s already patched" % fname)
        return
    if old not in text:
        raise SystemExit("ANCHOR NOT FOUND in %s -- edit by hand:\n-----\n%s\n-----" % (fname, old))
    assert marker in new
    open(path, "w", encoding="utf-8").write(text.replace(old, new, 1))
    print("  + %-45s %s" % (fname, marker.strip().splitlines()[0][:40]))


def copy(rel):
    src = os.path.join(PORT, rel)
    dst = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy(src, dst)
    print("  + %-45s copied" % rel)


# ------------------------------------------------------------------ common
copy("common/stereo3ds.h")
copy("common/stereo3ds.cpp")
rw("common/module.mk",
   "MODULE_OBJS := \\\n",
   "MODULE_OBJS := \\\n\tstereo3ds.o \\\n",
   "stereo3ds.o")

# ----------------------------------------------------------------- backend
copy("backends/platform/3ds/stereo3ds.cpp")
copy("backends/platform/3ds/stereo3ds.h")
copy("backends/platform/3ds/stereo_compose.h")
rw("backends/platform/3ds/module.mk",
   "MODULE_OBJS := \\\n",
   "MODULE_OBJS := \\\n\tstereo3ds.o \\\n",
   "stereo3ds.o")

G = "backends/platform/3ds/osystem-graphics.cpp"
rw(G,
   '#include "backends/platform/3ds/osystem.h"\n',
   '#include "backends/platform/3ds/osystem.h"\n#include "backends/platform/3ds/stereo3ds.h"\n',
   '#include "backends/platform/3ds/stereo3ds.h"')
rw(G,
   "\tC3D_DepthTest(false, GPU_GEQUAL, GPU_WRITE_ALL);\n\tC3D_CullFace(GPU_CULL_NONE);\n",
   "\tC3D_DepthTest(false, GPU_GEQUAL, GPU_WRITE_ALL);\n\tC3D_CullFace(GPU_CULL_NONE);\n\n"
   "\tstereo3dsInit();\n",
   "\tstereo3dsInit();")
rw(G,
   "\tC3D_RenderTargetDelete(_renderTargetTop);\n",
   "\tstereo3dsShutdown();\n\tC3D_RenderTargetDelete(_renderTargetTop);\n",
   "\tstereo3dsShutdown();")
rw(G,
   "\tC3D_FrameBegin(0);\n\t\t_gameTopTexture.transfer();\n",
   "\tC3D_FrameBegin(0);\n\t\t_gameTopTexture.transfer();\n\t\tstereo3dsPrepare(_gameTopTexture);\n",
   "stereo3dsPrepare(_gameTopTexture);")
rw(G,
   "\t\t// Render top screen\n"
   "\t\tC3D_RenderTargetClear(_renderTargetTop, C3D_CLEAR_ALL, 0x00000000, 0);\n"
   "\t\tC3D_FrameDrawOn(_renderTargetTop);\n"
   "\t\tif (_screen == kScreenTop || _screen == kScreenBoth) {\n"
   "\t\t\tC3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionTop);\n"
   "\t\t\tC3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _modelviewLocation, _gameTopTexture.getMatrix());\n"
   "\t\t\t_gameTopTexture.setFilteringMode(_magnifyMode != MODE_MAGON && _filteringEnabled);\n"
   "\t\t\t_gameTopTexture.render();\n",
   "\t\t// Render top screen (once per eye when the 3D slider is up)\n"
   "\t\tconst int stereoEyes = stereo3dsEyes();\n"
   "\t\tfor (int eyeIdx = 0; eyeIdx < stereoEyes; ++eyeIdx) {\n"
   "\t\tconst int eye = (eyeIdx == 0) ? -1 : 1;\n"
   "\t\tC3D_RenderTarget *topTarget = (eyeIdx == 0) ? _renderTargetTop : stereo3dsTarget();\n"
   "\t\tC3D_RenderTargetClear(topTarget, C3D_CLEAR_ALL, 0x00000000, 0);\n"
   "\t\tC3D_FrameDrawOn(topTarget);\n"
   "\t\tif (_screen == kScreenTop || _screen == kScreenBoth) {\n"
   "\t\t\tC3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionTop);\n"
   "\t\t\t_gameTopTexture.setFilteringMode(_magnifyMode != MODE_MAGON && _filteringEnabled);\n"
   "\t\t\tstereo3dsRenderGame(eye, _gameTopTexture, _modelviewLocation);\n",
   "stereo3dsRenderGame(eye, _gameTopTexture, _modelviewLocation);")
rw(G,
   "\t\t\t\t_cursorTexture.render();\n\t\t\t}\n\t\t}\n\n\t\t// Render bottom screen\n",
   "\t\t\t\t_cursorTexture.render();\n\t\t\t}\n\t\t}\n\t\t} // stereo eyes\n\n\t\t// Render bottom screen\n",
   "} // stereo eyes")

# ------------------------------------------------------------------- nuvie
SH = "engines/ultima/nuvie/screen/screen.h"
SC = "engines/ultima/nuvie/screen/screen.cpp"
MW = "engines/ultima/nuvie/gui/widgets/map_window.cpp"

rw(SH,
   "class Screen {\nprivate:\n\tconst Configuration *config;\n",
   "class Screen {\npublic:\n\t// ultima-3ds: per-pixel depth for stereoscopic 3D (see common/stereo3ds.h)\n"
   "\tStereo3DS::DepthPlane depth;\nprivate:\n\tconst Configuration *config;\n",
   "Stereo3DS::DepthPlane depth;")
# include goes right after the first #include in the header
text = open(os.path.join(ROOT, SH), encoding="utf-8").read()
if '#include "common/stereo3ds.h"' not in text:
    i = text.find("#include ")
    text = text[:i] + '#include "common/stereo3ds.h"\n' + text[i:]
    open(os.path.join(ROOT, SH), "w", encoding="utf-8").write(text)
    print("  + %-45s include" % SH)

rw(SC,
   "\t_renderSurface = CreateRenderSurface(_rawSurface);\n}\n",
   "\t_renderSurface = CreateRenderSurface(_rawSurface);\n\n"
   "\tdepth.create(width, height);\n\tdepth.setLevel(Stereo3DS::kFront); // GUI by default; MapWindow lowers it\n}\n",
   "depth.create(width, height);")
rw(SC,
   "\tif (_renderSurface->bits_per_pixel == 16) {\n\t\tif (opacity < 255)\n\t\t\treturn blit16WithOpacity(",
   "\t// ultima-3ds: tag the pixels this blit touches with the current depth level\n"
   "\tif (depth.valid()) {\n"
   "\t\tif (!trans) {\n"
   "\t\t\tdepth.stampRect(dest_x, dest_y, src_w, src_h);\n"
   "\t\t} else {\n"
   "\t\t\tconst byte *sp = src_buf;\n"
   "\t\t\tfor (int i = 0; i < src_h; i++, sp += src_pitch) {\n"
   "\t\t\t\tconst int yy = dest_y + i;\n"
   "\t\t\t\tif (yy < 0 || yy >= depth.height()) continue;\n"
   "\t\t\t\tfor (int j = 0; j < src_w; j++) {\n"
   "\t\t\t\t\tconst int xx = dest_x + j;\n"
   "\t\t\t\t\tif (sp[j] != 0xff && xx >= 0 && xx < depth.width())\n"
   "\t\t\t\t\t\tdepth.stamp(xx, yy);\n"
   "\t\t\t\t}\n"
   "\t\t\t}\n"
   "\t\t}\n"
   "\t}\n\n"
   "\tif (_renderSurface->bits_per_pixel == 16) {\n\t\tif (opacity < 255)\n\t\t\treturn blit16WithOpacity(",
   "tag the pixels this blit touches")
rw(SC,
   "bool Screen::fill(uint8 colour_num, uint16 x, uint16 y, sint16 w, sint16 h) {\n",
   "bool Screen::fill(uint8 colour_num, uint16 x, uint16 y, sint16 w, sint16 h) {\n"
   "\tdepth.stampRect(x, y, w, h); // ultima-3ds\n",
   "depth.stampRect(x, y, w, h); // ultima-3ds")
rw(SC,
   "bool Screen::clear(sint16 x, sint16 y, sint16 w, sint16 h, Common::Rect *clip_rect) {\n",
   "bool Screen::clear(sint16 x, sint16 y, sint16 w, sint16 h, Common::Rect *clip_rect) {\n"
   "\tdepth.stampRect(x, y, w, h); // ultima-3ds (clear = blacked-out ground)\n",
   "(clear = blacked-out ground)")
rw(SC,
   "void Screen::update() {\n\t_rawSurface->markAllDirty();\n",
   "void Screen::update() {\n\tdepth.push(); // ultima-3ds\n\t_rawSurface->markAllDirty();\n",
   "\tdepth.push(); // ultima-3ds\n\t_rawSurface->markAllDirty();")
text = open(os.path.join(ROOT, SC), encoding="utf-8").read()
marker = "depth.push(); // ultima-3ds (partial)"
if marker not in text:
    i = text.find("void Screen::update(int x, int y, uint16 w, uint16 h) {\n")
    assert i > 0, "partial update anchor"
    j = text.find("\t_rawSurface->update();", i)
    assert j > 0
    text = text[:j] + "\t" + marker + "\n" + text[j:]
    open(os.path.join(ROOT, SC), "w", encoding="utf-8").write(text)
    print("  + %-45s partial update push" % SC)

rw(MW,
   "void MapWindow::Display(bool full_redraw) {\n",
   "void MapWindow::Display(bool full_redraw) {\n"
   "\tStereo3DS::ScopedLevel _stereoGround(screen->depth, Stereo3DS::kGround); // ultima-3ds\n",
   "_stereoGround(screen->depth, Stereo3DS::kGround)")
rw(MW,
   "void MapWindow::drawObjs() {\n",
   "void MapWindow::drawObjs() {\n"
   "\tStereo3DS::ScopedLevel _stereoObj(screen->depth, Stereo3DS::kObject); // ultima-3ds\n",
   "_stereoObj(screen->depth, Stereo3DS::kObject)")
rw(MW,
   "inline void MapWindow::drawActor(const Actor *actor) {\n",
   "inline void MapWindow::drawActor(const Actor *actor) {\n"
   "\tStereo3DS::ScopedLevel _stereoActor(screen->depth, Stereo3DS::kActor); // ultima-3ds\n",
   "_stereoActor(screen->depth, Stereo3DS::kActor)")
rw(MW,
   "void MapWindow::drawRoofs() {\n",
   "void MapWindow::drawRoofs() {\n"
   "\tStereo3DS::ScopedLevel _stereoRoof(screen->depth, Stereo3DS::kFront); // ultima-3ds\n",
   "_stereoRoof(screen->depth, Stereo3DS::kFront)")
rw(MW,
   "\tif (show_cursor) {\n\t\tscreen->blit(area.left + cursor_x * 16, area.top + cursor_y * 16, (byte *)cursor_tile->data",
   "\tif (show_cursor) {\n\t\tStereo3DS::ScopedLevel _stereoCur(screen->depth, Stereo3DS::kFront); // ultima-3ds\n"
   "\t\tscreen->blit(area.left + cursor_x * 16, area.top + cursor_y * 16, (byte *)cursor_tile->data",
   "_stereoCur(screen->depth, Stereo3DS::kFront)")

# ----------------------------------------------------------------- ultima4
IM = "engines/ultima/ultima4/gfx/image.cpp"
U4S = "engines/ultima/ultima4/gfx/screen.h"
U4SC = "engines/ultima/ultima4/gfx/screen.cpp"
TV = "engines/ultima/ultima4/views/tileview.cpp"
DV = "engines/ultima/ultima4/views/dungeonview.cpp"

rw(U4S,
   "class Screen : public Graphics::Screen {\nprivate:\n",
   "class Screen : public Graphics::Screen {\npublic:\n"
   "\t// ultima-3ds: per-pixel depth for stereoscopic 3D (see common/stereo3ds.h)\n"
   "\tStereo3DS::DepthPlane _depth;\n"
   "\tvoid update() override;\nprivate:\n",
   "Stereo3DS::DepthPlane _depth;")
text = open(os.path.join(ROOT, U4S), encoding="utf-8").read()
if '#include "common/stereo3ds.h"' not in text:
    i = text.find("#include ")
    text = text[:i] + '#include "common/stereo3ds.h"\n' + text[i:]
    open(os.path.join(ROOT, U4S), "w", encoding="utf-8").write(text)
    print("  + %-45s include" % U4S)
rw(U4SC,
   "void Screen::screenWait(int numberOfAnimationFrames) {\n",
   "void Screen::update() {\n"
   "\t// ultima-3ds: (re)size the depth plane with the screen, push it with the frame\n"
   "\tif (!_depth.valid() || _depth.width() != w || _depth.height() != h) {\n"
   "\t\t_depth.create(w, h);\n\t\t_depth.setLevel(Stereo3DS::kFront); // UI/text by default\n\t}\n"
   "\t_depth.push();\n"
   "\tGraphics::Screen::update();\n}\n\n"
   "void Screen::screenWait(int numberOfAnimationFrames) {\n",
   "ultima-3ds: (re)size the depth plane with the screen")

rw(IM,
   "void Image::drawOn(Image *d, int x, int y) const {\n"
   "\tGraphics::ManagedSurface *destSurface = getSurface(d);\n",
   "// ultima-3ds: blits that land on the screen stamp the current depth level\n"
   "static inline void u4StampDepth(Graphics::ManagedSurface *dest, int x, int y, int w, int h) {\n"
   "\tif (dest == g_screen && g_screen)\n"
   "\t\tg_screen->_depth.stampRect(x, y, w, h);\n"
   "}\n\n"
   "void Image::drawOn(Image *d, int x, int y) const {\n"
   "\tGraphics::ManagedSurface *destSurface = getSurface(d);\n"
   "\tu4StampDepth(destSurface, x, y, _surface->w, _surface->h);\n",
   "static inline void u4StampDepth(")
rw(IM,
   "\tif (srcRect.isValidRect())\n\t\t// Blit the surface\n\t\tdestSurface->blitFrom(*_surface, srcRect, destPos);\n",
   "\tif (srcRect.isValidRect()) {\n\t\t// Blit the surface\n\t\tdestSurface->blitFrom(*_surface, srcRect, destPos);\n"
   "\t\tu4StampDepth(destSurface, destPos.x, destPos.y, srcRect.width(), srcRect.height());\n\t}\n",
   "u4StampDepth(destSurface, destPos.x, destPos.y, srcRect.width(), srcRect.height());")
rw(IM,
   "\t\tdestSurface->blitFrom(*_surface, src, destPos);\n\t}\n}\n",
   "\t\tdestSurface->blitFrom(*_surface, src, destPos);\n\t\tu4StampDepth(destSurface, destPos.x, destPos.y, rw, 1);\n\t}\n}\n",
   "u4StampDepth(destSurface, destPos.x, destPos.y, rw, 1);")
text = open(os.path.join(ROOT, IM), encoding="utf-8").read()
if '#include "ultima/ultima4/gfx/screen.h"' not in text:
    text = text.replace('#include "ultima/ultima4/gfx/image.h"\n',
                        '#include "ultima/ultima4/gfx/image.h"\n#include "ultima/ultima4/gfx/screen.h"\n', 1)
    open(os.path.join(ROOT, IM), "w", encoding="utf-8").write(text)
    print("  + %-45s include screen.h" % IM)

# TileView: terrain = ground (solid terrain = object), everything stacked on it = actor
rw(TV,
   "void TileView::drawTile(Common::Array<MapTile> &tiles, bool focus, int x, int y) {\n"
   "\tassertMsg(x < _columns, \"x value of %d out of range\", x);\n",
   "void TileView::drawTile(Common::Array<MapTile> &tiles, bool focus, int x, int y) {\n"
   "\tStereo3DS::ScopedLevel _stereo(g_screen->_depth, Stereo3DS::kGround); // ultima-3ds\n"
   "\tassertMsg(x < _columns, \"x value of %d out of range\", x);\n",
   "_stereo(g_screen->_depth, Stereo3DS::kGround); // ultima-3ds\n\tassertMsg(x < _columns")
rw(TV,
   "\t\t// Then draw it to the screen\n"
   "\t\t_animated->drawSubRectOn(_dest, SCALED(x * _tileWidth + _bounds.left),\n"
   "\t\t\tSCALED(y * _tileHeight + _bounds.top), 0, 0,\n"
   "\t\t\t\tSCALED(_tileWidth), SCALED(_tileHeight)\n"
   "\t\t);\n\t}\n",
   "\t\t// Then draw it to the screen\n"
   "\t\t// ultima-3ds: base terrain sits on the ground, solid terrain a step up,\n"
   "\t\t// anything stacked on top (party, creatures, ships) floats in front\n"
   "\t\tg_screen->_depth.setLevel(t == (int)tiles.size() - 1\n"
   "\t\t\t? (frontTileType->isOpaque() ? Stereo3DS::kObject : Stereo3DS::kGround)\n"
   "\t\t\t: Stereo3DS::kActor);\n"
   "\t\t_animated->drawSubRectOn(_dest, SCALED(x * _tileWidth + _bounds.left),\n"
   "\t\t\tSCALED(y * _tileHeight + _bounds.top), 0, 0,\n"
   "\t\t\t\tSCALED(_tileWidth), SCALED(_tileHeight)\n"
   "\t\t);\n\t}\n",
   "ultima-3ds: base terrain sits on the ground")
rw(TV,
   "void TileView::drawTile(MapTile &mapTile, bool focus, int x, int y) {\n"
   "\tTile *tile = _tileSet->get(mapTile._id);\n",
   "void TileView::drawTile(MapTile &mapTile, bool focus, int x, int y) {\n"
   "\tTile *tile = _tileSet->get(mapTile._id);\n"
   "\tStereo3DS::ScopedLevel _stereo(g_screen->_depth, tile->isOpaque() ? Stereo3DS::kObject : Stereo3DS::kGround); // ultima-3ds\n",
   "tile->isOpaque() ? Stereo3DS::kObject : Stereo3DS::kGround); // ultima-3ds")
text = open(os.path.join(ROOT, TV), encoding="utf-8").read()
if '#include "common/stereo3ds.h"' not in text:
    text = text.replace('#include "common/system.h"\n', '#include "common/system.h"\n#include "common/stereo3ds.h"\n', 1)
    open(os.path.join(ROOT, TV), "w", encoding="utf-8").write(text)
    print("  + %-45s include" % TV)

# DungeonView: distance -> depth, nearest walls closest to the player
rw(DV,
   "void DungeonView::drawInDungeon(Tile *tile, int x_offset, int distance, Direction orientation, bool tiledWall) {\n",
   "// ultima-3ds: first-person dungeon depth from draw distance (0 = adjacent)\n"
   "static inline byte u4DungeonLevel(int distance) {\n"
   "\tint lvl = 6 - distance * 2; // 0 -> 6, 1 -> 4, 2 -> 2, 3+ -> ground\n"
   "\tif (lvl < Stereo3DS::kGround) lvl = Stereo3DS::kGround;\n"
   "\treturn (byte)lvl;\n"
   "}\n\n"
   "void DungeonView::drawInDungeon(Tile *tile, int x_offset, int distance, Direction orientation, bool tiledWall) {\n"
   "\tStereo3DS::ScopedLevel _stereo(g_screen->_depth, u4DungeonLevel(distance)); // ultima-3ds\n",
   "static inline byte u4DungeonLevel(int distance)")
rw(DV,
   "void DungeonView::drawWall(int xoffset, int distance, Direction orientation, DungeonGraphicType type) {\n",
   "void DungeonView::drawWall(int xoffset, int distance, Direction orientation, DungeonGraphicType type) {\n"
   "\tStereo3DS::ScopedLevel _stereo(g_screen->_depth, u4DungeonLevel(distance)); // ultima-3ds\n",
   "void DungeonView::drawWall(int xoffset, int distance, Direction orientation, DungeonGraphicType type) {\n\tStereo3DS::ScopedLevel")
text = open(os.path.join(ROOT, DV), encoding="utf-8").read()
if '#include "common/stereo3ds.h"' not in text:
    i = text.find("#include ")
    text = text[:i] + '#include "common/stereo3ds.h"\n' + text[i:]
    open(os.path.join(ROOT, DV), "w", encoding="utf-8").write(text)
    print("  + %-45s include" % DV)
if '#include "ultima/ultima4/gfx/screen.h"' not in text:
    text = open(os.path.join(ROOT, DV), encoding="utf-8").read()
    i = text.find("#include ")
    text = text[:i] + '#include "ultima/ultima4/gfx/screen.h"\n' + text[i:]
    open(os.path.join(ROOT, DV), "w", encoding="utf-8").write(text)

print("done")

# ----------------------------------------------- allocHeapsOverride.cpp
# ScummVM's heap override requests (remaining - linear) bytes as one heap.
# Under Luma's Homebrew Launcher on a New 3DS that exceeds the 96 MB heap
# region (0x08000000-0x0E000000) and svcControlMemory fails -> svcBreak at
# boot. Clamp to the region and back off instead of panicking.
AH = "backends/platform/3ds/allocHeapsOverride.cpp"
rw(AH,
   "\t// Allocate the application heap\n\trc = svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC, static_cast<MemPerm>(static_cast<int>(MEMPERM_READ) | static_cast<int>(MEMPERM_WRITE)));\n\tif (R_FAILED(rc))\n\t\tsvcBreak(USERBREAK_PANIC);\n",
   "\t// Allocate the application heap (ultima-3ds: clamp to the heap region and\n"
   "\t// back off in 4 MB steps rather than panicking)\n"
   "\tconst u32 heapRegion = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;\n"
   "\tif (__ctru_heap_size > heapRegion) __ctru_heap_size = heapRegion;\n"
   "\t__ctru_heap_size &= ~0xFFF;\n"
   "\tfor (;;) {\n"
   "\t\trc = svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC, static_cast<MemPerm>(static_cast<int>(MEMPERM_READ) | static_cast<int>(MEMPERM_WRITE)));\n"
   "\t\tif (R_SUCCEEDED(rc) || __ctru_heap_size <= (16u << 20)) break;\n"
   "\t\t__ctru_heap_size -= (4u << 20);\n"
   "\t}\n"
   "\tif (R_FAILED(rc))\n\t\tsvcBreak(USERBREAK_PANIC);\n",
   "ultima-3ds: clamp to the heap region")
rw(AH,
   "\t// Allocate the linear heap\n\trc = svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, static_cast<MemPerm>(static_cast<int>(MEMPERM_READ) | static_cast<int>(MEMPERM_WRITE)));\n\tif (R_FAILED(rc))\n\t\tsvcBreak(USERBREAK_PANIC);\n",
   "\t// Allocate the linear heap (ultima-3ds: back off to 8 MB before giving up)\n"
   "\tfor (;;) {\n"
   "\t\trc = svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, static_cast<MemPerm>(static_cast<int>(MEMPERM_READ) | static_cast<int>(MEMPERM_WRITE)));\n"
   "\t\tif (R_SUCCEEDED(rc) || __ctru_linear_heap_size <= (8u << 20)) break;\n"
   "\t\t__ctru_linear_heap_size -= (2u << 20);\n"
   "\t}\n"
   "\tif (R_FAILED(rc))\n\t\tsvcBreak(USERBREAK_PANIC);\n",
   "ultima-3ds: back off to 8 MB")

# Default to the game on the bottom (touch) screen only -- no duplicated,
# off-centre magnified copy on the top screen.
rw("backends/platform/3ds/options.cpp",
   'ConfMan.registerDefault("3ds_screen", kScreenBoth);',
   'ConfMan.registerDefault("3ds_screen", kScreenBottom); // 3DS port: game on the touch screen only',
   "game on the touch screen only")

