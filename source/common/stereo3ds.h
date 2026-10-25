/* Stereoscopic depth side-channel for the 3DS backend (ultima-3ds port).
 *
 * ScummVM engines hand the backend one flat frame. To get real stereo on
 * the 3DS we let an engine tag every pixel it draws with a depth level; the
 * backend then splits the frame into layers and renders each one with a
 * per-eye horizontal offset proportional to its level and the 3D slider.
 *
 * Engines only ever touch this header. If the backend is not the 3DS one
 * (or stereo is off) `setDepthMap` is null and the calls are no-ops.
 */
#ifndef COMMON_STEREO3DS_H
#define COMMON_STEREO3DS_H

#include "common/scummsys.h"

namespace Stereo3DS {

enum Level {
	kGround = 0,   // terrain, floors, water: sits at (or slightly behind) the screen plane
	kObject = 1,   // objects, trees, walls, furniture
	kActor  = 2,   // creatures, NPCs, the party
	kFront  = 3,   // text, gumps, cursors, roofs: in front of everything
	kLevels = 8    // the backend quantises to at most this many layers
};

// Set by the 3DS backend; null elsewhere. `depth` is one byte per pixel
// (0..kLevels-1), `w`/`h` the game screen size, `pitch` in bytes.
typedef void (*SetDepthMapFn)(const byte *depth, int w, int h, int pitch);
extern SetDepthMapFn setDepthMap;

inline bool enabled() { return setDepthMap != nullptr; }

inline void push(const byte *depth, int w, int h, int pitch) {
	if (setDepthMap)
		setDepthMap(depth, w, h, pitch);
}

// Small helper engines can embed: a depth plane plus a "current level"
// that blits stamp into it.
class DepthPlane {
public:
	DepthPlane() : _buf(nullptr), _w(0), _h(0), _level(kGround) {}
	~DepthPlane() { delete[] _buf; }

	void create(int w, int h) {
		delete[] _buf;
		_w = w; _h = h;
		_buf = new byte[w * h];
		clear();
	}
	bool valid() const { return _buf != nullptr; }
	void clear(byte level = kGround) { if (_buf) memset(_buf, level, _w * _h); }

	void setLevel(byte level) { _level = level; }
	byte level() const { return _level; }

	// Stamp the current level over a rectangle (clipped).
	void stampRect(int x, int y, int w, int h) {
		if (!_buf) return;
		if (x < 0) { w += x; x = 0; }
		if (y < 0) { h += y; y = 0; }
		if (x + w > _w) w = _w - x;
		if (y + h > _h) h = _h - y;
		for (int j = 0; j < h; ++j)
			memset(_buf + (y + j) * _w + x, _level, w);
	}
	// Stamp one pixel (caller clips).
	inline void stamp(int x, int y) { _buf[y * _w + x] = _level; }

	void push() const { Stereo3DS::push(_buf, _w, _h, _w); }

	byte *data() { return _buf; }
	int width() const { return _w; }
	int height() const { return _h; }

private:
	byte *_buf;
	int _w, _h;
	byte _level;
};

// RAII: set a level for the duration of a scope.
class ScopedLevel {
public:
	ScopedLevel(DepthPlane &p, byte level) : _p(p), _prev(p.level()) { _p.setLevel(level); }
	~ScopedLevel() { _p.setLevel(_prev); }
private:
	DepthPlane &_p;
	byte _prev;
};

} // namespace Stereo3DS

#endif
