// ultima-3ds: the depth-map hook. Null on every backend except the 3DS,
// which assigns it in stereo3dsInit().
//
// Desktop debugging: with STEREO3DS_DUMP=<dir> in the environment, a hook is
// installed that writes every Nth depth map as a PGM (N = STEREO3DS_EVERY,
// default 30). Pair with ScummVM's own screenshots (Alt+S) to inspect the
// layering with the real engines, no 3DS needed.
#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/stereo3ds.h"

#ifndef __3DS__
#include <stdio.h>
#include <stdlib.h>
#endif

namespace Stereo3DS {

SetDepthMapFn setDepthMap = nullptr;

#ifndef __3DS__
static void dumpDepth(const byte *depth, int w, int h, int pitch) {
	static int n = 0, every = -1;
	if (every < 0) { const char *e = getenv("STEREO3DS_EVERY"); every = e ? atoi(e) : 30; if (every < 1) every = 1; }
	if ((n++ % every) != 0) return;
	char path[512];
	snprintf(path, sizeof(path), "%s/depth_%05d.pgm", getenv("STEREO3DS_DUMP"), n / every);
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P5\n%d %d\n255\n", w, h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			fputc(depth[y * pitch + x] * 32, f);   // levels 0..7 -> 0..224
	fclose(f);
}
static struct Installer {
	Installer() { if (getenv("STEREO3DS_DUMP")) setDepthMap = dumpDepth; }
} g_installer;
#endif

}
