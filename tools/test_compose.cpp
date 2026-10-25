// Desktop test for the stereo compositor: builds a synthetic "Ultima-like"
// frame (tiled ground, a few sprites, a text bar) with a depth map, runs the
// real splitLayers/layerShift, composites both eyes and writes PPMs plus a
// fringe check (no black pixels where the ground should show through).
//   g++ -std=c++11 -I../source/backends/platform/3ds test_compose.cpp -o test_compose && ./test_compose out/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "stereo_compose.h"
using namespace N3DS;

static const int W = 320, H = 200, LEVELS = 4;
static uint32_t rgba(int r, int g, int b) { return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF; }

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    std::vector<uint32_t> frame(W * H);
    std::vector<uint8_t> depth(W * H, 0);
    // ground: 16px checker of two greens, level 0
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
        frame[y * W + x] = (((x / 16) + (y / 16)) & 1) ? rgba(40, 120, 40) : rgba(60, 140, 60);
    // objects: trees (level 1) as brown squares, actors (level 2) as red, text bar (level 3)
    auto box = [&](int x0, int y0, int w, int h, uint32_t c, int lvl) {
        for (int y = y0; y < y0 + h; ++y) for (int x = x0; x < x0 + w; ++x) { frame[y * W + x] = c; depth[y * W + x] = lvl; }
    };
    for (int i = 0; i < 8; ++i) box(30 + i * 36, 40 + (i % 3) * 30, 12, 16, rgba(120, 80, 30), 1);
    box(150, 90, 10, 14, rgba(220, 40, 40), 2);
    box(200, 120, 10, 14, rgba(240, 240, 240), 2);
    box(0, 184, W, 16, rgba(10, 10, 60), 3);
    for (int x = 8; x < 200; x += 6) box(x, 190, 3, 5, rgba(255, 255, 160), 3);

    std::vector<std::vector<uint32_t>> layers(LEVELS, std::vector<uint32_t>(W * H));
    splitLayers(frame.data(), depth.data(), W, H, LEVELS,
                [&](int l, int y) { return layers[l].data() + y * W; });

    // fringe check: ground layer must be fully opaque and never black
    int black = 0, transparent = 0;
    for (int i = 0; i < W * H; ++i) {
        if ((layers[0][i] & 0xFF) == 0) ++transparent;
        else if ((layers[0][i] >> 8) == 0) ++black;
    }
    printf("ground layer: %d transparent, %d black pixels (want 0 / 0)\n", transparent, black);

    // composite both eyes at slider 1, 2.5px/level, scale 1
    for (int eye = -1; eye <= 1; eye += 2) {
        std::vector<uint8_t> img(W * H * 3, 0);
        for (int l = 0; l < LEVELS; ++l) {
            const int dx = layerShift(l, eye, 1.0f, 2.5f, 1.0f);
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                const uint32_t c = layers[l][y * W + x];
                if ((c & 0xFF) == 0) continue;
                const int xx = x + dx; if (xx < 0 || xx >= W) continue;
                uint8_t *p = &img[(y * W + xx) * 3];
                p[0] = c >> 24; p[1] = (c >> 16) & 0xFF; p[2] = (c >> 8) & 0xFF;
            }
        }
        // any black left in the composite (other than the text bar's dark blue) is a hole
        int holes = 0;
        for (int y = 0; y < 184; ++y) for (int x = 6; x < W - 6; ++x) {
            const uint8_t *p = &img[(y * W + x) * 3];
            if (p[0] == 0 && p[1] == 0 && p[2] == 0) ++holes;
        }
        printf("eye %+d: shifts per level = %d %d %d %d, holes = %d (want 0)\n", eye,
               layerShift(0, eye, 1, 2.5f, 1), layerShift(1, eye, 1, 2.5f, 1), layerShift(2, eye, 1, 2.5f, 1), layerShift(3, eye, 1, 2.5f, 1), holes);
        char path[512]; snprintf(path, sizeof(path), "%s/eye_%s.ppm", out, eye < 0 ? "left" : "right");
        FILE *f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H); fwrite(img.data(), 1, img.size(), f); fclose(f);
    }
    return 0;
}
