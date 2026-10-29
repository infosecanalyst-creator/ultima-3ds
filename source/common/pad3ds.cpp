// ultima-3ds: the touch-pad side-channel (see common/pad3ds.h).
//
// Desktop debugging: with PAD3DS_DUMP=<dir> in the environment, every change
// is appended to <dir>/pad.log and each new automap written as a PPM, so the
// engine side can be checked with the real games and no 3DS.
#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/pad3ds.h"

#include <string.h>
#include <ctype.h>
#ifndef __3DS__
#include <stdio.h>
#include <stdlib.h>
#ifdef PAD3DS_DESKTOP_PAGES
#include "backends/platform/3ds/pad_ui.h"   // desktop test build only: render the pad pages
#endif
#endif

namespace Pad3DS {

Info info;

static void changed() {
	++info.serial;
#ifndef __3DS__
	static const char *dir = getenv("PAD3DS_DUMP");
	if (!dir) return;
	char path[512];
	snprintf(path, sizeof(path), "%s/pad.log", dir);
	FILE *f = fopen(path, "a");
	if (!f) return;
	fprintf(f, "serial=%u game=%d input=%d choices='%s' talking=%d conv=%d place='%s' words=",
	        info.serial, info.game, info.input, info.choices, info.talking, info.convState, info.place);
	for (int i = 0; i < info.nwords; ++i) fprintf(f, "%s%s", i ? "," : "", info.words[i]);
	fprintf(f, " party=");
	for (int i = 0; i < info.nparty; ++i) fprintf(f, "%s%s", i ? "," : "", info.party[i]);
	fprintf(f, "\n");
	fclose(f);
#ifdef PAD3DS_DESKTOP_PAGES
	static uint8_t px[320 * 240 * 3];
	PadUI::State st;
	PadUI::build(st);
	PadUI::Canvas cv = { 320, 240, px };
	PadUI::draw(cv, st, 0);
	snprintf(path, sizeof(path), "%s/pad_%05u.ppm", dir, info.serial);
	FILE *g = fopen(path, "wb");
	if (g) {
		fprintf(g, "P6\n320 240\n255\n");
		fwrite(px, 1, sizeof(px), g);
		fclose(g);
	}
#endif
#endif
}

void setInput(int game, int input, const char *choices) {
	if (!choices) choices = "";
	if (info.game == game && info.input == input && !strncmp(info.choices, choices, sizeof(info.choices) - 1))
		return;
	info.game = game;
	info.input = input;
	strncpy(info.choices, choices, sizeof(info.choices) - 1);
	info.choices[sizeof(info.choices) - 1] = 0;
	changed();
}

void setTalk(bool talking, int convState) {
	if (info.talking == talking && info.convState == convState)
		return;
	info.talking = talking;
	info.convState = convState;
	changed();
}

void clearWords() {
	if (!info.nwords) return;
	info.nwords = 0;
	changed();
}

void addWord(const char *w) {
	char lw[kWordLen];
	int n = 0;
	for (; w[n] && n < kWordLen - 1; ++n) lw[n] = (char)tolower((unsigned char)w[n]);
	lw[n] = 0;
	if (n < 2) return;
	for (int i = 0; i < info.nwords; ++i) {
		if (!strcmp(info.words[i], lw)) {
			if (i == info.nwords - 1) return;          // already newest
			memmove(info.words[i], info.words[i + 1], (info.nwords - 1 - i) * kWordLen);
			strcpy(info.words[info.nwords - 1], lw);
			changed();
			return;
		}
	}
	if (info.nwords == kMaxWords) {                    // drop the oldest
		memmove(info.words[0], info.words[1], (kMaxWords - 1) * kWordLen);
		--info.nwords;
	}
	strcpy(info.words[info.nwords++], lw);
	changed();
}

void setParty(int n, const char *const *names) {
	if (n > kMaxParty) n = kMaxParty;
	bool same = n == info.nparty;
	for (int i = 0; same && i < n; ++i) same = !strncmp(info.party[i], names[i], kWordLen - 1);
	if (same) return;
	info.nparty = n;
	for (int i = 0; i < n; ++i) {
		strncpy(info.party[i], names[i], kWordLen - 1);
		info.party[i][kWordLen - 1] = 0;
	}
	changed();
}

void setPlace(const char *place) {
	if (!strncmp(info.place, place, sizeof(info.place) - 1)) return;
	strncpy(info.place, place, sizeof(info.place) - 1);
	info.place[sizeof(info.place) - 1] = 0;
	changed();
}

void mapChanged() {
	info.mapValid = true;
	++info.mapSerial;
#ifndef __3DS__
	static const char *dir = getenv("PAD3DS_DUMP");
	static uint32 last = 0;
	if (!dir || info.mapSerial - last < 1) return;
	last = info.mapSerial;
	char path[512];
	snprintf(path, sizeof(path), "%s/map_%05u.ppm", dir, info.mapSerial);
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", kMap, kMap);
	fwrite(info.map, 1, sizeof(info.map), f);
	fclose(f);
#endif
}

} // namespace Pad3DS
