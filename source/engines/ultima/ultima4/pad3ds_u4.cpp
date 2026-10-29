/* ultima-3ds: tell the 3DS touch pad what Ultima IV is waiting for, and draw
 * its automap (see common/pad3ds.h).
 *
 * Called from Screen::update(), so it runs whenever the game shows a frame.
 * It only reads game state; the pad answers with ordinary key presses.
 *
 * Automap: the world around the party (48 x 48 tiles), the whole town or
 * combat map, and dungeon levels that fill in as the party explores (saved
 * to u4automap.dat in the save folder).
 */
#include "common/pad3ds.h"
#include "common/savefile.h"
#include "common/system.h"

#include "ultima/ultima4/ultima4.h"
#include "ultima/ultima4/controllers/alpha_action_controller.h"
#include "ultima/ultima4/controllers/combat_controller.h"
#include "ultima/ultima4/controllers/game_controller.h"
#include "ultima/ultima4/controllers/intro_controller.h"
#include "ultima/ultima4/controllers/read_choice_controller.h"
#include "ultima/ultima4/controllers/read_dir_controller.h"
#include "ultima/ultima4/controllers/read_int_controller.h"
#include "ultima/ultima4/controllers/read_player_controller.h"
#include "ultima/ultima4/controllers/read_string_controller.h"
#include "ultima/ultima4/conversation/conversation.h"
#include "ultima/ultima4/events/event_handler.h"
#include "ultima/ultima4/filesys/savegame.h"
#include "ultima/ultima4/game/context.h"
#include "ultima/ultima4/game/person.h"
#include "ultima/ultima4/map/city.h"
#include "ultima/ultima4/map/dungeon.h"
#include "ultima/ultima4/map/location.h"
#include "ultima/ultima4/map/map.h"
#include "ultima/ultima4/map/tile.h"
#include "ultima/ultima4/map/tileset.h"

