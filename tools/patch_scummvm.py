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


# ------------------------------------------------------- in-game manual (Start)
# Start opens a built-in manual (backends/platform/3ds/osystem-manual.cpp):
# text on the top screen, section buttons on the touch screen. ScummVM's
# global menu (save/load/quit) moves from Start to L; Magnify Mode (L) is
# unbound -- Ultima runs at 320x200, too small to magnify anyway.
for f in ("osystem-manual.cpp", "manual3ds.h", "click3ds.h", "manual_text.h", "font6x13.h", "font6x13b.h"):
    copy("backends/platform/3ds/" + f)
rw("backends/platform/3ds/module.mk",
   "\tosystem-events.o\n",
   "\tosystem-events.o \\\n\tosystem-manual.o\n",
   "osystem-manual.o")
OH = "backends/platform/3ds/osystem.h"
rw(OH,
   "\tvolatile bool exiting;\n\tvolatile bool sleeping;\n",
   "\tvolatile bool exiting;\n\tvolatile bool sleeping;\n"
   "\tvolatile bool manualOpen = false; // in-game manual showing: input thread paused\n"
   "\tvoid runManual();                  // osystem-manual.cpp\n",
   "volatile bool manualOpen")
EV = "backends/platform/3ds/osystem-events.cpp"
rw(EV,
   "\t\t} while (osys->sleeping && !osys->exiting);\n",
   "\t\t} while ((osys->sleeping || osys->manualOpen) && !osys->exiting); // manual: it reads input itself\n",
   "osys->manualOpen) && !osys->exiting)")
rw(EV,
   "\tk3DSEventToggleDragMode,\n\tk3DSEventToggleMagnifyMode\n};\n",
   "\tk3DSEventToggleDragMode,\n\tk3DSEventToggleMagnifyMode,\n\tk3DSEventManual\n};\n",
   "\tk3DSEventManual")
rw(EV,
   "\tact = new Action(\"MAGM\", _(\"Toggle Magnify Mode\"));\n"
   "\tact->setCustomBackendActionEvent(k3DSEventToggleMagnifyMode);\n"
   "\tact->addDefaultInputMapping(\"JOY_LEFT_SHOULDER\");\n\tkeymap->addAction(act);\n",
   "\tact = new Action(\"MAGM\", _(\"Toggle Magnify Mode\"));\n"
   "\tact->setCustomBackendActionEvent(k3DSEventToggleMagnifyMode);\n"
   "\tkeymap->addAction(act); // unbound on this port: L opens the ScummVM menu\n\n"
   "\tact = new Action(\"MANUAL\", _(\"Instruction manual\"));\n"
   "\tact->setCustomBackendActionEvent(k3DSEventManual);\n"
   "\tact->addDefaultInputMapping(\"JOY_START\");\n\tkeymap->addAction(act);\n",
   "new Action(\"MANUAL\"")
rw(EV,
   "\tkeymapperDefaultBindings->setDefaultBinding(\"engine-default\", Common::kStandardActionOpenMainMenu, \"\");\n",
   "\tkeymapperDefaultBindings->setDefaultBinding(\"engine-default\", Common::kStandardActionOpenMainMenu, \"\");\n"
   "\t// Start = instruction manual on this port; the ScummVM menu moves to L\n"
   "\tkeymapperDefaultBindings->setDefaultBinding(Common::kGlobalKeymapName, \"MENU\", \"JOY_LEFT_SHOULDER\");\n",
   "\"MENU\", \"JOY_LEFT_SHOULDER\"")
rw(EV,
   "\tif (event.type == Common::EVENT_CUSTOM_BACKEND_ACTION_END) {\n\t\treturn true; // We'll say we have handled the event so it is not propagated\n\t}\n",
   "\tif (event.type == Common::EVENT_CUSTOM_BACKEND_ACTION_END) {\n"
   "\t\tif ((_3DSCustomEvent)event.customType == k3DSEventManual)\n"
   "\t\t\trunManual(); // on release, so the keymapper has seen Start go up\n"
   "\t\treturn true; // We'll say we have handled the event so it is not propagated\n\t}\n",
   "runManual(); // on release")

