// Desktop test for the Ultima touch pad (backends/platform/3ds/pad_ui.h):
// renders every page to <out>/pad_*.ppm and fails if any label is cut off.
//   g++ -std=c++11 -Itools/padstub -Isource -Isource/backends/platform/3ds 
//       tools/test_pad.cpp source/common/pad3ds.cpp -o /tmp/test_pad && /tmp/test_pad /tmp/padout
#include <stdio.h>
#include <string.h>
#include <vector>
#include "backends/platform/3ds/pad_ui.h"

using namespace Pad3DS;
static int g_bad = 0;
static uint8_t px[320 * 240 * 3], tpx[400 * 240 * 3];

static void save(const char *dir, const char *name, PadUI::Canvas &cv) {
	char p[512];
	snprintf(p, sizeof(p), "%s/pad_%s.ppm", dir, name);
	FILE *f = fopen(p, "wb");
	fprintf(f, "P6\n%d %d\n255\n", cv.w, cv.h);
	fwrite(cv.px, 1, cv.w * cv.h * 3, f);
	fclose(f);
}

static void page(const char *dir, const char *name, bool more = false, int flash = -1) {
	PadUI::State st;
	st.more = more;
	PadUI::build(st);
	st.flash = flash; st.flashUntil = flash >= 0 ? 100 : 0;
	PadUI::Canvas cv = { 320, 240, px };
	PadUI::draw(cv, st, 0);
	save(dir, name, cv);
	for (auto &b : st.buttons)
		if (b.x < 0 || b.y < 0 || b.x + b.w > 320 || b.y + b.h > 240) printf("OFFSCREEN %s: %s\n", name, b.label.c_str());
	// D-pad: every button must be reachable from the first focus
	const int n = (int)st.buttons.size();
	std::vector<bool> seen(n, false);
	std::vector<int> todo;
	PadUI::moveFocus(st, 0, 1);
	if (st.focus >= 0) { seen[st.focus] = true; todo.push_back(st.focus); }
	while (!todo.empty()) {
		const int at = todo.back(); todo.pop_back();
		const int d[4][2] = { {0,-1}, {0,1}, {-1,0}, {1,0} };
		for (auto &v : d) {
			st.focus = at; PadUI::moveFocus(st, v[0], v[1]);
			if (!seen[st.focus]) { seen[st.focus] = true; todo.push_back(st.focus); }
		}
	}
	for (int i = 0; i < n; ++i)
		if (!seen[i]) { printf("UNREACHABLE %s: %s\n", name, st.buttons[i].label.c_str()); ++g_bad; }
	st.focus = n > 1 ? 1 : 0;
	PadUI::draw(cv, st, 0);
	char fn[64]; snprintf(fn, sizeof(fn), "%s_focus", name);
	save(dir, fn, cv);
}

int main(int argc, char **argv) {
	const char *dir = argc > 1 ? argv[1] : ".";
	// a fake automap
	for (int y = 0; y < kMap; ++y)
		for (int x = 0; x < kMap; ++x) {
			byte *p = info.map + (y * kMap + x) * 3;
			const bool land = (x - 72) * (x - 72) + (y - 60) * (y - 60) < 2500;
			p[0] = land ? 44 : 24; p[1] = land ? 140 : 56; p[2] = land ? 44 : 184;
		}
	info.mapValid = true;
	setPlace("Britannia");

	setInput(kUltima4, kCommand); page(dir, "u4_command", false, 0);
	page(dir, "u4_more", true);
	setInput(kUltima4, kCombat); page(dir, "u4_combat");
	setInput(kUltima4, kDirection); page(dir, "u4_direction");
	setInput(kUltima4, kSpell); page(dir, "u4_spell");
	setInput(kUltima4, kChoice, "mf"); page(dir, "u4_choice_mf");
	setInput(kUltima4, kChoice, ""); page(dir, "u4_continue");
	const char *party[] = { "Ryan", "Iolo", "Geoffrey", "Jaana", "Julia", "Dupre", "Shamino", "Katrina" };
	setParty(8, party); setInput(kUltima4, kPlayer); page(dir, "u4_player");
	setInput(kUltima4, kNumber); page(dir, "u4_number");
	setTalk(true, 1); clearWords();
	const char *w[] = { "name", "job", "heal", "look", "virtue", "compassion", "join", "give", "bye" };
	for (auto s : w) addWord(s);
	setPlace("Britain");
	setInput(kUltima4, kString); page(dir, "u4_talk", false, 7);
	setTalk(true, 2); page(dir, "u4_talk_ask");
	setTalk(true, 7); page(dir, "u4_talk_qty");
	setTalk(true, 4); setInput(kUltima4, kChoice, ""); page(dir, "u4_vendor");
	setTalk(true, 3); page(dir, "u4_yesno");
	setTalk(false, -1); setInput(kUltima4, kString); page(dir, "u4_type");
	setPlace("Despise  level 8"); setInput(kUltima4, kCommand); page(dir, "u4_dungeon");
	info.mapValid = false; setInput(kUltima4, kIntro); page(dir, "u4_intro");
	setInput(kUltima4, kOther); page(dir, "u4_other");
	info.mapValid = true; setPlace("Underworld 3");
	setInput(kUltima6, kCommand); page(dir, "u6_command");
	page(dir, "u6_more", true);
	setInput(kUltima6, kTarget); page(dir, "u6_target");
	setTalk(true, -1); clearWords();
	const char *w6[] = { "Lord", "British", "shrines", "gargoyles", "moongate", "orb", "Iolo", "Dupre", "Shamino", "runes" };
	for (auto s : w6) addWord(s);
	setInput(kUltima6, kString); page(dir, "u6_talk");

	PadUI::Canvas top = { 400, 240, tpx };
	PadUI::drawDashboard(top);
	save(dir, "dashboard", top);

	for (auto &c : PadUI::cutLabels()) printf("CUT: %s\n", c.c_str());
	printf("%s\n", PadUI::cutLabels().empty() ? "all labels fit" : "LABELS CUT");
	printf("%s\n", g_bad ? "SOME BUTTONS UNREACHABLE" : "every button reachable with the D-pad");
	return PadUI::cutLabels().empty() ? 0 : 1;
}
