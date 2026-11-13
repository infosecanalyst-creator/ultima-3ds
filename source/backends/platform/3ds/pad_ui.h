/* pad_ui.h -- the Ultima touch pad: layout and drawing (ultima-3ds).
 *
 * The touch screen while the game is on the top screen:
 *
 *   +----------------+------------------+
 *   |   automap      |  buttons for     |
 *   |   144 x 144    |  what the game   |
 *   |                |  waits for       |
 *   |  place name    |                  |
 *   | [small] [small]|                  |
 *   +----------------+------------------+
 *   | ESC  YES  NO  ENTER  TYPE SWAP BOOK|
 *   +-----------------------------------+
 *
 * Without a map (title screen) the buttons use the whole upper area.
 * Plain C++ with no 3DS calls, so it is tested on a desktop
 * (tools/test_pad.cpp renders every page to an image and checks that every
 * label fits its button).
 */
#ifndef PAD_UI_H
#define PAD_UI_H

#include <algorithm>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "common/pad3ds.h"
#include "font6x13.h"
#include "font6x13b.h"

namespace PadUI {

// ---- what a button does --------------------------------------------------
enum ActKind { kKey, kText, kSpecial };
enum Special { kSwap = 1, kBook, kType, kMore, kBack };
enum KeyFlags { kCtrl = 1 };

// Key codes the backend maps onto Common::KeyCode (same order as there is
// not needed: the backend translates).
enum KeyName {
	kChar = 0,          // `ch` is the ASCII key
	kUp, kDown, kLeft, kRight, kReturn, kEscape, kBackspace, kTab, kSpace
};

struct Action {
	ActKind kind;
	int key;            // KeyName
	char ch;            // for kChar
	int flags;
	std::string text;   // kText: typed, then Return
	int special;        // kSpecial
};

inline Action key(char c, int flags = 0) { Action a = { kKey, kChar, c, flags, "", 0 }; return a; }
inline Action named(int k) { Action a = { kKey, k, 0, 0, "", 0 }; return a; }
inline Action text(const std::string &t) { Action a = { kText, 0, 0, 0, t, 0 }; return a; }
inline Action special(int s) { Action a = { kSpecial, 0, 0, 0, "", s }; return a; }

struct Button {
	int x, y, w, h;
	std::string label;
	Action act;
	bool strip;         // the bottom row
};

// ---- canvas ------------------------------------------------------------------
struct RGB { uint8_t r, g, b; };
static const RGB kBg = { 8, 12, 40 }, kFace = { 40, 56, 120 }, kEdge = { 120, 150, 230 },
	kPress = { 220, 180, 60 }, kGold = { 255, 204, 64 }, kWhite = { 240, 240, 240 },
	kGrey = { 150, 150, 165 }, kShade = { 20, 30, 70 }, kBlack = { 0, 0, 0 };

struct Canvas {
	int w, h;
	uint8_t *px;    // RGB888, row-major
	void fill(int x, int y, int fw, int fh, RGB c) {
		int x1 = x + fw, y1 = y + fh;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		if (x1 > w) x1 = w;
		if (y1 > h) y1 = h;
		for (int yy = y; yy < y1; ++yy) {
			uint8_t *p = px + (yy * w + x) * 3;
			for (int xx = x; xx < x1; ++xx, p += 3) { p[0] = c.r; p[1] = c.g; p[2] = c.b; }
		}
	}
	void frame(int x, int y, int fw, int fh, RGB c) {
		fill(x, y, fw, 1, c); fill(x, y + fh - 1, fw, 1, c);
		fill(x, y, 1, fh, c); fill(x + fw - 1, y, 1, fh, c);
	}
};

inline const uint8_t *glyph(char ch, bool bold) {
	unsigned c = (unsigned char)ch;
	if (c < 32 || c > 126) c = '?';
	return bold ? font6x13b_bits[c - 32] : font6x13_bits[c - 32];
}

inline int textWidth(const std::string &s) { return 6 * (int)s.size(); }

inline void drawText(Canvas &cv, int x, int y, const std::string &s, RGB c, bool bold = true) {
	for (size_t i = 0; i < s.size(); ++i, x += 6) {
		const uint8_t *g = glyph(s[i], bold);
		for (int r = 0; r < 13; ++r)
			for (int b = 0; b < 6; ++b)
				if (g[r] & (32 >> b)) cv.fill(x + b, y + r, 1, 1, c);
	}
}

// Labels that did not fit (the test fails on any).
inline std::vector<std::string> &cutLabels() { static std::vector<std::string> v; return v; }

// ---- pad state ----------------------------------------------------------------
struct State {
	bool more = false;          // the "More" page is open
	int flash = -1;             // index of the button drawn pressed
	uint32_t flashUntil = 0;
	uint32_t lastSerial = ~0u;  // info.serial the buttons were built for
	int focus = -1;             // D-pad focus (index into buttons), -1 = none yet
	std::vector<Button> buttons;
};

// Areas
static const int kMapX = 6, kMapY = 6;                     // map picture (kMap x kMap)
// Rule 11: buttons >= 24 px, >= 4 px apart, >= 6 px from the screen edges.
static const int kColX = 162, kColW = 152, kTopY = 6, kTopH = 190;   // pages: y 6..196
static const int kStripY = 200, kStripH = 34;                         // strip: y 200..234
static const int kFullX = 6, kFullW = 308;

inline std::string cap(const std::string &w) {
	if (w == "heal") return "Health";
	std::string s = w;
	if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = (char)(s[0] - 32);
	return s;
}

// Lay `items` out in a grid inside (x, y, w, h).
inline void grid(std::vector<Button> &out, int x, int y, int w, int h, int cols,
                 const std::vector<std::pair<std::string, Action> > &items, int rows = 0, int gap = 4) {
	if (items.empty()) return;
	if (!rows) rows = ((int)items.size() + cols - 1) / cols;
	const int bw = (w - gap * (cols - 1)) / cols;
	// Buttons grow to fill their area (fill rule: no empty band above the strip).
	const int bh = (h - gap * (rows - 1)) / rows;
	for (size_t i = 0; i < items.size(); ++i) {
		const int c = (int)i % cols, r = (int)i / cols;
		Button b = { x + c * (bw + gap), y + r * (bh + gap), bw, bh, items[i].first, items[i].second, false };
		out.push_back(b);
	}
}

typedef std::vector<std::pair<std::string, Action> > Items;

// ---- the pages -----------------------------------------------------------------
inline void u4Commands(Items &it) {
	it = { { "Talk", key('t') }, { "Attack", key('a') }, { "Enter", key('e') }, { "Klimb", key('k') },
	       { "Descend", key('d') }, { "Get chest", key('g') }, { "Search", key('s') }, { "Open door", key('o') },
	       { "Board", key('b') }, { "X-it", key('x') }, { "Peer gem", key('p') }, { "More...", special(kMore) } };
}
inline void u4More(Items &it) {
	it = { { "Attack", key('a') }, { "Board", key('b') }, { "Cast", key('c') }, { "Descend", key('d') },
	       { "Enter", key('e') }, { "Fire", key('f') }, { "Get chest", key('g') }, { "Hole up", key('h') },
	       { "Ignite", key('i') }, { "Jimmy lock", key('j') }, { "Klimb", key('k') }, { "Locate", key('l') },
	       { "Mix", key('m') }, { "New order", key('n') }, { "Open door", key('o') }, { "Peer gem", key('p') },
	       { "Quit & Save", key('q') }, { "Ready", key('r') }, { "Search", key('s') }, { "Talk", key('t') },
	       { "Use", key('u') }, { "Wear", key('w') }, { "X-it", key('x') }, { "Yell", key('y') },
	       { "Ztats", key('z') }, { "Pass", named(kSpace) }, { "Back", special(kBack) } };
}
inline void u4Combat(Items &it) {
	it = { { "Attack", key('a') }, { "Cast", key('c') }, { "Ready", key('r') }, { "Use", key('u') },
	       { "Get chest", key('g') }, { "Ztats", key('z') }, { "Pass", named(kSpace) }, { "More...", special(kMore) } };
}
inline void u4Spells(Items &it) {
	static const char *const n[26] = { "Awaken", "Blink", "Cure", "Dispel", "Energy", "Fireball", "Gate",
		"Heal", "Iceball", "Jinx", "Kill", "Light", "Missile", "Negate", "Open", "Protect", "Quickness",
		"Resurrect", "Sleep", "Tremor", "Undead", "View", "Winds", "X-it", "Y-up", "Z-down" };
	it.clear();
	for (int i = 0; i < 26; ++i) it.push_back({ n[i], key((char)('a' + i)) });
	it.push_back({ "Cancel", named(kEscape) });
}
inline void u6Commands(Items &it) {
	it = { { "Talk", key('t') }, { "Look", key('l') }, { "Get", key('g') }, { "Use", key('u') },
	       { "Move", key('m') }, { "Drop", key('d') }, { "Attack", key('a') }, { "Cast", key('c') },
	       { "Rest", key('r') }, { "Combat", key('b') }, { "Party view", key('/') }, { "More...", special(kMore) } };
}
inline void u6More(Items &it) {
	it = { { "Save game", key('s') }, { "Load last", key('l', kCtrl) }, { "Party mode", key('0') },
	       { "Solo Avatar", key('1') }, { "Prev member", key('-') }, { "Next member", key('+') },
	       { "Close gumps", key('z') }, { "Multi-use", key(';') }, { "Fight style", key('`') },
	       { "Cursor", named(kTab) }, { "Inventory", key('i') }, { "Back", special(kBack) } };
}
inline void arrows(std::vector<Button> &out, int x, int y, int w, int h, const char *okLabel, const Action &ok) {
	const int bw = (w - 8) / 3, bh = (h - 12) / 4;   // fill the area
	auto add = [&](int c, int r, const char *l, const Action &a) {
		Button b = { x + c * (bw + 4), y + r * (bh + 4), bw, bh, l, a, false };
		out.push_back(b);
	};
	add(1, 0, "North", named(kUp));
	add(0, 1, "West", named(kLeft));
	add(2, 1, "East", named(kRight));
	add(1, 2, "South", named(kDown));
	if (okLabel) add(1, 1, okLabel, ok);
	Button c = { x, y + 3 * (bh + 4), w, bh, "Cancel", named(kEscape), false };
	out.push_back(c);
}
inline void numberPad(Items &it) {
	it = { { "7", key('7') }, { "8", key('8') }, { "9", key('9') },
	       { "4", key('4') }, { "5", key('5') }, { "6", key('6') },
	       { "1", key('1') }, { "2", key('2') }, { "3", key('3') },
	       { "Bksp", named(kBackspace) }, { "0", key('0') }, { "OK", named(kReturn) } };
}

// Ultima IV conversation states (Conversation::State)
enum { kAsk = 2, kAskYesNo = 3, kVendor = 4, kBuyQty = 7, kSellQty = 8, kBuyPrice = 9,
       kConfirm = 10, kContinueQ = 11, kTopic = 12, kGiveBeggar = 16 };

inline bool hasMap() { return Pad3DS::info.mapValid; }

inline void build(State &st) {
	using namespace Pad3DS;
	const Info &in = info;
	std::vector<Button> &b = st.buttons;
	b.clear();
	// strip
	static const char *const sl[7] = { "ESC", "YES", "NO", "ENTER", "TYPE", "SWAP", "BOOK" };
	const Action sa[7] = { named(kEscape), key('y'), key('n'), named(kReturn), special(kType),
	                       special(kSwap), special(kBook) };
	const int sw = (kFullW - 6 * 4) / 7;   // 40
	for (int i = 0; i < 7; ++i) {
		Button s = { kFullX + i * (sw + 4), kStripY, sw, kStripH, sl[i], sa[i], true };
		b.push_back(s);
	}

	const bool map = hasMap();
	const int ax = map ? kColX : kFullX, aw = map ? kColW : kFullW;
	Items it;
	const bool u4 = in.game == kUltima4, u6 = in.game == kUltima6;

	// small buttons under the map
	if (map && !st.more && (in.input == kCommand || in.input == kCombat)) {
		Items sm;
		if (u4) sm = { { "Pass", named(kSpace) }, { "Ztats", key('z') } };
		else sm = { { "Inventory", key('i') }, { "Cancel", named(kSpace) } };
		grid(b, kFullX, 168, 144, 26, 2, sm);   // under the 144-px map (y 6..150) and its place name
	}

	if (st.more && (in.input == kCommand || in.input == kCombat)) {
		if (u4) u4More(it); else u6More(it);
		grid(b, kFullX, kTopY, kFullW, kTopH, 5, it);   // 27 items: 5 x 6 rows of 28 px
		return;
	}
	st.more = false;

	switch (in.input) {
	case kCommand:
		if (u4) u4Commands(it); else u6Commands(it);
		grid(b, ax, kTopY, aw, kTopH, 2, it, 6);
		break;
	case kCombat:
		u4Combat(it);
		grid(b, ax, kTopY, aw, kTopH, 2, it, 6);
		break;
	case kDirection:
		arrows(b, ax, kTopY, aw, kTopH, nullptr, named(kReturn));
		break;
	case kTarget:
		arrows(b, ax, kTopY, aw, kTopH, "Select", named(kReturn));
		break;
	case kSpell:
		u4Spells(it);
		grid(b, kFullX, kTopY, kFullW, kTopH, 5, it);
		break;
	case kIntro:
		if (u6) {
			it = { { "Journey Onward", key('j') }, { "Continue", named(kSpace) },
			       { "Create a Character", key('c') }, { "Skip", named(kEscape) },
			       { "Transfer a Character", key('t') }, { "A", key('a') },
			       { "Introduction", key('i') }, { "B", key('b') } };
			grid(b, kFullX, kTopY, kFullW, kTopH, 2, it);   // fill down to the strip
		} else {
			it = { { "Journey Onward", key('j') }, { "New Game", key('i') }, { "Return to View", key('r') },
			       { "Configure", key('c') }, { "About", key('a') }, { "Continue", named(kSpace) } };
			grid(b, 60, kTopY + 4, 200, 186, 1, it);
		}
		break;
	case kPlayer:
		for (int i = 0; i < in.nparty; ++i)
			{ char l[24]; snprintf(l, sizeof(l), "%d %s", i + 1, in.party[i]); it.push_back({ l, key((char)('1' + i)) }); }
		it.push_back({ "Cancel", named(kEscape) });
		if (it.size() > 6) grid(b, ax, kTopY, aw, kTopH, 2, it);   // a big party: two columns keep rows >= 24 px
		else grid(b, ax, kTopY, aw, kTopH, 1, it, 6);
		break;
	case kNumber:
		numberPad(it);
		grid(b, ax, kTopY, aw, kTopH, 3, it, 4);
		break;
	case kChoice: {
		std::string ch = in.choices;
		const bool vendor = in.talking && u4 && (in.convState == kVendor || in.convState == 5 || in.convState == 6);
		const bool yesNo = in.talking && u4 && (in.convState == kAskYesNo || in.convState == kConfirm || in.convState == kContinueQ);
		if (ch.empty() && vendor) {
			it = { { "Buy", key('b') }, { "Sell", key('s') }, { "Yes", key('y') }, { "No", key('n') } };
			for (char c = 'a'; c <= 'h'; ++c) it.push_back({ std::string(1, (char)(c - 32)), key(c) });
			grid(b, ax, kTopY, aw, kTopH, 2, it, 6);
		} else if (ch.empty() && yesNo) {
			it = { { "Yes", key('y') }, { "No", key('n') } };
			grid(b, ax, kTopY + 30, aw, 90, 1, it);
		} else if (ch.empty()) {
			it = { { "Continue", named(kSpace) } };
			grid(b, ax, kTopY + 50, aw, 60, 1, it);
		} else {
			for (char c : ch) {
				std::string l;
				switch (c) {
				case 'y': l = "Yes"; break;
				case 'n': l = "No"; break;
				case 'm': l = "Male"; break;
				case 'f': l = "Female"; break;
				case ' ': l = "Space"; break;
				case 27:  l = "Cancel"; break;
				default:  l = std::string(1, (c >= 'a' && c <= 'z') ? (char)(c - 32) : c); break;
				}
				it.push_back({ l, c == 27 ? named(kEscape) : c == ' ' ? named(kSpace) : key(c) });
			}
			grid(b, ax, kTopY, aw, it.size() <= 4 ? 120 : kTopH, it.size() <= 4 ? 1 : 2, it);
		}
		break;
	}
	case kString:
		if (in.talking && u4 && in.convState == kAsk) {
			it = { { "Yes", text("yes") }, { "No", text("no") } };
			grid(b, ax, kTopY + 30, aw, 90, 1, it);
		} else if (in.talking && u4 && (in.convState == kBuyQty || in.convState == kSellQty ||
		                                in.convState == kBuyPrice || in.convState == kGiveBeggar)) {
			numberPad(it);
			grid(b, ax, kTopY, aw, kTopH, 3, it, 4);
		} else if (in.talking) {
			if (u6) {   // the words every U6 talker knows, then the highlighted ones
				static const char *const std6[] = { "name", "job", "join", "bye" };
				for (const char *w : std6) it.push_back({ cap(w), text(w) });
			}
			int first = in.nwords > 12 - (int)it.size() ? in.nwords - (12 - (int)it.size()) : 0;
			for (int i = first; i < in.nwords; ++i) {
				bool dup = false;
				for (auto &e : it) dup = dup || e.second.text == in.words[i];
				if (!dup) it.push_back({ cap(in.words[i]), text(in.words[i]) });
			}
			if (it.size() > 12) it.resize(12);
			grid(b, ax, kTopY, aw, kTopH, 2, it, 6);
		} else {
			it = { { "Type...", special(kType) } };
			grid(b, ax, kTopY + 40, aw, 70, 1, it);
		}
		break;
	default:   // kOther
		arrows(b, ax, kTopY, aw, kTopH, "Enter", named(kReturn));
		break;
	}
}

inline void drawButton(Canvas &cv, const Button &bt, bool down) {
	cv.fill(bt.x, bt.y, bt.w, bt.h, down ? kPress : kFace);
	cv.frame(bt.x, bt.y, bt.w, bt.h, kEdge);
	if (down) {   // pushed in: shadow along the top and left
		cv.fill(bt.x + 1, bt.y + 1, bt.w - 2, 2, kShade);
		cv.fill(bt.x + 1, bt.y + 1, 2, bt.h - 2, kShade);
	} else {
		cv.fill(bt.x + 1, bt.y + bt.h - 2, bt.w - 2, 1, kShade);   // raised edge
	}
	const int press = down ? 1 : 0;
	const RGB tc = down ? kBlack : kWhite;
	const int maxChars = (bt.w - 4) / 6;
	std::string l = bt.label;
	if ((int)l.size() > maxChars) {
		size_t cut = l.rfind(' ', maxChars);
		if (cut != std::string::npos && cut > 0 && bt.h >= 28 && (int)(l.size() - cut - 1) <= maxChars) {
			std::string l1 = l.substr(0, cut), l2 = l.substr(cut + 1);
			const int y = bt.y + (bt.h - 26) / 2 + press;
			drawText(cv, bt.x + (bt.w - textWidth(l1)) / 2 + press, y, l1, tc);
			drawText(cv, bt.x + (bt.w - textWidth(l2)) / 2 + press, y + 13, l2, tc);
			return;
		}
		cutLabels().push_back(bt.label);
		l.resize(maxChars > 0 ? maxChars : 0);
	}
	drawText(cv, bt.x + (bt.w - textWidth(l)) / 2 + press, bt.y + (bt.h - 13) / 2 + press, l, tc);
}

// D-pad: move the focus to the nearest button in that direction (the first
// press picks the first button of the page, not the bottom row).
inline void moveFocus(State &st, int dx, int dy) {
	const int n = (int)st.buttons.size();
	if (!n) return;
	if (st.focus < 0 || st.focus >= n) {
		st.focus = 0;
		for (int i = 0; i < n; ++i)
			if (!st.buttons[i].strip) { st.focus = i; break; }
		return;
	}
	const Button &f = st.buttons[st.focus];
	const int fx = f.x + f.w / 2, fy = f.y + f.h / 2;
	int best = -1, bestScore = 1 << 30;
	for (int i = 0; i < n; ++i) {
		if (i == st.focus) continue;
		const Button &b = st.buttons[i];
		const int ex = b.x + b.w / 2 - fx, ey = b.y + b.h / 2 - fy;
		const int along = ex * dx + ey * dy;
		if (along <= 0) continue;                       // must lie that way
		const int across = (ex * dy < 0 ? -ex * dy : ex * dy) + (ey * dx < 0 ? -ey * dx : ey * dx);
		const int score = along + across * 3;
		if (score < bestScore) { bestScore = score; best = i; }
	}
	if (best >= 0) st.focus = best;
}

inline void draw(Canvas &cv, const State &st, uint32_t now) {
	using namespace Pad3DS;
	cv.fill(0, 0, cv.w, cv.h, kBg);
	if (hasMap() && !st.more && info.input != kSpell) {
		cv.fill(kMapX - 2, kMapY - 2, kMap + 4, kMap + 4, kEdge);
		for (int y = 0; y < kMap; ++y)
			memcpy(cv.px + ((kMapY + y) * cv.w + kMapX) * 3, info.map + y * kMap * 3, kMap * 3);
		std::string place = info.place;
		if ((int)place.size() > 25) { cutLabels().push_back(place); place.resize(25); }
		drawText(cv, 4 + (150 - textWidth(place)) / 2, 153, place, kGold);
	}
	for (size_t i = 0; i < st.buttons.size(); ++i)
		drawButton(cv, st.buttons[i], (int)i == st.flash && now < st.flashUntil);
	if (st.focus >= 0 && st.focus < (int)st.buttons.size()) {   // D-pad focus: gold frame
		const Button &b = st.buttons[st.focus];
		cv.frame(b.x - 1, b.y - 1, b.w + 2, b.h + 2, kGold);
		cv.frame(b.x - 2, b.y - 2, b.w + 4, b.h + 4, kGold);
	}
}

// The top screen while the game is on the touch screen: the automap 1.5x,
// the place name and a reminder of ZL.
// House style (claude/3ds-look-and-feel.md): a 22 px header bar with the
// title in gold, and a 16 px hint strip at y 224 saying what the buttons do.
inline void drawHeader(Canvas &cv, const std::string &title, const std::string &status = "") {
	cv.fill(0, 0, cv.w, 22, kFace);
	drawText(cv, 8, 5, title, kGold);
	if (!status.empty()) {
		std::string st = status;
		const int room = (cv.w - 16 - textWidth(title) - 12) / 6;
		if ((int)st.size() > room) { cutLabels().push_back(st); st.resize(room > 0 ? room : 0); }
		drawText(cv, cv.w - 8 - textWidth(st), 5, st, kWhite);
	}
}
inline void drawHintStrip(Canvas &cv, const std::string &hint) {
	cv.fill(0, 224, cv.w, 16, kShade);
	if (textWidth(hint) > cv.w - 8) cutLabels().push_back(hint);
	drawText(cv, (cv.w - textWidth(hint)) / 2, 226, hint, kGold, false);
}

inline void drawDashboard(Canvas &cv) {
	using namespace Pad3DS;
	cv.fill(0, 0, cv.w, cv.h, kBg);
	if (!info.mapValid) {
		// No map yet (intro, title menu, cutscenes): the touch-screen controls,
		// filling the screen like the launcher card (fill rule).
		drawHeader(cv, "GAME ON THE TOUCH SCREEN");
		static const char *const lines[] = {
			"Tap the game to click where you tap.",
			"",
			"*BUTTONS",
			"Circle Pad: move the mouse cursor",
			"A: left click     B: right click",
			"D-pad: walk",
			"R: drag / hover touch mode",
			"ZL: game back on top, buttons below",
		};
		drawHintStrip(cv, "ZL swap screens    START menu");
		for (int i = 0; i < 8; ++i) {
			const char *t = lines[i];
			const bool head = t[0] == '*';
			if (head) ++t;
			if (textWidth(t) > cv.w - 16) cutLabels().push_back(t);
			drawText(cv, 8, 36 + i * 20, t, head ? kGold : kGrey, false);
		}
		return;
	}
	// Map between the header bar and the hint strip (y 28..220).
	const int n = std::min(kMap * 3 / 2, 192), ox = 10, oy = 28;
	drawHeader(cv, "ULTIMA", info.place);
	drawHintStrip(cv, "ZL swap screens    START menu");
	if (info.mapValid) {
		cv.fill(ox - 2, oy - 2, n + 4, n + 4, kEdge);
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x) {
				const uint8_t *p = info.map + ((y * kMap / n) * kMap + x * kMap / n) * 3;
				uint8_t *d = cv.px + ((oy + y) * cv.w + ox + x) * 3;
				d[0] = p[0]; d[1] = p[1]; d[2] = p[2];
			}
	}
	const int rx = ox + n + 12, rw = cv.w - rx - 6, cols = rw / 6;
	std::string place = info.place;
	if ((int)place.size() > cols) { cutLabels().push_back(place); place.resize(cols); }
	(void)place;   // shown in the header bar
	// The controls for the game on the touch screen, down to the hint strip (fill rule).
	static const char *const hint[] = { "ZL: game back on", "top, buttons below", "", "Circle Pad: cursor", "A: left click",
		"B: right click", "D-pad: walk", "R: drag / hover" };
	for (int i = 0; i < 8; ++i) {
		if ((int)strlen(hint[i]) > cols) cutLabels().push_back(hint[i]);
		drawText(cv, rx, oy + 6 + i * 22, hint[i], i < 2 ? kWhite : kGrey, false);
	}
}