# ------------------------------------------------ Ultima buttons on the 3DS
# Ultima IV is keyboard driven; give the common commands buttons.
U4 = "engines/ultima/ultima4/metaengine.cpp"
t = open(os.path.join(ROOT, U4), encoding="utf-8").read()
if "JOY_UP" not in t:
    import re
    for act, joy in (("UP", "JOY_UP"), ("DOWN", "JOY_DOWN"), ("LEFT", "JOY_LEFT"), ("RIGHT", "JOY_RIGHT"),
                     ("ESCAPE", "JOY_Y")):
        t = re.sub(r'(\{ KEYBIND_%s, "%s", [^\n]*?), nullptr \}' % (act, act), r'\1, "%s" }' % joy, t)
    t = t.replace('"talk", "t", nullptr }', '"talk", "t", "JOY_X" }', 1)
    t = t.replace('"attack", "a", nullptr }', '"attack", "a", "JOY_LEFT_STICK" }', 1)
    t = t.replace('"enter", "e", nullptr }', '"enter", "e", "JOY_RIGHT_STICK" }', 1)
    # menus: X confirms (Talk is not in the menu keymap)
    t = t.replace('static const KeybindingRecord MENU_KEYS[] = {\n\t{ KEYBIND_INTERACT, "INTERACT", _s("Interact"), "interact", "RETURN", nullptr },',
                  'static const KeybindingRecord MENU_KEYS[] = {\n\t{ KEYBIND_INTERACT, "INTERACT", _s("Interact"), "interact", "RETURN", "JOY_X" },', 1)
    assert t.count('"JOY_UP"') >= 3 and '"JOY_X" }' in t, "Ultima IV keymap anchors moved"
    open(os.path.join(ROOT, U4), "w", encoding="utf-8").write(t)
    print("  + %-45s 3DS buttons" % U4)
else:
    print("  = %-45s already patched" % U4)

U6 = "engines/ultima/nuvie/metaengine.cpp"
rw(U6,
   '\t{ "WALK_WEST", _s("Walk west"), "LEFT", "KP4" },\n\t{ "WALK_EAST", _s("Walk east"), "RIGHT", "KP6" },\n'
   '\t{ "WALK_NORTH", _s("Walk north"), "UP", "KP8" },\n\t{ "WALK_SOUTH", _s("Walk south"), "DOWN", "KP2" },\n',
   '\t{ "WALK_WEST", _s("Walk west"), "LEFT", "JOY_LEFT" }, // 3DS D-pad\n\t{ "WALK_EAST", _s("Walk east"), "RIGHT", "JOY_RIGHT" },\n'
   '\t{ "WALK_NORTH", _s("Walk north"), "UP", "JOY_UP" },\n\t{ "WALK_SOUTH", _s("Walk south"), "DOWN", "JOY_DOWN" },\n',
   '"JOY_LEFT" }, // 3DS D-pad')
rw(U6,
   '\t{ "TALK", _s("Talk"), "t", nullptr },\n',
   '\t{ "TALK", _s("Talk"), "t", "JOY_X" }, // 3DS\n',
   '"JOY_X" }, // 3DS')
rw(U6,
   '\t{ "ATTACK", _s("Attack"), "a", nullptr },\n',
   '\t{ "ATTACK", _s("Attack"), "a", "JOY_LEFT_STICK" }, // 3DS ZL\n',
   '"JOY_LEFT_STICK" }, // 3DS ZL')
rw(U6,
   '\t{ "USE", _s("Use"), "u", nullptr },\n',
   '\t{ "USE", _s("Use"), "u", "JOY_RIGHT_STICK" }, // 3DS ZR\n',
   '"JOY_RIGHT_STICK" }, // 3DS ZR')
