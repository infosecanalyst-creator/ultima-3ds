/* The Ultima touch pad on the 3DS (ultima-3ds).
 *
 * While an Ultima game runs, the game is shown on the top screen and the
 * touch screen shows an automap and buttons for whatever the game is waiting
 * for (pad_ui.h; the engines report their state through common/pad3ds.h).
 * A tap sends the same key presses a keyboard would. SELECT (or SWAP) puts
 * the game back on the touch screen for direct stylus play; the top screen
 * then shows a larger automap. ScummVM's own menus and the launcher keep the
 * normal touch-screen layout.
 *
 * Threads: the input thread only queues taps (padQueueTap); building,
 * drawing and acting on them happen on the main thread (padPump from
 * pollEvent, padUpdateLayout / padPrepare / padRender* from updateScreen).
 */
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <stdio.h>
#include <string.h>
#include <vector>
#include "backends/platform/3ds/osystem.h"
#include "backends/platform/3ds/pad_ui.h"
#include "backends/platform/3ds/click3ds.h"
#include "common/config-manager.h"
#include "common/mutex.h"
#include "engines/engine.h"

namespace N3DS {

// 16-bit (RGB565): half the linear memory of RGBA8, which the 3D layers need.
static const GfxMode3DS kPadMode = { Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0), GPU_RGB565,
	(GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)) };

static PadUI::State s_pad;
static Sprite *s_bottomSprite = nullptr, *s_topSprite = nullptr;
static uint8_t s_bottomPx[320 * 240 * 3], s_topPx[400 * 240 * 3];
static bool s_bottomDirty = true, s_topDirty = true;
static uint32 s_drawnMapSerial = ~0u, s_drawnSerial = ~0u;
static bool s_swapped = false;          // SELECT: game on the touch screen
static uint32 s_lastFrame = 0, s_lastFrameTime = 0;
static uint32 s_autoTypeSerial = ~0u;

struct Tap { int x, y; };   // x < 0: a button press, y = PadNav
static Common::Mutex *s_tapMutex = nullptr;
static Tap s_taps[8];
static int s_ntaps = 0;

static void canvasToSprite(const PadUI::Canvas &cv, Sprite *s) {
	for (int y = 0; y < cv.h; ++y) {
		uint16 *row = (uint16 *)s->getBasePtr(0, y);
		const uint8_t *src = cv.px + y * cv.w * 3;
		for (int x = 0; x < cv.w; ++x, src += 3)
			row[x] = (uint16)(((src[0] & 0xF8) << 8) | ((src[1] & 0xFC) << 3) | (src[2] >> 3));
	}
	s->markDirty();
}

// 0: normal layout, 1: game on top + pad, 2: game on the touch screen + top dashboard
static int wantedMode(OSystem_3DS *osys, bool overlayVisible) {
	if (!g_engine || osys->manualOpen || overlayVisible)
		return 0;
	if (Pad3DS::info.game == Pad3DS::kNoGame)
		return 0;
	return s_swapped ? 2 : 1;
}

void OSystem_3DS::padUpdateLayout() {
	if (!g_engine && Pad3DS::info.game) {    // back in the launcher: forget the last game
		memset(&Pad3DS::info, 0, sizeof(Pad3DS::info));
		s_pad = PadUI::State();
		s_swapped = false;
	}
	const int mode = wantedMode(this, _overlayVisible);
	const Screen want = mode == 1 ? kScreenTop : mode == 2 ? kScreenBottom : (Screen)ConfMan.getInt("3ds_screen");
	const bool modeChanged = mode != padMode;
	padMode = mode;
	padTouch = mode == 1;
	if (_screen != want) {
		_screen = want;
		updateSize();
	}
	if (modeChanged) {
		updateBacklight();
		s_bottomDirty = s_topDirty = true;
	}
}

// Launcher and other ScummVM screens on the touch screen: card on top.
static bool s_cardShown = false;
static bool launcherCard(int padMode, int screen) {
	return padMode == 0 && screen == (int)kScreenBottom;
}

