/* Touch-pad side-channel for the 3DS backend (ultima-3ds port).
 *
 * With the game on the top screen, the 3DS touch screen shows an automap and
 * buttons for whatever the game is waiting for (commands, a direction, a
 * yes/no answer, a party member, a number, conversation words...). Engines
 * describe that here every frame; the backend reads it and turns taps into
 * the keys the game expects. Nothing else in the engines changes.
 *
 * Plain data, written and read on the main thread only.
 */
#ifndef COMMON_PAD3DS_H
#define COMMON_PAD3DS_H

#include "common/scummsys.h"

namespace Pad3DS {

enum Game { kNoGame = 0, kUltima4 = 4, kUltima6 = 6 };

// What the game is waiting for.
enum Input {
	kOther = 0,    // a menu or screen the pad does not know: arrows, Space, Enter, Esc
	kCommand,      // walking around: the command keys
	kCombat,       // combat: the combat command keys
	kDirection,    // "Dir?"
	kChoice,       // one key out of `choices` ("" = any key: Continue)
	kPlayer,       // a party member (1-8)
	kNumber,       // digits + Enter
	kString,       // a word + Enter (talk words when `talking`)
	kIntro,        // Ultima IV title menu
	kSpell,        // a spell letter (Ultima IV cast / mix)
	kTarget        // Ultima VI: pick a target with the cursor (arrows + Enter)
};

enum { kMaxWords = 16, kWordLen = 16, kMaxParty = 8, kMap = 144 };

struct Info {
	int game;                       // Game; 0 when no engine has written this frame
	int input;                      // Input
	char choices[24];               // for kChoice, lower case
	bool talking;                   // a conversation is running
	int convState;                  // Ultima IV Conversation::State while talking, else -1
	int nwords;                     // conversation words (Ultima IV: the talker's keywords,
	char words[kMaxWords][kWordLen];//   Ultima VI: the words highlighted with @ so far)
	int nparty;
	char party[kMaxParty][kWordLen];
	char place[40];                 // automap caption
	// Automap: kMap x kMap RGB888, drawn by the engine.
	bool mapValid;
	byte map[kMap * kMap * 3];
	uint32 mapSerial;               // bumped when `map` changed
	uint32 serial;                  // bumped when anything but the map changed
	uint32 frame;                   // bumped by the engine every update (0 = never)
};

extern Info info;

// Engines: compare-and-set helpers that bump `serial` on change.
void setInput(int game, int input, const char *choices = "");
void setTalk(bool talking, int convState);
void clearWords();
void addWord(const char *w);                 // de-duplicated, case-insensitive, newest last
void setParty(int n, const char *const *names);
void setPlace(const char *place);
void mapChanged();                            // after writing info.map

} // namespace Pad3DS

#endif