rw(U6,
   '\t{ "GAME_MENU_DIALOG", _s("Show game menu; Cancel action if in middle of action"), "ESCAPE", nullptr },\n',
   '\t{ "GAME_MENU_DIALOG", _s("Show game menu; Cancel action if in middle of action"), "ESCAPE", "JOY_Y" }, // 3DS\n',
   '"ESCAPE", "JOY_Y" }, // 3DS')

# ------------------------------------------------ touch pad (game on top)
# The game moves to the top screen and the touch screen shows an automap and
# buttons for what the game is waiting for (common/pad3ds.h). The engines
# only report their state; the backend sends ordinary keys.
copy("common/pad3ds.h")
copy("common/pad3ds.cpp")
rw("common/module.mk",
   "MODULE_OBJS := \\\n",
   "MODULE_OBJS := \\\n\tpad3ds.o \\\n",
   "pad3ds.o")
copy("engines/ultima/ultima4/pad3ds_u4.cpp")
copy("engines/ultima/nuvie/pad3ds_u6.cpp")
rw("engines/ultima/module.mk",
   "\tultima4/metaengine.o \\\n\tultima4/ultima4.o\n",
   "\tultima4/metaengine.o \\\n\tultima4/pad3ds_u4.o \\\n\tultima4/ultima4.o\n",
   "ultima4/pad3ds_u4.o")
rw("engines/ultima/module.mk",
   "\tnuvie/metaengine.o \\\n",
   "\tnuvie/metaengine.o \\\n\tnuvie/pad3ds_u6.o \\\n",
   "nuvie/pad3ds_u6.o")

# Ultima IV: accessors the pad needs, and the conversation start/end
rw("engines/ultima/ultima4/conversation/conversation.h",
   "\tKeyword *operator[](const Common::String &kw);\n",
   "\tKeyword *operator[](const Common::String &kw);\n"
   "\tconst KeywordMap &keywords() const { return _keywords; } // ultima-3ds pad\n",
   "keywords() const { return _keywords; } // ultima-3ds pad")
rw("engines/ultima/ultima4/controllers/read_choice_controller.h",
   "\tstatic char get(const Common::String &choices, EventHandler *eh = nullptr);\n",
   "\tstatic char get(const Common::String &choices, EventHandler *eh = nullptr);\n"
   "\tconst Common::String &choices() const { return _choices; } // ultima-3ds pad\n",
   "choices() const { return _choices; } // ultima-3ds pad")
rw("engines/ultima/ultima4/game/person.h",
   "\tvoid setDialogue(Dialogue *d);\n",
   "\tvoid setDialogue(Dialogue *d);\n"
   "\tDialogue *getDialogue() const { return _dialogue; } // ultima-3ds pad\n",
   "getDialogue() const { return _dialogue; } // ultima-3ds pad")
DA = "engines/ultima/ultima4/core/debugger_actions.cpp"
rw(DA,
   "void DebuggerActions::talkRunConversation(Conversation &conv, Person *talker, bool showPrompt) {\n",
   "void pad3dsTalkBegin(Conversation *conv, Person *talker); // ultima-3ds pad (pad3ds_u4.cpp)\n"
   "void pad3dsTalkEnd();\n\n"
   "void DebuggerActions::talkRunConversation(Conversation &conv, Person *talker, bool showPrompt) {\n"
   "\tpad3dsTalkBegin(&conv, talker);\n",
   "\tpad3dsTalkBegin(&conv, talker);")
rw(DA,
   "\tif (conv._reply.size() > 0)\n\t\tg_screen->screenMessage(\"%s\", conv._reply.front().c_str());\n}\n",
   "\tif (conv._reply.size() > 0)\n\t\tg_screen->screenMessage(\"%s\", conv._reply.front().c_str());\n"
   "\tpad3dsTalkEnd();\n}\n",
   "\tpad3dsTalkEnd();\n}")
rw(U4SC,
   "void Screen::update() {\n",
   "void pad3dsUpdate(); // ultima-3ds pad (pad3ds_u4.cpp)\n\n"
   "void Screen::update() {\n\tpad3dsUpdate();\n",
   "void Screen::update() {\n\tpad3dsUpdate();")