void OSystem_3DS::padPrepare() {
	const bool card = launcherCard(padMode, (int)_screen) && !manualOpen;
	if (!padMode && !card)
		return;
	if (!s_bottomSprite) {
		s_bottomSprite = new Sprite;
		s_topSprite = new Sprite;
		s_bottomSprite->create(320, 240, &kPadMode);
		s_topSprite->create(400, 240, &kPadMode);
		s_bottomSprite->setPosition(0, 0);
		s_topSprite->setPosition(0, 0);
		s_bottomSprite->setFilteringMode(false);
		s_topSprite->setFilteringMode(false);
	}
	const uint32 now = getMillis();
	if (Pad3DS::info.serial != s_pad.lastSerial) {
		s_pad.lastSerial = Pad3DS::info.serial;
		PadUI::build(s_pad);
		if (s_pad.focus >= 0) {          // D-pad in use: start on the new page's first button
			s_pad.focus = -1;
			PadUI::moveFocus(s_pad, 0, 0);
		}
		s_bottomDirty = true;
	}
	if (Pad3DS::info.mapSerial != s_drawnMapSerial) {
		s_drawnMapSerial = Pad3DS::info.mapSerial;
		s_bottomDirty = s_topDirty = true;
	}
	if (s_pad.flash >= 0 && now >= s_pad.flashUntil) {
		s_pad.flash = -1;
		s_bottomDirty = true;
	}
	if (padMode == 1 && s_bottomDirty) {
		s_bottomDirty = false;
		PadUI::Canvas cv = { 320, 240, s_bottomPx };
		PadUI::draw(cv, s_pad, now);
		canvasToSprite(cv, s_bottomSprite);
	}
	if (card && (!s_cardShown || s_topDirty)) {
		s_topDirty = false;
		s_cardShown = true;
		PadUI::Canvas cv = { 400, 240, s_topPx };
		PadUI::drawLauncherCard(cv);
		canvasToSprite(cv, s_topSprite);
	}
	if (!card) s_cardShown = false;
	if (padMode == 2 && s_topDirty) {
		s_topDirty = false;
		PadUI::Canvas cv = { 400, 240, s_topPx };
		PadUI::drawDashboard(cv);
		canvasToSprite(cv, s_topSprite);
	}
	s_bottomSprite->transfer();
	s_topSprite->transfer();
}

void OSystem_3DS::padRenderBottom() {
	if (padMode != 1 || !s_bottomSprite)
		return;
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionBottom);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _modelviewLocation, s_bottomSprite->getMatrix());
	s_bottomSprite->render();
}

void OSystem_3DS::padRenderTop() {
	if (!s_topSprite || !(padMode == 2 || (launcherCard(padMode, (int)_screen) && !manualOpen)))
		return;
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionTop);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _modelviewLocation, s_topSprite->getMatrix());
	s_topSprite->render();
}

// Input thread: remember the tap; the main thread acts on it.
void OSystem_3DS::padQueueTap(int x, int y) {
	if (!s_tapMutex)
		return;
	Common::StackLock lock(*s_tapMutex);
	if (s_ntaps < 8) {
		s_taps[s_ntaps].x = x;
		s_taps[s_ntaps].y = y;
		++s_ntaps;
	}
}

// Input thread: D-pad / A / B while the pad is showing (see PadNav).
void OSystem_3DS::padQueueNav(int nav) {
	padQueueTap(-1, nav);
}

void OSystem_3DS::padToggleSwap() {
	if (!g_engine || Pad3DS::info.game == Pad3DS::kNoGame)
		return;
	s_swapped = !s_swapped;
	click3ds::play();
}

// ---- sending keys --------------------------------------------------------------
static Common::KeyState keyFor(const PadUI::Action &a) {
	using namespace Common;
	switch (a.key) {
	case PadUI::kUp:        return KeyState(KEYCODE_UP);
	case PadUI::kDown:      return KeyState(KEYCODE_DOWN);
	case PadUI::kLeft:      return KeyState(KEYCODE_LEFT);
	case PadUI::kRight:     return KeyState(KEYCODE_RIGHT);
	case PadUI::kReturn:    return KeyState(KEYCODE_RETURN, ASCII_RETURN);
	case PadUI::kEscape:    return KeyState(KEYCODE_ESCAPE, ASCII_ESCAPE);
	case PadUI::kBackspace: return KeyState(KEYCODE_BACKSPACE, ASCII_BACKSPACE);
	case PadUI::kTab:       return KeyState(KEYCODE_TAB, ASCII_TAB);
	case PadUI::kSpace:     return KeyState(KEYCODE_SPACE, ASCII_SPACE);
	default: break;
	}
	const char c = a.ch;
	const byte flags = (a.flags & PadUI::kCtrl) ? KBD_CTRL : 0;
	if (c >= 'a' && c <= 'z')
		return KeyState((KeyCode)(KEYCODE_a + (c - 'a')), c, flags);
	if (c >= 'A' && c <= 'Z')
		return KeyState((KeyCode)(KEYCODE_a + (c - 'A')), c, flags);
	if (c >= '0' && c <= '9')
		return KeyState((KeyCode)(KEYCODE_0 + (c - '0')), c, flags);
	switch (c) {
	case ' ': return KeyState(KEYCODE_SPACE, ' ');
	case '-': return KeyState(KEYCODE_MINUS, '-');
	case '+': return KeyState(KEYCODE_PLUS, '+');
	case ';': return KeyState(KEYCODE_SEMICOLON, ';');
	case '/': return KeyState(KEYCODE_SLASH, '/');
	case '`': return KeyState(KEYCODE_BACKQUOTE, '`');
	case '.': return KeyState(KEYCODE_PERIOD, '.');
	case ',': return KeyState(KEYCODE_COMMA, ',');
	default:  return KeyState(KEYCODE_INVALID, c);
	}
}

