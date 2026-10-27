/* In-game instruction manual for the ultima-3ds port.
 *
 * Start opens it from anywhere (launcher, game, ScummVM menus). Text on the
 * top screen, one button per section on the touch screen. It runs its own
 * loop on the main thread, so the engine waits underneath; the input thread
 * pauses while it is open (both would otherwise call hidScanInput).
 *
 * The text is compiled in (manual_text.h, from docs/manual.txt); a
 * manual.txt in sdmc:/3ds/scummvm/ replaces it.
 */
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <stdio.h>
#include <string.h>
#include <vector>
#include "backends/platform/3ds/manual3ds.h"
#include "backends/platform/3ds/manual_text.h"

#include "backends/platform/3ds/osystem.h"
#include "backends/platform/3ds/stereo3ds.h"

namespace N3DS {

static manual3ds::Manual *g_manual = nullptr;
static Sprite *g_manualTop = nullptr, *g_manualBottom = nullptr;   // kept for the session

static const GfxMode3DS kManualMode = { Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0), GPU_RGBA8,
	(GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)) };

static void canvasToSprite(const manual3ds::Canvas &cv, Sprite *s) {
	for (int y = 0; y < cv.h; ++y) {
		uint32 *row = (uint32 *)s->getBasePtr(0, y);
		const uint8_t *src = cv.px + y * cv.w * 3;
		for (int x = 0; x < cv.w; ++x, src += 3)
			row[x] = ((uint32)src[0] << 24) | ((uint32)src[1] << 16) | ((uint32)src[2] << 8) | 0xFF;
	}
	s->markDirty();
}

void OSystem_3DS::runManual() {
	if (!g_manual) {
		g_manual = new manual3ds::Manual;
		g_manual->load(kManualText, "sdmc:/3ds/scummvm/manual.txt", "Ultima for 3DS: manual");
		g_manualTop = new Sprite;
		g_manualBottom = new Sprite;
		g_manualTop->create(400, 240, &kManualMode);
		g_manualBottom->create(320, 240, &kManualMode);
		g_manualTop->setPosition(0, 0);
		g_manualBottom->setPosition(0, 0);
		g_manualTop->setFilteringMode(false);
		g_manualBottom->setFilteringMode(false);
	}

	manualOpen = true;
	delayMillis(30);                        // let the input thread finish its scan

	static std::vector<uint8_t> topPx(400 * 240 * 3), botPx(320 * 240 * 3);
	manual3ds::Canvas top = { 400, 240, topPx.data() };
	manual3ds::Canvas bot = { 320, 240, botPx.data() };

	g_manual->open();
	hidSetRepeatParameters(20, 6);
	bool redraw = true;
	while (aptMainLoop() && !exiting) {
		if (sleeping) {
			delayMillis(50);
			redraw = true;
			continue;
		}
		hidScanInput();
		const u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
		manual3ds::Input mi;
		static const struct { u32 b; uint32_t k; bool rep; } map[] = {
			{ KEY_DUP, manual3ds::K_UP, true }, { KEY_DDOWN, manual3ds::K_DOWN, true },
			{ KEY_DLEFT, manual3ds::K_LEFT, true }, { KEY_DRIGHT, manual3ds::K_RIGHT, true },
			{ KEY_L, manual3ds::K_L, false }, { KEY_R, manual3ds::K_R, false },
			{ KEY_A, manual3ds::K_A, false }, { KEY_B, manual3ds::K_B, false },
			{ KEY_START, manual3ds::K_START, false },
		};
		for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
			if ((map[i].rep ? rep : down) & map[i].b)
				mi.down |= map[i].k;
		if (down & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			mi.touch_down = true;
			mi.tx = tp.px;
			mi.ty = tp.py;
		}
		circlePosition cp;
		hidCircleRead(&cp);
		mi.cpad_y = cp.dy;

		const bool wasDirty = g_manual->dirty();
		if (!g_manual->update(mi))
			break;
		if (!(redraw || mi.down || mi.touch_down || wasDirty || g_manual->dirty())) {
			gspWaitForVBlank();
			continue;
		}
		redraw = false;
		g_manual->draw_top(top);
		g_manual->draw_bottom(bot);
		canvasToSprite(top, g_manualTop);
		canvasToSprite(bot, g_manualBottom);

		C3D_FrameBegin(0);
			g_manualTop->transfer();
			g_manualBottom->transfer();
		C3D_FrameEnd(0);

		C3D_FrameBegin(0);
			const int eyes = stereo3dsEyes();
			for (int e = 0; e < eyes; ++e) {
				C3D_RenderTarget *t = e == 0 ? _renderTargetTop : stereo3dsTarget();
				C3D_RenderTargetClear(t, C3D_CLEAR_ALL, 0x00000000, 0);
				C3D_FrameDrawOn(t);
				C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionTop);
				C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _modelviewLocation, g_manualTop->getMatrix());
				g_manualTop->render();
			}
			C3D_RenderTargetClear(_renderTargetBottom, C3D_CLEAR_ALL, 0x00000000, 0);
			C3D_FrameDrawOn(_renderTargetBottom);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionBottom);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _modelviewLocation, g_manualBottom->getMatrix());
			g_manualBottom->render();
		C3D_FrameEnd(0);
	}

	// Wait for the stylus / buttons that closed the manual to be let go, so
	// they don't reach the game as a click.
	for (int i = 0; i < 100 && aptMainLoop(); ++i) {
		hidScanInput();
		if (!(hidKeysHeld() & (KEY_TOUCH | KEY_A | KEY_B | KEY_START)))
			break;
		delayMillis(10);
	}
	manualOpen = false;
	updateScreen();                         // the game's frame is still in its textures
}

} // namespace N3DS
