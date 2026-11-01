/* Stereoscopic layer compositing for the ScummVM 3DS backend (ultima-3ds).
 *
 * The engine pushes a per-pixel depth map (see common/stereo3ds.h). Each
 * frame we split the game texture into one RGBA layer per depth level,
 * inpaint the holes an object leaves in the ground layer, and draw the
 * layers twice: left eye and right eye, each layer shifted horizontally by
 * (level - 1) * slider * kShift. Ground sinks slightly into the screen,
 * objects sit on the plane, actors and text float in front.
 *
 * Everything here is self-contained; osystem-graphics.cpp calls:
 *   stereo3dsInit()                        once, after C3D_Init
 *   stereo3dsPrepare(gameTexture)          inside the transfer frame
 *   stereo3dsActive()                      to decide whether to render twice
 *   stereo3dsRenderGame(eye, game, mvLoc)  in place of game.render()
 *   stereo3dsTarget()                      the GFX_RIGHT render target
 */

#define FORBIDDEN_SYMBOL_EXCEPTION_time_h
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_fprintf

#include "backends/platform/3ds/osystem.h"
#include "backends/platform/3ds/sprite.h"
#include "backends/platform/3ds/stereo3ds.h"
#include "backends/platform/3ds/stereo_compose.h"
#include "common/stereo3ds.h"
#include "common/textconsole.h"
#include <3ds.h>
#include <citro3d.h>

