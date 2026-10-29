/* ultima-3ds: tell the 3DS touch pad what Ultima VI is waiting for, and draw
 * its automap (see common/pad3ds.h).
 *
 * Called from Screen::update(). Conversation words come from the '@' marks
 * in the NPCs' lines (the words the original highlights), collected in
 * Converse::print().
 *
 * Automap: 72 x 72 tiles around the party, 2 px each, coloured with each
 * tile's average colour. Underground it shows only what the party has been
 * near (saved to u6automap.dat in the save folder).
 */
#include "common/pad3ds.h"
#include "common/savefile.h"
#include "common/system.h"

#include "ultima/nuvie/core/nuvie_defs.h"
#include "ultima/nuvie/core/game.h"
#include "ultima/nuvie/core/converse.h"
#include "ultima/nuvie/core/events.h"
#include "ultima/nuvie/core/magic.h"
#include "ultima/nuvie/core/map.h"
#include "ultima/nuvie/core/player.h"
#include "ultima/nuvie/core/tile_manager.h"
#include "ultima/nuvie/gui/widgets/msg_scroll.h"
#include "ultima/nuvie/screen/game_palette.h"

namespace Ultima {
namespace Nuvie {

// ---- conversation words ----------------------------------------------------
void pad3dsTalkBegin() {
	Pad3DS::clearWords();
}

// Pick out the @words of an NPC line.
void pad3dsConverseText(const char *s) {
	if (!s)
		return;
	for (const char *p = s; *p; ++p) {
		if (*p != '@')
			continue;
		char w[Pad3DS::kWordLen];
		int n = 0;
		for (const char *q = p + 1; *q && Common::isAlpha(*q) && n < Pad3DS::kWordLen - 1; ++q)
			w[n++] = *q;
		w[n] = 0;
		if (n >= 2)
			Pad3DS::addWord(w);
	}
}

// ---- automap ----------------------------------------------------------------
struct RGB { byte r, g, b; };

static RGB s_tileCol[2048];
static bool s_tileColDone[2048];
static uint8 s_pal[256 * 4];
static bool s_palLoaded = false;

static RGB tileColour(Game *game, uint16 n) {
	if (n >= 2048)
		return { 0, 0, 0 };
	if (s_tileColDone[n])
		return s_tileCol[n];
	if (!s_palLoaded) {
		s_palLoaded = game->get_palette() && game->get_palette()->loadPaletteIntoBuffer(s_pal);
		if (!s_palLoaded)
			return { 60, 60, 60 };
	}
	const Tile *t = game->get_tile_manager()->get_original_tile(n);
	uint32 r = 0, g = 0, b = 0, cnt = 0;
	if (t) {
		for (int i = 0; i < 256; ++i) {
			const uint8 c = t->data[i];
			if (c == 0xff)
				continue;   // transparent
			r += s_pal[c * 4]; g += s_pal[c * 4 + 1]; b += s_pal[c * 4 + 2];
			++cnt;
		}
	}
	RGB col = cnt ? RGB{ (byte)(r / cnt), (byte)(g / cnt), (byte)(b / cnt) } : RGB{ 0, 0, 0 };
	s_tileCol[n] = col;
	s_tileColDone[n] = true;
	return col;
}

static void fill(int x, int y, int w, int h, RGB c) {
	using namespace Pad3DS;
	for (int yy = MAX(0, y); yy < MIN((int)kMap, y + h); ++yy)
		for (int xx = MAX(0, x); xx < MIN((int)kMap, x + w); ++xx) {
			byte *p = info.map + (yy * kMap + xx) * 3;
			p[0] = c.r; p[1] = c.g; p[2] = c.b;
		}
}

// Underground: one bit per tile for levels 1..5 (256 x 256 each).
static byte s_seen[5][256][32];
static bool s_seenLoaded = false, s_seenDirty = false;
static uint32 s_lastSave = 0;

static void loadSeen() {
	s_seenLoaded = true;
	memset(s_seen, 0, sizeof(s_seen));
	Common::InSaveFile *f = g_system->getSavefileManager()->openForLoading("u6automap.dat");
	if (f) {
		f->read(s_seen, sizeof(s_seen));
		delete f;
	}
}

static void saveSeen() {
	s_seenDirty = false;
	s_lastSave = g_system->getMillis();
	Common::OutSaveFile *f = g_system->getSavefileManager()->openForSaving("u6automap.dat", false);
	if (f) {
		f->write(s_seen, sizeof(s_seen));
		f->finalize();
		delete f;
	}
}

static void updateMap(Game *game) {
	Player *player = game->get_player();
	Map *map = game->get_game_map();
	if (!player || !map || !game->get_tile_manager())
		return;
	uint16 px, py;
	uint8 pz;
	player->get_location(&px, &py, &pz);

	static int lx = -1, ly = -1, lz = -1;
	if (px == lx && py == ly && pz == lz && Pad3DS::info.mapValid) {
		if (s_seenDirty && g_system->getMillis() - s_lastSave > 5000)
			saveSeen();
		return;
	}
	lx = px; ly = py; lz = pz;

	static const char *const names[] = { "Britannia", "Underworld 1", "Underworld 2", "Underworld 3",
	                                     "Underworld 4", "Gargoyle Lands" };
	Pad3DS::setPlace(pz <= 5 ? names[pz] : "");

	const bool fog = pz >= 1 && pz <= 5;
	if (fog) {
		if (!s_seenLoaded)
			loadSeen();
		for (int dy = -5; dy <= 5; ++dy)
			for (int dx = -5; dx <= 5; ++dx) {
				const int x = (px + dx) & 255, y = (py + dy) & 255;
				byte &b = s_seen[pz - 1][y][x >> 3];
				if (!(b & (1 << (x & 7)))) {
					b |= 1 << (x & 7);
					s_seenDirty = true;
				}
			}
		if (s_seenDirty && g_system->getMillis() - s_lastSave > 5000)
			saveSeen();
	}

	const int n = 72, cell = 2;
	const int w = map->get_width(pz);
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x) {
			int tx = px - n / 2 + x, ty = py - n / 2 + y;
			RGB c = { 0, 0, 0 };
			if (tx >= 0 && ty >= 0 && tx < w && ty < w) {
				if (!fog || (s_seen[pz - 1][ty & 255][(tx & 255) >> 3] >> (tx & 7) & 1)) {
					const Tile *t = map->get_tile(tx, ty, pz, true);
					if (t)
						c = tileColour(game, t->tile_num);
				} else {
					c = { 10, 12, 28 };
				}
			}
			fill(x * cell, y * cell, cell, cell, c);
		}
	// the party
	const int cx = n / 2 * cell;
	fill(cx - 2, cx - 2, 6, 6, { 0, 0, 0 });
	fill(cx - 1, cx - 1, 4, 4, { 255, 255, 255 });
	Pad3DS::mapChanged();
}

// ---- what the game waits for ----------------------------------------------
void pad3dsUpdate() {
	using namespace Pad3DS;
	++info.frame;
	Game *game = Game::get_game();
	if (!game || !game->get_event()) {
		setInput(kUltima6, kOther);
		return;
	}
	if (!game->isLoaded()) {                   // the intro and its menu
		setInput(kUltima6, kIntro);
		return;
	}
	Events *ev = game->get_event();
	Converse *conv = game->get_converse();
	MsgScroll *scroll = game->get_scroll();
	const bool talking = conv && conv->running();

	int input = kCommand;
	Common::String choices;
	if (scroll && scroll->pad3dsPageBreak()) {
		input = kChoice;                       // "more": any key
	} else if (scroll && scroll->pad3dsInputMode()) {
		if (scroll->pad3dsYesNo())
			input = kChoice, choices = "yn";
		else if (scroll->pad3dsNumbers())
			input = kNumber;
		else if (scroll->pad3dsPermit()) {
			choices = scroll->pad3dsPermit();
			choices.toLowercase();
			input = (!choices.empty() && Common::isDigit(choices[0])) ? kNumber : kChoice;
		} else
			input = kString;
	} else if (talking) {
		input = kChoice;                       // between lines
	} else {
		switch (ev->get_mode()) {
		case MOVE_MODE:
		case EQUIP_MODE:
			input = kCommand;
			break;
		default:
			input = kTarget;                   // pick a target / direction, Enter, Esc
			break;
		}
	}
	setInput(kUltima6, input, choices.c_str());
	setTalk(talking, -1);
	updateMap(game);
}

} // namespace Nuvie
} // namespace Ultima