void OSystem_3DS::padSendKey(const Common::KeyState &ks) {
	Common::Event e;
	// Arrows go as the D-pad's joystick buttons: the 3DS backend declares no
	// keyboard, so the keymapper drops arrow keys before the games see them.
	int joy = -1;
	switch (ks.keycode) {
	case Common::KEYCODE_UP:    joy = Common::JOYSTICK_BUTTON_DPAD_UP; break;
	case Common::KEYCODE_DOWN:  joy = Common::JOYSTICK_BUTTON_DPAD_DOWN; break;
	case Common::KEYCODE_LEFT:  joy = Common::JOYSTICK_BUTTON_DPAD_LEFT; break;
	case Common::KEYCODE_RIGHT: joy = Common::JOYSTICK_BUTTON_DPAD_RIGHT; break;
	default: break;
	}
	if (joy >= 0) {
		e.type = Common::EVENT_JOYBUTTON_DOWN;
		e.joystick.button = joy;
		pushPadEvent(e);
		e.type = Common::EVENT_JOYBUTTON_UP;
		pushPadEvent(e);
		return;
	}
	e.type = Common::EVENT_KEYDOWN;
	e.kbd = ks;
	pushPadEvent(e);
	e.type = Common::EVENT_KEYUP;
	pushPadEvent(e);
}

void OSystem_3DS::padTypeText(const char *t) {
	for (const char *p = t; *p; ++p) {
		PadUI::Action a = PadUI::key(*p);
		padSendKey(keyFor(a));
	}
	padSendKey(Common::KeyState(Common::KEYCODE_RETURN, Common::ASCII_RETURN));
}

// The 3DS keyboard; what is typed goes to the game followed by Enter.
void OSystem_3DS::padOpenKeyboard() {
	SwkbdState kb;
	char buf[64] = { 0 };
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 32);
	swkbdSetHintText(&kb, Pad3DS::info.talking ? "Say a word" : "Type");
	swkbdSetValidation(&kb, SWKBD_ANYTHING, 0, 0);
	const SwkbdButton b = swkbdInputText(&kb, buf, sizeof(buf));
	if (b != SWKBD_BUTTON_RIGHT)
		return;
	// keep what the games accept: letters, digits, spaces
	char clean[64];
	int n = 0;
	for (const char *p = buf; *p && n < 63; ++p) {
		const char c = *p;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ')
			clean[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
	}
	clean[n] = 0;
	padTypeText(clean);
}

// Main thread, from pollEvent: act on queued taps.
void OSystem_3DS::padPump() {
	if (!s_tapMutex)
		s_tapMutex = new Common::Mutex();
	Tap taps[8];
	int n;
	{
		Common::StackLock lock(*s_tapMutex);
		n = s_ntaps;
		memcpy(taps, s_taps, sizeof(Tap) * n);
		s_ntaps = 0;
	}
	if (padMode != 1)
		return;

	for (int i = 0; i < n; ++i) {
		int idx;
		if (taps[i].x < 0) {               // D-pad / A / B
			const int nav = taps[i].y;
			if (nav == kNavB) {
				padSendKey(Common::KeyState(Common::KEYCODE_ESCAPE, Common::ASCII_ESCAPE));
				continue;
			}
			if (nav != kNavA) {
				PadUI::moveFocus(s_pad, nav == kNavLeft ? -1 : nav == kNavRight ? 1 : 0,
				                 nav == kNavUp ? -1 : nav == kNavDown ? 1 : 0);
				s_bottomDirty = true;
				continue;
			}
			if (s_pad.focus < 0) {         // A before any D-pad: show the focus first
				PadUI::moveFocus(s_pad, 0, 0);
				s_bottomDirty = true;
				continue;
			}
			idx = s_pad.focus;
		} else {
			idx = PadUI::hit(s_pad, taps[i].x, taps[i].y);
		}
		if (idx < 0 || idx >= (int)s_pad.buttons.size())
			continue;
		const PadUI::Button bt = s_pad.buttons[idx];   // copy: actions may rebuild the list
		s_pad.flash = idx;
		s_pad.flashUntil = getMillis() + 150;
		s_bottomDirty = true;
		click3ds::play();
		switch (bt.act.kind) {
		case PadUI::kKey:
			padSendKey(keyFor(bt.act));
			break;
		case PadUI::kText:
			padTypeText(bt.act.text.c_str());
			break;
		case PadUI::kSpecial:
			switch (bt.act.special) {
			case PadUI::kSwap: padToggleSwap(); break;
			case PadUI::kBook: updateScreen(); runManual(); break;
			case PadUI::kType: updateScreen(); padOpenKeyboard(); break;
			case PadUI::kMore: s_pad.more = true; PadUI::build(s_pad); break;
			case PadUI::kBack: s_pad.more = false; PadUI::build(s_pad); break;
			default: break;
			}
			break;
		}
	}

	// The game wants a word that is not conversation (a name, a mantra...):
	// open the keyboard once, by itself.
	if (Pad3DS::info.input == Pad3DS::kString && !Pad3DS::info.talking &&
	        s_autoTypeSerial != Pad3DS::info.serial) {
		s_autoTypeSerial = Pad3DS::info.serial;
		updateScreen();
		padOpenKeyboard();
	}
	(void)s_lastFrame; (void)s_lastFrameTime; (void)s_drawnSerial;
}

} // namespace N3DS