// Top screen while ScummVM's own screens (launcher, options, the game menu) are
// on the touch screen: a short card instead of a dark screen (both screens
// always in use).
inline void drawLauncherCard(Canvas &cv) {
	cv.fill(0, 0, cv.w, cv.h, kBg);
	drawHeader(cv, "ULTIMA IV AND VI FOR NEW 3DS");
	drawHintStrip(cv, "ZL swap screens    START menu");
	static const char *const lines[] = {
		"Tap a game on the touch screen, then Start.",
		"New game: Add Game, then pick the ULTIMA4",
		"or ULTIMA6 folder on the SD card.",
		"",
		"*IN A GAME",
		"Circle Pad: walk     D-pad + A: touch buttons",
		"X: talk     Y / B: cancel     Select: attack",
		"ZR: U4 Enter, U6 Use     L: save, load, quit",
		"ZL: swap the screens",
		"",
		"START: menu: manual, save, exit (works here too)",
	};
	// 11 lines, 17 px apart: the card reaches the hint strip (fill rule).
	for (int i = 0; i < 11; ++i) {
		const char *t = lines[i];
		const bool head = t[0] == '*';
		if (head) ++t;
		if (textWidth(t) > cv.w - 16) cutLabels().push_back(t);
		drawText(cv, 8, 31 + i * 17, t, head ? kGold : kGrey, false);
	}
}

inline int hit(const State &st, int x, int y) {
	for (size_t i = 0; i < st.buttons.size(); ++i) {
		const Button &b = st.buttons[i];
		if (x >= b.x - 1 && x < b.x + b.w + 1 && y >= b.y - 1 && y < b.y + b.h + 1) return (int)i;
	}
	return -1;
}

} // namespace PadUI

#endif