namespace N3DS {

#define TEXTURE_TRANSFER_FLAGS_S(in, out) \
	(GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(in) | GX_TRANSFER_OUT_FORMAT(out) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static const GfxMode3DS _stereoModeRGBA8 = { Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0),
	GPU_RGBA8, TEXTURE_TRANSFER_FLAGS_S(GX_TRANSFER_FMT_RGBA8, GX_TRANSFER_FMT_RGBA8) };

// Parallax per depth level, in game pixels, at full slider.
static const float kShift = 2.5f;

struct StereoState {
	C3D_RenderTarget *targetRight = nullptr;
	Sprite layers[Stereo3DS::kLevels];
	byte *depth = nullptr;        // copy of the engine's depth map
	int depthW = 0, depthH = 0;
	int levelsUsed = 0;
	bool haveDepth = false;       // an engine pushed a map this session
	bool fresh = false;           // depth changed since last prepare
	bool enabled = true;          // user toggle (osd / config)
};
static StereoState s;

static void onDepthMap(const byte *depth, int w, int h, int pitch) {
	if (w <= 0 || h <= 0)
		return;
	if (!s.depth || s.depthW != w || s.depthH != h) {
		delete[] s.depth;
		s.depth = new byte[w * h];
		s.depthW = w;
		s.depthH = h;
	}
	int maxLevel = 0;
	for (int y = 0; y < h; ++y) {
		const byte *src = depth + y * pitch;
		byte *dst = s.depth + y * w;
		for (int x = 0; x < w; ++x) {
			byte d = src[x];
			if (d >= Stereo3DS::kLevels)
				d = Stereo3DS::kLevels - 1;
			dst[x] = d;
			if (d > maxLevel)
				maxLevel = d;
		}
	}
	s.levelsUsed = maxLevel + 1;
	s.haveDepth = true;
	s.fresh = true;
}

// The right-eye target and 3D mode are only set up once an engine actually
// pushes a depth map. The Ultima engines never do, so the 3D slider does
// nothing at all for them (it used to crash: render-twice path with no depth).
static void createRightTarget() {
	if (s.targetRight)
		return;
	s.targetRight = C3D_RenderTargetCreate(240, 400, GPU_RB_RGB8, -1);
	if (!s.targetRight)
		return;
	C3D_RenderTargetClear(s.targetRight, C3D_CLEAR_ALL, 0x00000000, 0);
	C3D_RenderTargetSetOutput(s.targetRight, GFX_TOP, GFX_RIGHT,
		GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
		GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
		GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
	gfxSetWide(false);
	gfxSet3D(true);
}

void stereo3dsInit() {
	Stereo3DS::setDepthMap = onDepthMap;
}

void stereo3dsShutdown() {
	Stereo3DS::setDepthMap = nullptr;
	for (int i = 0; i < Stereo3DS::kLevels; ++i)
		s.layers[i].free();
	if (s.targetRight) {
		C3D_RenderTargetDelete(s.targetRight);
		s.targetRight = nullptr;
	}
	delete[] s.depth;
	s.depth = nullptr;
	gfxSet3D(false);   // harmless when it was never turned on
}

void stereo3dsSetEnabled(bool on) { s.enabled = on; }
bool stereo3dsEnabled() { return s.enabled; }

float stereo3dsSlider() {
	return osGet3DSliderState();
}

bool stereo3dsActive() {
	return s.enabled && s.haveDepth && s.targetRight && stereo3dsSlider() > 0.01f;
}

C3D_RenderTarget *stereo3dsTarget() { return s.targetRight; }

// How many eyes to render this frame: 2 whenever the slider is up, so the
// right-eye buffer never shows stale content (identical images == flat 2D).
int stereo3dsEyes() {
	return (s.targetRight && stereo3dsSlider() > 0.01f) ? 2 : 1;
}

// Split the game texture into per-level RGBA layers.
void stereo3dsPrepare(Sprite &game) {
	if (s.haveDepth && s.enabled && !s.targetRight)
		createRightTarget(); // first depth map: now 3D is worth turning on
	if (!stereo3dsActive())
		return;
	const int w = game.actualWidth, h = game.actualHeight;
	if (w != s.depthW || h != s.depthH)
		return; // engine and backend disagree on size; leave stereo off this frame

	for (int l = 0; l < s.levelsUsed; ++l) {
		Sprite &L = s.layers[l];
		if (L.actualWidth != w || L.actualHeight != h || !L.getPixels())
			L.create(w, h, &_stereoModeRGBA8, true);
		L.setPosition(game.getPosX(), game.getPosY());
		L.setScale(game.getScaleX(), game.getScaleY());
		L.setFilteringMode(false);
	}

	// Convert the game frame to RGBA8888 once, then split into layers.
	const Graphics::PixelFormat &fmt = game.format;
	static uint32 *frame = nullptr;
	static int frameW = 0, frameH = 0;
	if (!frame || frameW != w || frameH != h) {
		delete[] frame;
		frame = new uint32[w * h];
		frameW = w; frameH = h;
	}
	for (int y = 0; y < h; ++y) {
		const byte *src = (const byte *)game.getBasePtr(0, y);
		uint32 *dst = frame + y * w;
		for (int x = 0; x < w; ++x) {
			uint32 c = (fmt.bytesPerPixel == 4) ? ((const uint32 *)src)[x] : ((const uint16 *)src)[x];
			byte a, r, g, b;
			fmt.colorToARGB(c, a, r, g, b);
			dst[x] = ((uint32)r << 24) | ((uint32)g << 16) | ((uint32)b << 8) | 0xFF;
		}
	}
	splitLayers(frame, s.depth, w, h, s.levelsUsed,
	            [&](int l, int y) { return (sc_u32 *)s.layers[l].getBasePtr(0, y); });

	for (int l = 0; l < s.levelsUsed; ++l) {
		s.layers[l].markDirty();
		s.layers[l].transfer();
	}
	s.fresh = false;
}

// eye: -1 = left, +1 = right.
void stereo3dsRenderGame(int eye, Sprite &game, int modelviewLocation) {
	if (!stereo3dsActive() || game.actualWidth != s.depthW || game.actualHeight != s.depthH) {
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, modelviewLocation, game.getMatrix());
		game.render();
		return;
	}
	const float slider = stereo3dsSlider();
	const float scale = game.getScaleX();
	for (int l = 0; l < s.levelsUsed; ++l) {
		const int x = game.getPosX() + layerShift(l, eye, slider, kShift, scale);
		s.layers[l].setPosition(x, game.getPosY());
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, modelviewLocation, s.layers[l].getMatrix());
		s.layers[l].render();
	}
}

} // namespace N3DS