# Ultima VI: scroll state accessors, conversation words, per-frame update
rw("engines/ultima/nuvie/gui/widgets/msg_scroll.h",
   "public:\n\n\tMsgScroll(const Configuration *cfg, Font *f);\n",
   "public:\n"
   "\t// ultima-3ds pad: what the scroll is waiting for\n"
   "\tbool pad3dsInputMode() const { return input_mode; }\n"
   "\tconst char *pad3dsPermit() const { return permit_input; }\n"
   "\tbool pad3dsYesNo() const { return yes_no_only || aye_nay_only; }\n"
   "\tbool pad3dsNumbers() const { return numbers_only; }\n"
   "\tbool pad3dsPageBreak() const { return page_break; }\n\n"
   "\tMsgScroll(const Configuration *cfg, Font *f);\n",
   "bool pad3dsInputMode() const")
CV = "engines/ultima/nuvie/core/converse.cpp"
rw(CV,
   "bool Converse::start(uint8 n) {\n",
   "void pad3dsTalkBegin(); // ultima-3ds pad (pad3ds_u6.cpp)\n"
   "void pad3dsConverseText(const char *s);\n\n"
   "bool Converse::start(uint8 n) {\n\tpad3dsTalkBegin();\n",
   "bool Converse::start(uint8 n) {\n\tpad3dsTalkBegin();")
rw(CV,
   "\tif (s)\n\t\tscroll->display_string(s, MSGSCROLL_NO_MAP_DISPLAY);\n"
   "\telse\n\t\tscroll->display_string(get_output(), MSGSCROLL_NO_MAP_DISPLAY);\n",
   "\tpad3dsConverseText(s ? s : get_output().c_str()); // ultima-3ds pad\n"
   "\tif (s)\n\t\tscroll->display_string(s, MSGSCROLL_NO_MAP_DISPLAY);\n"
   "\telse\n\t\tscroll->display_string(get_output(), MSGSCROLL_NO_MAP_DISPLAY);\n",
   "pad3dsConverseText(s ? s : get_output().c_str());")
rw(SC,
   "void Screen::update() {\n\tdepth.push(); // ultima-3ds\n",
   "void pad3dsUpdate(); // ultima-3ds pad (pad3ds_u6.cpp)\n\n"
   "void Screen::update() {\n\tpad3dsUpdate();\n\tdepth.push(); // ultima-3ds\n",
   "void Screen::update() {\n\tpad3dsUpdate();")
rw(SC,
   "void Screen::performUpdate() {\n\tdepth.push(); // ultima-3ds (partial)\n",
   "void Screen::performUpdate() {\n\tpad3dsUpdate();\n\tdepth.push(); // ultima-3ds (partial)\n",
   "void Screen::performUpdate() {\n\tpad3dsUpdate();")
print("done")

# Backend: the pad itself (osystem-pad.cpp, pad_ui.h)
for f in ("osystem-pad.cpp", "pad_ui.h"):
    copy("backends/platform/3ds/" + f)
rw("backends/platform/3ds/module.mk",
   "\tosystem-manual.o\n",
   "\tosystem-manual.o \\\n\tosystem-pad.o\n",
   "osystem-pad.o")
rw(OH,
   "\tvoid runManual();                  // osystem-manual.cpp\n",
   "\tvoid runManual();                  // osystem-manual.cpp\n"
   "\t// Ultima touch pad (osystem-pad.cpp): game on the top screen, buttons below\n"
   "\tvolatile bool padTouch = false;    // taps go to the pad (read by the input thread)\n"
   "\tint padMode = 0;                   // 0 normal, 1 game on top + pad, 2 game below + map on top\n"
   "\tvoid padUpdateLayout();\n\tvoid padPrepare();\n\tvoid padRenderBottom();\n\tvoid padRenderTop();\n"
   "\tvoid padQueueTap(int x, int y);\n\tvoid padPump();\n\tvoid padToggleSwap();\n"
   "\tvoid padSendKey(const Common::KeyState &ks);\n\tvoid padTypeText(const char *t);\n"
   "\tvoid padOpenKeyboard();\n\tvoid pushPadEvent(Common::Event &e);\n",
   "volatile bool padTouch")
