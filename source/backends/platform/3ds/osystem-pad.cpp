/* The Ultima touch pad on the 3DS (ultima-3ds).
 *
 * While an Ultima game runs, the game is shown on the top screen and the
 * touch screen shows an automap and buttons for whatever the game is waiting
 * for (pad_ui.h; the engines report their state through common/pad3ds.h).
 * A tap sends the same key presses a keyboard would. ZL (or SWAP) puts
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
#include "engines/metaengine.h"

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
static bool s_swapped = false;          // ZL / SWAP: game on the touch screen
static bool s_swapLoaded = false;       // rule 16: the swap is remembered (3ds_swapped in scummvm.ini)
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
	if (!s_swapLoaded) {
		const Common::String &app = Common::ConfigManager::kApplicationDomain;
		s_swapped = ConfMan.hasKey("3ds_swapped", app) && ConfMan.getBool("3ds_swapped", app);
		s_swapLoaded = true;
	}
	return s_swapped ? 2 : 1;
}

void OSystem_3DS::padUpdateLayout() {
	if (!g_engine && Pad3DS::info.game) {    // back in the launcher: forget the last game
		memset(&Pad3DS::info, 0, sizeof(Pad3DS::info));
		s_pad = PadUI::State();
		s_swapped = false;
		s_swapLoaded = false;   // read again for the next game
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
	ConfMan.setBool("3ds_swapped", s_swapped, Common::ConfigManager::kApplicationDomain);
	ConfMan.flushToDisk();
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
	// ZL / START while the game has the keymapper off (Ultima VI intro, title
	// menu, cutscenes, typing): the input thread only sets these flags.
	if (padSwapRequest) {
		padSwapRequest = false;
		padToggleSwap();
	}
	if (padManualRequest) {
		padManualRequest = false;
		updateScreen();
		runPortMenu();
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

// ---- START: the port menu (house style, claude/3ds-look-and-feel.md) ----------
// RESUME, MANUAL, SAVE / LOAD (ScummVM's game menu: save, load, options; in a
// game only) and EXIT GAME (asks first). Runs its own loop on the main thread
// like the manual; the input thread pauses meanwhile (manualOpen).
// Rule 17: can EXIT GAME save to the engine's autosave slot right now? Not when the
// engine can't save at this moment, nor when that slot holds one of the player's own
// saves (the same check as Engine::saveAutosaveIfEnabled).
static bool exitAutosaveOk() {
	if (!g_engine || !g_engine->canSaveAutosaveCurrently())
		return false;
	const int slot = g_engine->getAutosaveSlot();
	if (slot < 0)
		return false;
	if (const MetaEngine *me = g_engine->getMetaEngine()) {
		SaveStateDescriptor desc = me->querySaveMetaInfos(ConfMan.getActiveDomainName().c_str(), slot);
		if (desc.isValid() && !desc.isAutosave())
			return false;
	}
	return true;
}

// SV-02: set when EXIT GAME's save failed; the menu then reopens on "Not saved!".
static bool s_exitSaveFailed = false;

void OSystem_3DS::runPortMenu() {
	static Sprite *top = nullptr, *bot = nullptr;
	static uint8_t topPx[400 * 240 * 3], botPx[320 * 240 * 3];
	if (!top) {
		top = new Sprite;
		bot = new Sprite;
		top->create(400, 240, &kPadMode);
		bot->create(320, 240, &kPadMode);
		top->setPosition(0, 0);
		bot->setPosition(0, 0);
		top->setFilteringMode(false);
		bot->setFilteringMode(false);
	}
	const bool inGame = g_engine != nullptr;
	enum { kResume, kManual, kSave, kExit, kExitNoSave };
	const bool failPage = s_exitSaveFailed;       // SV-02: reopened after a failed exit save
	s_exitSaveFailed = false;
	struct Item { const char *label; int act; };
	std::vector<Item> items;
	items.push_back({ "RESUME", kResume });
	items.push_back({ "MANUAL", kManual });
	items.push_back({ inGame ? "SAVE / LOAD / OPTIONS" : "SAVE / LOAD: START A GAME FIRST", kSave });
	// house rule: a button that can't be used stays, greyed out and inert
	auto usable = [&](int i, bool conf) { return conf || items[i].act != kSave || inGame; };
	items.push_back({ "EXIT GAME", kExit });

	manualOpen = true;
	if (R_SUCCEEDED(gspLcdInit())) {
		GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);
		gspLcdExit();
	}
	delayMillis(30);                        // let the input thread finish its scan

	int focus = failPage ? 1 : 0, pressed = -1, chosen = -1;
	bool confirm = failPage, redraw = true;
	std::vector<PadUI::Button> btns;
	// full-width buttons filling the touch screen below the header (rule 11)
	auto layout = [&]() {
		btns.clear();
		if (!confirm) {
			const int n = (int)items.size(), y0 = 28, gap = 4, h = (240 - 6 - y0 - gap * (n - 1)) / n;
			for (int i = 0; i < n; ++i)
				btns.push_back({ 6, y0 + i * (h + gap), 308, h, items[i].label, PadUI::Action(), false });
		} else {
			btns.push_back({ 6, 120, 150, 112, failPage ? "EXIT ANYWAY" : "YES, EXIT", PadUI::Action(), false });
			btns.push_back({ 164, 120, 150, 112, "NO", PadUI::Action(), false });
		}
	};
	while (aptMainLoop() && !exiting && chosen < 0) {
		if (sleeping) { delayMillis(50); redraw = true; continue; }
		hidScanInput();
		const u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
		layout();
		const int n = (int)btns.size();
		if (focus >= n) focus = n - 1;
		int hitIdx = -1;
		if (rep & (KEY_DUP | KEY_DLEFT)) { do focus = (focus + n - 1) % n; while (!usable(focus, confirm)); redraw = true; }
		if (rep & (KEY_DDOWN | KEY_DRIGHT)) { do focus = (focus + 1) % n; while (!usable(focus, confirm)); redraw = true; }
		if (down & KEY_A) hitIdx = focus;
		if (down & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			for (int i = 0; i < n; ++i)
				if (tp.px >= btns[i].x && tp.px < btns[i].x + btns[i].w && tp.py >= btns[i].y && tp.py < btns[i].y + btns[i].h)
					hitIdx = i;
		}
		if (hitIdx >= 0 && !usable(hitIdx, confirm)) hitIdx = -1;   // greyed: no click, no action
		if (down & (KEY_B | KEY_START)) {
			if (confirm && !failPage) { confirm = false; focus = (int)items.size() - 1; redraw = true; }
			else chosen = kResume;
		}
		if (hitIdx >= 0) {
			click3ds::play();
			pressed = hitIdx;
			focus = hitIdx;
			if (!confirm) {
				if (items[hitIdx].act == kExit) { confirm = true; focus = 1; }
				else chosen = items[hitIdx].act;
			} else if (hitIdx == 0) {
				chosen = failPage ? kExitNoSave : kExit;
			} else if (failPage) {
				chosen = kResume;                 // NO after a failed save: back to the game
			} else {
				confirm = false;
				focus = (int)items.size() - 1;
			}
			redraw = true;
		}
		if (!redraw) { gspWaitForVBlank(); continue; }
		redraw = false;
		{
			const size_t before = btns.size();
			const bool wasConfirm = btns.size() == 2 && (btns[0].label == "YES, EXIT" || btns[0].label == "EXIT ANYWAY");
			layout();                         // the press may have switched pages: draw the new one
			if (wasConfirm != confirm || before != btns.size()) pressed = -1;
		}
		const int nb = (int)btns.size();
		if (focus >= nb) focus = nb - 1;

		PadUI::Canvas tc = { 400, 240, topPx };
		tc.fill(0, 0, 400, 240, PadUI::kBg);
		PadUI::drawHeader(tc, "ULTIMA IV AND VI", "PAUSED");
		PadUI::drawText(tc, 8, 40, failPage ? "Not saved!" : confirm ? "Exit the game?" : "The game is paused.", PadUI::kWhite);
		PadUI::drawText(tc, 8, 62, failPage ? "The save failed (SD card full?). Exit anyway?" : confirm ? (exitAutosaveOk() ? "It is saved to its autosave slot first." : "Anything you have not saved is lost.")
		                                   : "Choose on the touch screen, or with", PadUI::kGrey, false);
		if (!confirm) PadUI::drawText(tc, 8, 80, "the D-pad and A.", PadUI::kGrey, false);
		PadUI::drawHintStrip(tc, confirm ? "A choose    B no" : "A choose    B / START resume");
		PadUI::Canvas bc = { 320, 240, botPx };
		bc.fill(0, 0, 320, 240, PadUI::kBg);
		PadUI::drawHeader(bc, failPage ? "SAVE FAILED" : confirm ? "EXIT GAME" : "MENU");
		if (failPage) {
			PadUI::drawText(bc, 8, 40, "Not saved! Exit anyway?", PadUI::kWhite);
			PadUI::drawText(bc, 8, 62, "Progress since the last save is lost.", PadUI::kGrey, false);
		} else if (confirm) {
			PadUI::drawText(bc, 8, 40, "Exit the game?", PadUI::kWhite);
			PadUI::drawText(bc, 8, 62, exitAutosaveOk()
				? "The game is saved to its autosave first." : "Unsaved progress is lost.", PadUI::kGrey, false);
		}
		for (int i = 0; i < nb; ++i) {
			if (!usable(i, confirm)) {        // greyed out: dim face, grey label
				const PadUI::Button &bt = btns[i];
				bc.fill(bt.x, bt.y, bt.w, bt.h, PadUI::kShade);
				bc.frame(bt.x, bt.y, bt.w, bt.h, PadUI::kFace);
				PadUI::drawText(bc, bt.x + (bt.w - PadUI::textWidth(bt.label)) / 2, bt.y + (bt.h - 13) / 2, bt.label, PadUI::kGrey, false);
				continue;
			}
			PadUI::drawButton(bc, btns[i], i == pressed);
			if (i == focus) {                 // 2 px gold focus ring
				bc.frame(btns[i].x - 1, btns[i].y - 1, btns[i].w + 2, btns[i].h + 2, PadUI::kGold);
				bc.frame(btns[i].x - 2, btns[i].y - 2, btns[i].w + 4, btns[i].h + 4, PadUI::kGold);
			}
		}
		pressed = -1;
		canvasToSprite(tc, top);
		canvasToSprite(bc, bot);
		C3D_FrameBegin(0);
			top->transfer();
			bot->transfer();
		C3D_FrameEnd(0);
		C3D_FrameBegin(0);
			C3D_RenderTargetClear(_renderTargetTop, C3D_CLEAR_ALL, 0x00000000, 0);
			C3D_FrameDrawOn(_renderTargetTop);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionTop);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _modelviewLocation, top->getMatrix());
			top->render();
			C3D_RenderTargetClear(_renderTargetBottom, C3D_CLEAR_ALL, 0x00000000, 0);
			C3D_FrameDrawOn(_renderTargetBottom);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionBottom);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _modelviewLocation, bot->getMatrix());
			bot->render();
		C3D_FrameEnd(0);
	}
	// wait for the button / stylus that closed the menu to be let go
	for (int i = 0; i < 100 && aptMainLoop(); ++i) {
		hidScanInput();
		if (!(hidKeysHeld() & (KEY_TOUCH | KEY_A | KEY_B | KEY_START)))
			break;
		delayMillis(10);
	}
	manualOpen = false;
	updateBacklight();
	updateScreen();

	if (chosen == kManual) {
		runManual();
	} else if (chosen == kSave && g_engine) {
		Common::Event e;
		e.type = Common::EVENT_MAINMENU;          // ScummVM's game menu: save, load, options
		pushPadEvent(e);
	} else if (chosen == kExit) {
		// Rule 17 (never lose progress): a game that can be saved now goes to its autosave
		// slot first (Journey Onward / Load picks it up).
		if (exitAutosaveOk()) {
			const int slot = g_engine->getAutosaveSlot();
			const Common::Error r = g_engine->saveGameState(slot, "Autosave", true);
			if (r.getCode() != Common::kNoError) {
				// SV-02: never quit after a failed save without asking
				s_exitSaveFailed = true;
				runPortMenu();
				return;
			}
			// SV-01: Journey Onward loads the engine's "last save", which autosaves
			// never update; point it at the exit save (Ultima IV: last_save,
			// Ultima VI: latest_save).
			ConfMan.setInt("last_save", slot);
			ConfMan.setInt("latest_save", slot);
			ConfMan.flushToDisk();
		}
		Common::Event e;
		e.type = Common::EVENT_QUIT;
		pushPadEvent(e);
	} else if (chosen == kExitNoSave) {
		Common::Event e;
		e.type = Common::EVENT_QUIT;
		pushPadEvent(e);
	}
}

// SV-06: Close from the HOME menu (or power off). pollEvent calls this once, on the
// main thread, when aptMainLoop() first returns false: the same guarded autosave EXIT
// GAME does (exitAutosaveOk), then the normal quit. Nobody can be asked if it fails,
// so a failed save is only logged; a good one points Journey Onward at it (SV-01).
void n3dsHomeCloseSave() {
	if (!exitAutosaveOk()) {
		printf("[3ds] HOME close: no exit save possible now\n");
		return;
	}
	const int slot = g_engine->getAutosaveSlot();
	const Common::Error r = g_engine->saveGameState(slot, "Autosave", true);
	if (r.getCode() != Common::kNoError) {
		printf("[3ds] HOME close: exit save to slot %d FAILED\n", slot);
		return;
	}
	ConfMan.setInt("last_save", slot);
	ConfMan.setInt("latest_save", slot);
	ConfMan.flushToDisk();
	printf("[3ds] HOME close: exit save to slot %d written\n", slot);
}

// ---- the game picker (house style): big buttons instead of ScummVM's launcher ----
// ScummVM's own launcher only has its smallest built-in font on a 320x240
// overlay (freetype is off), about 6 px tall: too small to read (principle,
// Nov 18 2026: text fonts and sizes have to be readable). base/main.cpp's
// launcherDialog() calls this first. 1: a game was chosen (its domain is
// active), 0: show ScummVM's launcher (adding games, settings), -1: quit.
int OSystem_3DS::runGamePicker() {
	struct Game { Common::String target, label; };
	std::vector<Game> games;
	for (const auto &d : ConfMan.getGameDomains()) {
		Common::String desc = d._value.getValOrDefault("description");
		if (desc.empty()) desc = d._key;
		games.push_back({ d._key, desc });
	}
	if (games.empty())
		return 0;                           // nothing added yet: the launcher's Add Game
	for (size_t i = 0; i < games.size(); ++i)   // by name (the map has no order)
		for (size_t j = i + 1; j < games.size(); ++j)
			if (games[j].label < games[i].label) { Game t = games[i]; games[i] = games[j]; games[j] = t; }
	if (games.size() > 5) games.resize(5);

	static Sprite *top = nullptr, *bot = nullptr;
	static uint8_t topPx[400 * 240 * 3], botPx[320 * 240 * 3];
	if (!top) {
		top = new Sprite;
		bot = new Sprite;
		top->create(400, 240, &kPadMode);
		bot->create(320, 240, &kPadMode);
		top->setPosition(0, 0);
		bot->setPosition(0, 0);
		top->setFilteringMode(false);
		bot->setFilteringMode(false);
	}

	const int nGames = (int)games.size(), n = nGames + 1;   // + SCUMMVM LAUNCHER
	std::vector<PadUI::Button> btns;
	{
		const int y0 = 28, gap = 4, h = (240 - 6 - y0 - gap * (n - 1)) / n;
		for (int i = 0; i < n; ++i) {
			std::string l = i < nGames ? games[i].label.c_str() : "SCUMMVM LAUNCHER";
			if (PadUI::textWidth(l) > 296) l = l.substr(0, 296 / 6 - 3) + "...";
			btns.push_back({ 6, y0 + i * (h + gap), 308, h, l, PadUI::Action(), false });
		}
	}
	int focus = 0;
	const Common::String last = ConfMan.get("lastselectedgame");
	for (int i = 0; i < nGames; ++i)
		if (games[i].target == last) focus = i;

	if (R_SUCCEEDED(gspLcdInit())) {
		GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);
		gspLcdExit();
	}
	int result = 2, pressed = -1;           // 2: still choosing
	bool redraw = true;
	int frames = 0;                         // draw twice: both screen buffers show the picker
	while (result == 2) {
		if (!aptMainLoop() || exiting) { result = -1; break; }
		if (sleeping) { delayMillis(50); redraw = true; continue; }
		manualOpen = true;                  // the input thread leaves the keys to us
		hidScanInput();
		const u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
		int hitIdx = -1;
		if (rep & (KEY_DUP | KEY_DLEFT)) { focus = (focus + n - 1) % n; redraw = true; }
		if (rep & (KEY_DDOWN | KEY_DRIGHT)) { focus = (focus + 1) % n; redraw = true; }
		if (down & KEY_A) hitIdx = focus;
		if (down & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			for (int i = 0; i < n; ++i)
				if (tp.px >= btns[i].x && tp.px < btns[i].x + btns[i].w && tp.py >= btns[i].y && tp.py < btns[i].y + btns[i].h)
					hitIdx = i;
		}
		if (down & KEY_START) {             // the house START menu: manual, exit
			for (int i = 0; i < 100 && aptMainLoop(); ++i) {
				hidScanInput();
				if (!(hidKeysHeld() & KEY_START)) break;
				delayMillis(10);
			}
			manualOpen = false;
			runPortMenu();
			Common::Event ev;
			while (getEventManager()->pollEvent(ev)) {}
			if (getEventManager()->shouldQuit()) { result = -1; break; }
			redraw = true;
			continue;
		}
		if (hitIdx >= 0) {
			click3ds::play();
			pressed = focus = hitIdx;
			redraw = true;
			result = hitIdx < nGames ? 1 : 0;
		}
		if (redraw) { frames = 2; redraw = false; }
		if (frames == 0) { gspWaitForVBlank(); continue; }
		--frames;

		PadUI::Canvas tc = { 400, 240, topPx };
		tc.fill(0, 0, 400, 240, PadUI::kBg);
		PadUI::drawHeader(tc, "ULTIMA IV AND VI");
		PadUI::drawText(tc, 8, 40, "Choose a game.", PadUI::kWhite);
		PadUI::drawText(tc, 8, 62, "Tap it on the touch screen, or use", PadUI::kGrey, false);
		PadUI::drawText(tc, 8, 80, "the D-pad and A.", PadUI::kGrey, false);
		PadUI::drawText(tc, 8, 110, "SCUMMVM LAUNCHER: add games, change", PadUI::kGrey, false);
		PadUI::drawText(tc, 8, 128, "ScummVM's settings.", PadUI::kGrey, false);
		PadUI::drawHintStrip(tc, "A start    START menu    ZL swap in a game");
		PadUI::Canvas bc = { 320, 240, botPx };
		bc.fill(0, 0, 320, 240, PadUI::kBg);
		PadUI::drawHeader(bc, "CHOOSE A GAME");
		for (int i = 0; i < n; ++i) {
			PadUI::drawButton(bc, btns[i], i == pressed);
			if (i == focus) {
				bc.frame(btns[i].x - 1, btns[i].y - 1, btns[i].w + 2, btns[i].h + 2, PadUI::kGold);
				bc.frame(btns[i].x - 2, btns[i].y - 2, btns[i].w + 4, btns[i].h + 4, PadUI::kGold);
			}
		}
		if (frames == 0) pressed = -1;
		canvasToSprite(tc, top);
		canvasToSprite(bc, bot);
		C3D_FrameBegin(0);
			top->transfer();
			bot->transfer();
		C3D_FrameEnd(0);
		C3D_FrameBegin(0);
			C3D_RenderTargetClear(_renderTargetTop, C3D_CLEAR_ALL, 0x00000000, 0);
			C3D_FrameDrawOn(_renderTargetTop);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionTop);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _modelviewLocation, top->getMatrix());
			top->render();
			C3D_RenderTargetClear(_renderTargetBottom, C3D_CLEAR_ALL, 0x00000000, 0);
			C3D_FrameDrawOn(_renderTargetBottom);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _projectionLocation, &_projectionBottom);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, _modelviewLocation, bot->getMatrix());
			bot->render();
		C3D_FrameEnd(0);
	}
	for (int i = 0; i < 100 && aptMainLoop(); ++i) {   // let go of the stylus / A first
		hidScanInput();
		if (!(hidKeysHeld() & (KEY_TOUCH | KEY_A | KEY_START)))
			break;
		delayMillis(10);
	}
	manualOpen = false;
	updateBacklight();
	if (result == 1)
		ConfMan.setActiveDomain(games[focus].target);
	return result;
}

} // namespace N3DS

int n3dsGamePicker() {     // base/main.cpp (ultima-3ds)
	return dynamic_cast<N3DS::OSystem_3DS *>(g_system)->runGamePicker();
}