namespace Ultima {
namespace Ultima4 {

// ---- conversation (set from DebuggerActions::talkRunConversation) ----------
static Conversation *s_conv = nullptr;
static Person *s_talker = nullptr;

void pad3dsTalkBegin(Conversation *conv, Person *talker) {
	s_conv = conv;
	s_talker = talker;
	Pad3DS::clearWords();
	Dialogue *d = talker ? talker->getDialogue() : nullptr;
	if (!d)
		return;    // vendors run scripts: no keyword list
	// The standard words first, then the talker's own two keywords.
	static const char *const standard[] = { "name", "job", "heal", "look", "join", "give", "bye" };
	Common::Array<Common::String> own;
	for (const auto &kv : d->keywords()) {
		const Common::String &k = kv._key;
		if (k.empty() || k == "ojna")
			continue;
		bool isStd = false;
		for (const char *s : standard)
			isStd = isStd || k == s;
		if (!isStd)
			own.push_back(k);
	}
	for (int i = 0; i < 4; ++i)    // name job heal look
		Pad3DS::addWord(standard[i]);
	for (const auto &k : own)
		Pad3DS::addWord(k.c_str());
	for (int i = 4; i < 7; ++i)    // join give bye
		Pad3DS::addWord(standard[i]);
}

void pad3dsTalkEnd() {
	s_conv = nullptr;
	s_talker = nullptr;
	Pad3DS::clearWords();
	Pad3DS::setTalk(false, -1);
}

// ---- automap ----------------------------------------------------------------
struct RGB { byte r, g, b; };

static RGB colourForName(const Common::String &n) {
	static const struct { const char *name; RGB c; } table[] = {
		{ "sea", { 0, 0, 104 } }, { "water", { 24, 56, 184 } }, { "shallows", { 70, 120, 230 } },
		{ "swamp", { 76, 96, 44 } }, { "grass", { 44, 140, 44 } }, { "brush", { 96, 164, 64 } },
		{ "forest", { 18, 88, 30 } }, { "hills", { 156, 132, 84 } }, { "mountains", { 150, 150, 160 } },
		{ "dungeon", { 230, 40, 40 } }, { "city", { 250, 210, 70 } }, { "castle", { 250, 210, 70 } },
		{ "town", { 250, 210, 70 } }, { "ruins", { 250, 210, 70 } }, { "shrine", { 250, 210, 70 } },
		{ "lcb_west", { 250, 210, 70 } }, { "lcb_entrance", { 250, 210, 70 } }, { "lcb_east", { 250, 210, 70 } },
		{ "dungeon_floor", { 64, 54, 44 } }, { "bridge", { 140, 96, 52 } }, { "bridge_pieces", { 140, 96, 52 } },
		{ "up_ladder", { 230, 210, 120 } }, { "down_ladder", { 230, 210, 120 } },
		{ "brick_floor", { 118, 108, 98 } }, { "wood_floor", { 140, 100, 60 } },
		{ "stone_wall", { 205, 205, 215 } }, { "brick_wall", { 205, 205, 215 } }, { "solid", { 205, 205, 215 } },
		{ "solids", { 205, 205, 215 } }, { "column", { 205, 205, 215 } }, { "secret_door", { 205, 205, 215 } },
		{ "rocks", { 170, 160, 150 } }, { "black", { 0, 0, 0 } },
		{ "door", { 230, 160, 40 } }, { "locked_door", { 230, 160, 40 } },
		{ "chest", { 250, 210, 70 } }, { "lava", { 240, 90, 20 } }, { "campfire", { 255, 120, 0 } },
		{ "poison_field", { 60, 200, 60 } }, { "energy_field", { 90, 90, 255 } },
		{ "fire_field", { 255, 80, 0 } }, { "sleep_field", { 200, 80, 200 } },
		{ "altar", { 220, 220, 220 } }, { "ankh", { 255, 255, 255 } },
		{ "moongate", { 70, 150, 255 } }, { "moongate_opening", { 70, 150, 255 } },
		{ "shipmast", { 140, 96, 52 } }, { "shipwheel", { 140, 96, 52 } }, { "ship", { 140, 96, 52 } },
		{ "horse", { 44, 140, 44 } }, { "balloon", { 44, 140, 44 } },
	};
	for (const auto &t : table)
		if (n == t.name)
			return t.c;
	if (n.size() == 1 || n == "space" || n == "spacers")   // sign letters in towns
		return { 96, 76, 56 };
	return { 96, 96, 100 };
}

static RGB tileColour(Map *map, const Coords &c) {
	static Common::HashMap<uint, RGB> cache;
	MapTile *mt = map->getTileFromData(c);
	if (!mt)
		return { 0, 0, 0 };
	const uint id = mt->getId();
	if (cache.contains(id))
		return cache[id];
	Tile *t = map->_tileSet ? map->_tileSet->get(id) : nullptr;
	RGB col = t ? colourForName(t->getName()) : RGB{ 0, 0, 0 };
	cache[id] = col;
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

// The party: a white square with a black outline (cell >= 3).
static void drawParty(int x, int y, int cell) {
	const int s = MAX(3, cell);
	fill(x - 1, y - 1, s + 2, s + 2, { 0, 0, 0 });
	fill(x, y, s, s, { 255, 255, 255 });
	if (s >= 7)
		fill(x + s / 2 - 1, y + s / 2 - 1, 2, 2, { 230, 40, 40 });
}

// Dungeon cells seen, per map id and level: one bit per x.
static byte s_seen[256][8][8];
static bool s_seenLoaded = false, s_seenDirty = false;

static void loadSeen() {
	s_seenLoaded = true;
	memset(s_seen, 0, sizeof(s_seen));
	Common::InSaveFile *f = g_system->getSavefileManager()->openForLoading("u4automap.dat");
	if (f) {
		f->read(s_seen, sizeof(s_seen));
		delete f;
	}
}

static void saveSeen() {
	s_seenDirty = false;
	Common::OutSaveFile *f = g_system->getSavefileManager()->openForSaving("u4automap.dat", false);
	if (f) {
		f->write(s_seen, sizeof(s_seen));
		f->finalize();
		delete f;
	}
}

static void markSeen(int id, int z, int x, int y) {
	if (id < 0 || id > 255 || z < 0 || z > 7)
		return;
	x &= 7; y &= 7;    // dungeon levels wrap
	if (!(s_seen[id][z][y] & (1 << x))) {
		s_seen[id][z][y] |= (1 << x);
		s_seenDirty = true;
	}
}

static bool isSeen(int id, int z, int x, int y) {
	return (s_seen[id & 255][z & 7][y & 7] >> (x & 7)) & 1;
}

static void drawDungeon(Dungeon *d, const MapCoords &pos) {
	if (!s_seenLoaded)
		loadSeen();
	const int id = d->_id, z = pos.z;
	// What the party can see: its own cell, the eight around it, and up to
	// three cells ahead until a wall.
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
			markSeen(id, z, pos.x + dx, pos.y + dy);
	static const int fx[] = { 0, -1, 0, 1, 0 }, fy[] = { 0, 0, -1, 0, 1 };
	const int o = g_ultima->_saveGame->_orientation;
	if (o >= DIR_WEST && o <= DIR_SOUTH) {
		int x = pos.x, y = pos.y;
		for (int i = 0; i < 3; ++i) {
			x = (x + fx[o] + 8) & 7; y = (y + fy[o] + 8) & 7;
			markSeen(id, z, x, y);
			if (d->tokenAt(MapCoords(x, y, z)) == DUNGEON_WALL)
				break;
			markSeen(id, z, x + fy[o], y + fx[o]);    // the walls beside the corridor
			markSeen(id, z, x - fy[o], y - fx[o]);
		}
	}
	if (s_seenDirty)
		saveSeen();

	const int cell = 18;
	fill(0, 0, Pad3DS::kMap, Pad3DS::kMap, { 10, 12, 28 });
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x) {
			if (!isSeen(id, z, x, y))
				continue;
			const int px = x * cell, py = y * cell;
			const DungeonToken t = d->tokenAt(MapCoords(x, y, z));
			RGB floor = { 46, 50, 74 };
			switch (t) {
			case DUNGEON_WALL:
			case DUNGEON_SECRET_DOOR:
				fill(px, py, cell, cell, { 170, 172, 190 });
				continue;
			case DUNGEON_ROOM:     floor = { 110, 60, 150 }; break;
			case DUNGEON_FOUNTAIN: floor = { 50, 100, 220 }; break;
			case DUNGEON_FIELD:    floor = { 160, 60, 160 }; break;
			case DUNGEON_ALTAR:    floor = { 200, 200, 200 }; break;
			case DUNGEON_MAGIC_ORB: floor = { 60, 200, 220 }; break;
			default: break;
			}
			fill(px, py, cell, cell, floor);
			fill(px, py, cell, 1, { 24, 26, 44 });   // grid lines
			fill(px, py, 1, cell, { 24, 26, 44 });
			const RGB gold = { 250, 210, 70 };
			switch (t) {
			case DUNGEON_LADDER_UP:          // ▲
			case DUNGEON_LADDER_DOWN:        // ▼
			case DUNGEON_LADDER_UPDOWN: {
				const bool up = t != DUNGEON_LADDER_DOWN, down = t != DUNGEON_LADDER_UP;
				for (int i = 0; i < 6; ++i) {
					if (up)   fill(px + 9 - i, py + 2 + i, 1 + 2 * i, 1, gold);
					if (down) fill(px + 9 - i, py + 15 - i, 1 + 2 * i, 1, gold);
				}
				break;
			}
			case DUNGEON_CHEST:  fill(px + 5, py + 6, 8, 6, gold); break;
			case DUNGEON_DOOR:   fill(px + 2, py + 7, cell - 4, 4, { 200, 130, 40 }); break;
			case DUNGEON_CEILING_HOLE:
			case DUNGEON_FLOOR_HOLE: fill(px + 5, py + 5, 8, 8, { 0, 0, 0 }); break;
			default: break;
			}
		}
	drawParty((pos.x & 7) * cell + 5, (pos.y & 7) * cell + 5, 8);
}

static void drawArea(Map *map, const MapCoords &pos, bool world) {
	if (world) {
		// 48 x 48 tiles around the party, 3 px each
		const int n = 48, cell = 3;
		for (int y = 0; y < n; ++y)
			for (int x = 0; x < n; ++x) {
				MapCoords c(pos.x - n / 2 + x, pos.y - n / 2 + y, pos.z);
				c.wrap(map);
				fill(x * cell, y * cell, cell, cell, tileColour(map, c));
			}
		drawParty(n / 2 * cell, n / 2 * cell, cell);
		return;
	}
	// a whole town / combat map, as big as fits
	const int w = map->_width, h = map->_height;
	const int cell = MAX(1, MIN(Pad3DS::kMap / MAX(1, w), Pad3DS::kMap / MAX(1, h)));
	const int ox = (Pad3DS::kMap - w * cell) / 2, oy = (Pad3DS::kMap - h * cell) / 2;
	fill(0, 0, Pad3DS::kMap, Pad3DS::kMap, { 0, 0, 0 });
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			fill(ox + x * cell, oy + y * cell, cell, cell, tileColour(map, MapCoords(x, y, pos.z)));
	if (pos.x >= 0 && pos.y >= 0 && pos.x < w && pos.y < h)
		drawParty(ox + pos.x * cell, oy + pos.y * cell, cell);
}

static void updateMap() {
	if (!g_context || !g_context->_location || !g_context->_location->_map)
		return;
	Location *loc = g_context->_location;
	Map *map = loc->_map;
	const MapCoords &pos = loc->_coords;

	static Map *lastMap = nullptr;
	static MapCoords lastPos;
	static int lastOrient = -1;
	const int orient = g_ultima->_saveGame->_orientation;
	if (map == lastMap && pos == lastPos && orient == lastOrient && Pad3DS::info.mapValid)
		return;
	lastMap = map; lastPos = pos; lastOrient = orient;

	Common::String place;
	switch (map->_type) {
	case Map::WORLD:   place = "Britannia"; break;
	case Map::DUNGEON: place = Common::String::format("%s  level %d", map->getName().c_str(), pos.z + 1); break;
	case Map::COMBAT:  place = "Combat"; break;
	default:           place = map->getName(); break;
	}
	Pad3DS::setPlace(place.c_str());

	if (map->_type == Map::DUNGEON) {
		Dungeon *d = dynamic_cast<Dungeon *>(map);
		if (!d)
			return;
		// the dungeon's combat rooms are COMBAT maps; a DUNGEON is the 8x8 levels
		drawDungeon(d, pos);
	} else {
		drawArea(map, pos, map->_type == Map::WORLD);
	}
	Pad3DS::mapChanged();
}

// ---- what the game waits for ----------------------------------------------
void pad3dsUpdate() {
	using namespace Pad3DS;
	++info.frame;
	Controller *c = eventHandler ? eventHandler->getController() : nullptr;

	int input = kOther;
	Common::String choices;
	if (!c) {
		input = kOther;
	} else if (dynamic_cast<ReadPlayerController *>(c)) {
		input = kPlayer;
	} else if (ReadChoiceController *rc = dynamic_cast<ReadChoiceController *>(c)) {
		input = kChoice;
		choices = rc->choices();
		choices.toLowercase();
	} else if (dynamic_cast<ReadIntController *>(c)) {
		input = kNumber;
	} else if (dynamic_cast<ReadStringController *>(c)) {
		input = kString;
	} else if (dynamic_cast<ReadDirController *>(c)) {
		input = kDirection;
	} else if (dynamic_cast<AlphaActionController *>(c)) {
		input = kSpell;
	} else if (dynamic_cast<CombatController *>(c)) {
		input = kCombat;
	} else if (dynamic_cast<GameController *>(c)) {
		input = kCommand;
	} else if (dynamic_cast<IntroController *>(c)) {
		input = kIntro;
	}
	setInput(kUltima4, input, choices.c_str());
	setTalk(s_conv != nullptr, s_conv ? (int)s_conv->_state : -1);

	if (input == kPlayer && g_ultima && g_ultima->_saveGame) {
		const char *names[8];
		const int n = MIN<int>(g_ultima->_saveGame->_members, 8);
		for (int i = 0; i < n; ++i)
			names[i] = g_ultima->_saveGame->_players[i]._name;
		setParty(n, names);
	}

	if (g_context && g_context->_location)
		updateMap();
	else if (info.mapValid) {
		info.mapValid = false;
		setPlace("");
		++info.serial;
	}
}

} // namespace Ultima4
} // namespace Ultima