rw(G,
   "\tif (sleeping || exiting) {\n\t\treturn;\n\t}\n\n\tif (!_dirtyRects.empty()) {\n",
   "\tif (sleeping || exiting) {\n\t\treturn;\n\t}\n\n\tpadUpdateLayout(); // ultima-3ds pad\n\n\tif (!_dirtyRects.empty()) {\n",
   "\tpadUpdateLayout(); // ultima-3ds pad")
rw(G,
   "\t\t_osdMessage.transfer();\n\t\t_activityIcon.transfer();\n\tC3D_FrameEnd(0);\n",
   "\t\t_osdMessage.transfer();\n\t\t_activityIcon.transfer();\n\t\tpadPrepare(); // ultima-3ds pad\n\tC3D_FrameEnd(0);\n",
   "\t\tpadPrepare(); // ultima-3ds pad")
rw(G,
   "\t\tC3D_RenderTargetClear(topTarget, C3D_CLEAR_ALL, 0x00000000, 0);\n\t\tC3D_FrameDrawOn(topTarget);\n",
   "\t\tC3D_RenderTargetClear(topTarget, C3D_CLEAR_ALL, 0x00000000, 0);\n\t\tC3D_FrameDrawOn(topTarget);\n"
   "\t\tpadRenderTop(); // ultima-3ds pad: the map while the game is on the touch screen\n",
   "\t\tpadRenderTop(); // ultima-3ds pad")
rw(G,
   "\t\t\t_gameTopTexture.setFilteringMode(_magnifyMode != MODE_MAGON && _filteringEnabled);\n"
   "\t\t\tstereo3dsRenderGame(eye, _gameTopTexture, _modelviewLocation);\n",
   "\t\t\t// ultima-3ds pad: smooth scaling when the game fills the top screen (1.2x)\n"
   "\t\t\t_gameTopTexture.setFilteringMode(padMode == 1 || (_magnifyMode != MODE_MAGON && _filteringEnabled));\n"
   "\t\t\tstereo3dsRenderGame(eye, _gameTopTexture, _modelviewLocation);\n",
   "_gameTopTexture.setFilteringMode(padMode == 1 ||")
rw(G,
   "\t\t\t\t_cursorTexture.render();\n\t\t\t}\n\t\t}\n\tC3D_FrameEnd(0);\n}\n",
   "\t\t\t\t_cursorTexture.render();\n\t\t\t}\n\t\t}\n\t\tpadRenderBottom(); // ultima-3ds pad\n\tC3D_FrameEnd(0);\n}\n",
   "\t\tpadRenderBottom(); // ultima-3ds pad")
rw("backends/platform/3ds/osystem.cpp",
   "void OSystem_3DS::updateBacklight() {\n",
   "void OSystem_3DS::updateBacklight() {\n"
   "\tif (padMode) { // ultima-3ds pad: both screens in use\n"
   "\t\tif (R_SUCCEEDED(gspLcdInit())) {\n\t\t\tGSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);\n\t\t\tgspLcdExit();\n\t\t}\n"
   "\t\treturn;\n\t}\n",
   "ultima-3ds pad: both screens in use")
rw(EV,
   "\t\t// Touch screen events\n\t\tif (held & KEY_TOUCH) {\n",
   "\t\t// ultima-3ds pad: while the game is on the top screen, taps go to the\n"
   "\t\t// pad's buttons (decided when the stylus goes down)\n"
   "\t\tstatic bool padCapture = false;\n"
   "\t\tif (keysPressed & KEY_TOUCH)\n\t\t\tpadCapture = osys->padTouch;\n"
   "\t\tif (padCapture) {\n"
   "\t\t\tif (keysPressed & KEY_TOUCH) {\n\t\t\t\ttouchPosition t;\n\t\t\t\thidTouchRead(&t);\n"
   "\t\t\t\tosys->padQueueTap(t.px, t.py);\n\t\t\t}\n"
   "\t\t\tif (!(held & KEY_TOUCH))\n\t\t\t\tpadCapture = false;\n"
   "\t\t} else\n"
   "\t\t// Touch screen events\n\t\tif (held & KEY_TOUCH) {\n",
   "static bool padCapture = false;")
