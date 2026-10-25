/* Pure pixel logic of the stereo compositor (no 3DS dependencies), shared by
 * the backend and the desktop test in tools/test_compose.cpp.
 *
 * splitLayers: one RGBA8888 frame + a per-pixel level map -> one RGBA layer
 * per level (0 = fully transparent), with the ground layer (level 0)
 * inpainted under every elevated pixel so per-layer parallax never exposes
 * a hole.
 */
#ifndef BACKENDS_3DS_STEREO_COMPOSE_H
#define BACKENDS_3DS_STEREO_COMPOSE_H

#include <stdint.h>

namespace N3DS {

// 32-bit unsigned that matches ScummVM's uint32 on every toolchain (on
// newlib/ARM uint32_t is 'unsigned long', which breaks template deduction).
typedef unsigned int sc_u32;
typedef unsigned char sc_u8;


// rowOf(level, y) must return a pointer to that layer's row y (w uint32s).
template<typename RowFn>
inline void splitLayers(const sc_u32 *frame, const sc_u8 *depth, int w, int h,
                        int levels, RowFn rowOf) {
	for (int y = 0; y < h; ++y) {
		const sc_u32 *src = frame + y * w;
		const sc_u8 *d = depth + y * w;
		sc_u32 *rows[16];
		for (int l = 0; l < levels; ++l)
			rows[l] = rowOf(l, y);

		for (int x = 0; x < w; ++x) {
			const int lvl = d[x] < levels ? d[x] : levels - 1;
			for (int l = 0; l < levels; ++l)
				rows[l][x] = (l == lvl) ? src[x] : 0u;
		}

		// Inpaint ground holes from the nearest ground pixel in the row
		// (bounded search to the right, unbounded memory to the left).
		sc_u32 *g0 = rows[0];
		bool rowHasGround = false;
		for (int x = 0; x < w && !rowHasGround; ++x)
			rowHasGround = (d[x] == 0);
		if (!rowHasGround) {
			// a row with no ground at all (a full-width text bar): continue the
			// row above so the shifted bar never reveals black
			if (y > 0)
				for (int x = 0; x < w; ++x) g0[x] = rowOf(0, y - 1)[x];
			else
				for (int x = 0; x < w; ++x) g0[x] = 0x000000FFu;
			continue;
		}
		int lastGround = -1;
		for (int x = 0; x < w; ++x) {
			if (d[x] == 0) { lastGround = x; continue; }
			int nxt = -1;
			for (int k = x + 1; k < w && k < x + 24; ++k)
				if (d[k] == 0) { nxt = k; break; }
			int pick = -1;
			if (lastGround >= 0 && nxt >= 0)
				pick = (x - lastGround <= nxt - x) ? lastGround : nxt;
			else if (lastGround >= 0)
				pick = lastGround;
			else if (nxt >= 0)
				pick = nxt;
			g0[x] = (pick >= 0) ? src[pick] : 0x000000FFu;
		}
	}
}

// Parallax (in screen px) of a level for one eye. Level 1 is the screen
// plane; lower levels recede, higher pop out. Right eye (+1) shifts
// in-front content left.
inline int layerShift(int level, int eye, float slider, float shiftPerLevel, float scale) {
	const float par = (float)(level - 1) * shiftPerLevel * slider * scale;
	const float v = -(float)eye * par;
	return (int)(v + (v >= 0 ? 0.5f : -0.5f));
}

} // namespace N3DS

#endif