rw(EV,
   "void OSystem_3DS::initEvents() {\n",
   "void OSystem_3DS::pushPadEvent(Common::Event &e) { // ultima-3ds pad\n"
   "\tpushEventQueue(&_eventQueue, e);\n}\n\n"
   "void OSystem_3DS::initEvents() {\n",
   "void OSystem_3DS::pushPadEvent(")
rw(EV,
   "\tCommon::StackLock lock(*eventMutex);\n\n\tif (_eventQueue.empty()) {\n",
   "\tpadPump(); // ultima-3ds pad: taps -> keys (outside the lock: it may open the keyboard)\n\n"
   "\tCommon::StackLock lock(*eventMutex);\n\n\tif (_eventQueue.empty()) {\n",
   "\tpadPump(); // ultima-3ds pad")
rw(EV,
   "\tk3DSEventToggleMagnifyMode,\n\tk3DSEventManual\n};\n",
   "\tk3DSEventToggleMagnifyMode,\n\tk3DSEventManual,\n\tk3DSEventSwap\n};\n",
   "\tk3DSEventSwap\n")
rw(EV,
   "\tact->addDefaultInputMapping(\"JOY_START\");\n\tkeymap->addAction(act);\n",
   "\tact->addDefaultInputMapping(\"JOY_START\");\n\tkeymap->addAction(act);\n\n"
   "\t// ultima-3ds pad: SELECT moves the game between the two screens\n"
   "\tact = new Action(\"SWAP\", _(\"Game on top / touch screen\"));\n"
   "\tact->setCustomBackendActionEvent(k3DSEventSwap);\n"
   "\tact->addDefaultInputMapping(\"JOY_BACK\");\n\tkeymap->addAction(act);\n",
   "new Action(\"SWAP\"")
rw(EV,
   "\tkeymapperDefaultBindings->setDefaultBinding(Common::kGlobalKeymapName, \"MENU\", \"JOY_LEFT_SHOULDER\");\n",
   "\tkeymapperDefaultBindings->setDefaultBinding(Common::kGlobalKeymapName, \"MENU\", \"JOY_LEFT_SHOULDER\");\n"
   "\t// SELECT swaps screens (ultima-3ds pad); the pad's TYPE button opens the keyboard\n"
   "\tkeymapperDefaultBindings->setDefaultBinding(Common::kGlobalKeymapName, \"VIRT\", \"C+F7\");\n",
   "\"VIRT\", \"C+F7\"")
rw(EV,
   "\tswitch ((_3DSCustomEvent)event.customType) {\n\tcase k3DSEventToggleDragMode:\n",
   "\tswitch ((_3DSCustomEvent)event.customType) {\n"
   "\tcase k3DSEventSwap:\n\t\tpadToggleSwap(); // ultima-3ds pad\n\t\treturn true;\n\n"
   "\tcase k3DSEventToggleDragMode:\n",
   "\tcase k3DSEventSwap:\n")
rw("backends/platform/3ds/main.cpp",
   "\tgfxSetWide(systemModel != SYSTEM_MODEL_2DS);\n",
   "\t// ultima-3ds: stereo 3D needs the normal 400 px top screen; in 800 px mode\n"
   "\t// (which 3D switches off) the top screen showed its left half, stretched.\n"
   "\tgfxSetWide(false);\n\t(void)systemModel;\n",
   "gfxSetWide(false);")
print("done")

# ------------------------------------------ Ultima IV "enhancements" clash
# Ultima IV saves its own "enhancements" option as true/false in the game's
# settings, but ScummVM itself reads "enhancements" as a number when any game
# starts: "ConfigManager::getInt(enhancements): 'true' is not a valid integer".
# Give Ultima IV its own key and drop a true/false value left behind.
rw("engines/ultima/ultima4/core/settings.cpp",
   '\ts.syncAsBool("enhancements", _enhancements, DEFAULT_ENHANCEMENTS);\n',
   '\ts.syncAsBool("u4_enhancements", _enhancements, DEFAULT_ENHANCEMENTS); // ultima-3ds: own key\n',
   '"u4_enhancements"')
rw("engines/engine.cpp",
   '\t_activeEnhancements = (int32)ConfMan.getInt("enhancements");\n',
   '\t// ultima-3ds: older Ultima IV builds saved "enhancements" as true/false\n'
   '\tif (ConfMan.hasKey("enhancements", _targetName)) {\n'
   '\t\tconst Common::String v = ConfMan.get("enhancements", _targetName);\n'
   '\t\tif (v.empty() || !Common::isDigit(v[0]))\n'
   '\t\t\tConfMan.removeKey("enhancements", _targetName);\n'
   '\t}\n'
   '\t_activeEnhancements = (int32)ConfMan.getInt("enhancements");\n',
   "older Ultima IV builds saved")
print("done")

# ------------------------------- pad: D-pad between buttons, stick walks
# While the pad is showing: the D-pad moves a focus between the pad's
# buttons, A presses the focused one, B is Escape, and the Circle Pad walks
# (arrow keys, repeating while held). The mouse cursor is hidden on the top
# screen. The audio thread gets four buffers instead of two (choppy sound).
rw(OH,
   "\tvoid padOpenKeyboard();\n\tvoid pushPadEvent(Common::Event &e);\n",
   "\tvoid padOpenKeyboard();\n\tvoid pushPadEvent(Common::Event &e);\n"
   "\tvoid padQueueNav(int nav);             // input thread: D-pad / A / B on the pad\n"
   "\tenum { kNavUp = 1, kNavDown, kNavLeft, kNavRight, kNavA, kNavB };\n",
   "void padQueueNav(int nav);")
rw(EV,
   "\t\t// C-Pad events\n\t\tcirclePosition circle;\n\t\thidCircleRead(&circle);\n",
   "\t\t// C-Pad events\n\t\tcirclePosition circle;\n\t\thidCircleRead(&circle);\n\n"
   "\t\t// ultima-3ds pad: the Circle Pad walks (arrow keys) while the pad shows\n"
   "\t\tstatic Common::KeyCode padWalkKey = Common::KEYCODE_INVALID;\n"
   "\t\tstatic uint32 padWalkNext = 0;\n"
   "\t\t{\n"
   "\t\t\tCommon::KeyCode dir = Common::KEYCODE_INVALID;\n"
   "\t\t\tif (osys->padTouch && (abs(circle.dx) > 60 || abs(circle.dy) > 60))\n"
   "\t\t\t\tdir = abs(circle.dx) > abs(circle.dy) ? (circle.dx > 0 ? Common::KEYCODE_RIGHT : Common::KEYCODE_LEFT)\n"
   "\t\t\t\t                                     : (circle.dy > 0 ? Common::KEYCODE_UP : Common::KEYCODE_DOWN);\n"
   "\t\t\tconst uint32 nowMs = osys->getMillis();\n"
   "\t\t\tCommon::Event k;\n"
   "\t\t\tif (dir != padWalkKey) {\n"
   "\t\t\t\tif (padWalkKey != Common::KEYCODE_INVALID) {\n"
   "\t\t\t\t\tk.type = Common::EVENT_KEYUP;\n\t\t\t\t\tk.kbd = Common::KeyState(padWalkKey);\n"
   "\t\t\t\t\tpushEventQueue(eventQueue, k);\n\t\t\t\t}\n"
   "\t\t\t\tpadWalkKey = dir;\n"
   "\t\t\t\tif (dir != Common::KEYCODE_INVALID) {\n"
   "\t\t\t\t\tk.type = Common::EVENT_KEYDOWN;\n\t\t\t\t\tk.kbd = Common::KeyState(dir);\n"
   "\t\t\t\t\tpushEventQueue(eventQueue, k);\n\t\t\t\t\tpadWalkNext = nowMs + 300;\n\t\t\t\t}\n"
   "\t\t\t} else if (dir != Common::KEYCODE_INVALID && nowMs >= padWalkNext) {\n"
   "\t\t\t\tk.type = Common::EVENT_KEYDOWN;\n\t\t\t\tk.kbd = Common::KeyState(dir);\n\t\t\t\tk.kbdRepeat = true;\n"
   "\t\t\t\tpushEventQueue(eventQueue, k);\n\t\t\t\tpadWalkNext = nowMs + 170;\n"
   "\t\t\t}\n"
   "\t\t\tif (osys->padTouch)\n\t\t\t\tcircle.dx = circle.dy = 0;   // no mouse movement meanwhile\n"
   "\t\t}\n",
   "the Circle Pad walks (arrow keys) while the pad shows")
rw(EV,
   "\t\t// Button events\n\t\tdoJoyEvent(eventQueue, keysPressed, keysReleased, KEY_L,",
   "\t\t// ultima-3ds pad: D-pad moves between the pad's buttons, A presses, B = Escape\n"
   "\t\tif (osys->padTouch) {\n"
   "\t\t\tconst u32 rep = hidKeysDownRepeat();\n"
   "\t\t\tif (rep & KEY_DUP)    osys->padQueueNav(OSystem_3DS::kNavUp);\n"
   "\t\t\tif (rep & KEY_DDOWN)  osys->padQueueNav(OSystem_3DS::kNavDown);\n"
   "\t\t\tif (rep & KEY_DLEFT)  osys->padQueueNav(OSystem_3DS::kNavLeft);\n"
   "\t\t\tif (rep & KEY_DRIGHT) osys->padQueueNav(OSystem_3DS::kNavRight);\n"
   "\t\t\tif (keysPressed & KEY_A) osys->padQueueNav(OSystem_3DS::kNavA);\n"
   "\t\t\tif (keysPressed & KEY_B) osys->padQueueNav(OSystem_3DS::kNavB);\n"
   "\t\t\tkeysPressed &= ~(KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_A | KEY_B);\n"
   "\t\t}\n\n"
   "\t\t// Button events\n\t\tdoJoyEvent(eventQueue, keysPressed, keysReleased, KEY_L,",
   "D-pad moves between the pad's buttons, A presses, B = Escape")
rw(G,
   "\t\t\tif (_cursorVisible && _showCursor && _screen == kScreenTop) {\n",
   "\t\t\tif (_cursorVisible && _showCursor && _screen == kScreenTop && padMode != 1) { // ultima-3ds pad: no cursor\n",
   "padMode != 1) { // ultima-3ds pad: no cursor")
AU = "backends/platform/3ds/osystem-audio.cpp"
rw(AU,
   "\tconst int bufferCount = 2;\n",
   "\tconst int bufferCount = 4; // ultima-3ds: more headroom (sound was choppy)\n",
   "bufferCount = 4; // ultima-3ds")
rw(AU,
   "\t\tndspWaveBuf *buf = &buffers[bufferIndex];\n"
   "\t\tif (buf->status == NDSP_WBUF_FREE || buf->status == NDSP_WBUF_DONE) {\n",
   "\t\tfor (int fill = 0; fill < bufferCount; ++fill) { // ultima-3ds: refill every free buffer\n"
   "\t\tndspWaveBuf *buf = &buffers[bufferIndex];\n"
   "\t\tif (buf->status != NDSP_WBUF_FREE && buf->status != NDSP_WBUF_DONE)\n\t\t\tbreak;\n"
   "\t\t{\n",
   "refill every free buffer")
rw(AU,
   "\t\t\tbufferIndex++;\n\t\t\tbufferIndex %= bufferCount;\n\t\t}\n\t}\n",
   "\t\t\tbufferIndex++;\n\t\t\tbufferIndex %= bufferCount;\n\t\t}\n\t\t} // ultima-3ds refill loop\n\t}\n",
   "} // ultima-3ds refill loop")
print("done")
