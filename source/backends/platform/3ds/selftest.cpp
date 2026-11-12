// selftest.c - run a port's test script on a real 3DS, without anyone holding it.
//
// Linked into a port with the --wrap flags in selftest.mk. When the port is
// started with the argument "--selftest=<script>" (the test agent does this),
// the script presses buttons, taps and moves the sticks by replacing what the
// HID functions return, and saves both screens as BMP files. At the end it
// tells the Homebrew loader to start the test agent again and exits, so the
// next test can run. Without the argument nothing changes.
//
// Script (same language as the emulator suite, scripts/<port>.txt):
//   wait N | press BTN [frames] | hold BTN | release BTN | chord HELD BTN [frames]
//   tap X Y [frames] | stick L|C X Y FRAMES | shot NAME | hang N | end
// One "frame" is one call of hidScanInput.
#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {   // the same file builds as C or C++ (ScummVM's build only compiles C++)
#endif

extern int __system_argc;
extern char** __system_argv;

void __real_hidScanInput(void);
u32 __real_hidKeysHeld(void);
u32 __real_hidKeysDown(void);
u32 __real_hidKeysDownRepeat(void);
u32 __real_hidKeysUp(void);
void __real_hidTouchRead(touchPosition* pos);
void __real_hidCircleRead(circlePosition* pos);
void __real_irrstCstickRead(circlePosition* pos);
bool __real_aptMainLoop(void);

static int st_done;               // script finished: the app is asked to quit
static u64 st_doneFrame;
static int st_on = -1;            // -1 not checked yet, 0 off, 1 running
static char st_dir[256];          // results folder
static char* st_lines[4096];
static int st_n, st_pc;
static FILE* st_log;
static u64 st_frame;
static long st_wait;              // frames left in the current step
static u32 st_held, st_prev, st_tapFrames, st_hold;
static int st_tx, st_ty;
static int st_stick, st_sx, st_sy, st_stickFrames;

static void st_logf(const char* fmt, ...) {
    if (!st_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(st_log, fmt, ap);
    va_end(ap);
    fflush(st_log);
}

static u32 st_button(const char* b) {
    static const struct { const char* n; u32 k; } m[] = {
        {"A", KEY_A}, {"B", KEY_B}, {"X", KEY_X}, {"Y", KEY_Y}, {"L", KEY_L}, {"R", KEY_R},
        {"ZL", KEY_ZL}, {"ZR", KEY_ZR}, {"START", KEY_START}, {"SELECT", KEY_SELECT},
        {"UP", KEY_DUP}, {"DOWN", KEY_DDOWN}, {"LEFT", KEY_DLEFT}, {"RIGHT", KEY_DRIGHT},
        {"CUP", KEY_CSTICK_UP}, {"CDOWN", KEY_CSTICK_DOWN}, {"CLEFT", KEY_CSTICK_LEFT}, {"CRIGHT", KEY_CSTICK_RIGHT}};
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++)
        if (!strcmp(m[i].n, b)) return m[i].k;
    st_logf("unknown button %s\n", b);
    return 0;
}

// Both screens as 24-bit BMP files, read from what the screens are showing now.
static void st_bmp(const char* path, const u8* fb, int fmt, int w, int h, int stride) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    int row = (w * 3 + 3) & ~3;
    u32 size = 54 + row * h;
    u8 hd[54] = {'B', 'M'};
    memcpy(hd + 2, &size, 4);
    u32 v = 54; memcpy(hd + 10, &v, 4);
    v = 40; memcpy(hd + 14, &v, 4);
    memcpy(hd + 18, &w, 4);
    memcpy(hd + 22, &h, 4);
    hd[26] = 1; hd[28] = 24;
    fwrite(hd, 1, 54, f);
    u8* line = (u8*)malloc(row);
    int bpp = fmt == GSP_RGBA8_OES ? 4 : fmt == GSP_BGR8_OES ? 3 : 2;
    for (int y = h - 1; y >= 0; y--) {           // BMP rows go bottom-up
        memset(line, 0, row);
        for (int x = 0; x < w; x++) {
            // 3DS framebuffers are rotated: column x is stored as a run of 240 pixels, y from the bottom.
            const u8* p = fb + x * stride + (239 - y) * bpp;
            u8 r = 0, g = 0, b = 0;
            if (fmt == GSP_RGBA8_OES) { r = p[3]; g = p[2]; b = p[1]; }
            else if (fmt == GSP_BGR8_OES) { r = p[2]; g = p[1]; b = p[0]; }
            else {
                u16 c = p[0] | (p[1] << 8);
                if (fmt == GSP_RGB565_OES) { r = (c >> 11) << 3; g = ((c >> 5) & 63) << 2; b = (c & 31) << 3; }
                else if (fmt == GSP_RGB5_A1_OES) { r = (c >> 11) << 3; g = ((c >> 6) & 31) << 3; b = ((c >> 1) & 31) << 3; }
                else { r = (c >> 12) << 4; g = ((c >> 8) & 15) << 4; b = ((c >> 4) & 15) << 4; }
            }
            line[x * 3] = b; line[x * 3 + 1] = g; line[x * 3 + 2] = r;
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
}

static void st_shot(const char* name) {
    GSPGPU_CaptureInfo ci;
    if (R_FAILED(GSPGPU_ImportDisplayCaptureInfo(&ci))) { st_logf("shot %s: capture failed\n", name); return; }
    char p[512];
    for (int s = 0; s < 2; s++) {
        GSPGPU_CaptureInfoEntry* e = &ci.screencapture[s];
        int fmt = e->format & 7;
        // the top screen in the 800-pixel mode: neither the 2D (bit 6) nor the 3D (bit 5) flag
        int w = s == 0 ? ((e->format & 0x60) == 0 ? 800 : 400) : 320;
        snprintf(p, sizeof p, "%s/%s_%s.bmp", st_dir, name, s == 0 ? "top" : "bottom");
        st_bmp(p, (const u8*)e->framebuf0_vaddr, fmt, w, 240, e->framebuf_widthbytesize);
    }
    st_logf("shot %s (frame %llu)\n", name, (unsigned long long)st_frame);
}

// Start the test agent again when the script ends (or the game hangs).
static void st_finish(const char* why) {
    st_logf("%s\ndone frames=%llu\n", why, (unsigned long long)st_frame);
    if (st_log) fclose(st_log);
    st_log = NULL;
    Handle h;
    if (R_SUCCEEDED(svcConnectToPort(&h, "hb:ldr"))) {
        static const char path[] = "/3ds/testagent/testagent.3dsx";
        u32* cmd = getThreadCommandBuffer();
        cmd[0] = IPC_MakeHeader(2, 0, 2);
        cmd[1] = IPC_Desc_StaticBuffer(sizeof path, 0);
        cmd[2] = (u32)path;
        svcSendSyncRequest(h);
        static u32 argv[0x100];
        argv[0] = 1;
        strcpy((char*)&argv[1], "sdmc:/3ds/testagent/testagent.3dsx");
        cmd = getThreadCommandBuffer();
        cmd[0] = IPC_MakeHeader(3, 0, 2);
        cmd[1] = IPC_Desc_StaticBuffer(sizeof argv, 1);
        cmd[2] = (u32)argv;
        svcSendSyncRequest(h);
        svcCloseHandle(h);
    }
    // Quit the normal way: aptMainLoop() now returns false, so the game runs its
    // own cleanup (calling exit() from here crashed the GSP thread on hardware).
    // A game that never asks aptMainLoop is ended 10 seconds later.
    st_done = 1;
    st_doneFrame = st_frame;
}

static void st_load(const char* script) {
    FILE* f = fopen(script, "r");
    if (!f) return;
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    for (char* l = strtok(buf, "\n"); l && st_n < 4096; l = strtok(NULL, "\n")) {
        char* h = strchr(l, '#');
        if (h) *h = 0;
        while (*l == ' ' || *l == '\t') l++;
        size_t k = strlen(l);
        while (k && (l[k - 1] == ' ' || l[k - 1] == '\r' || l[k - 1] == '\t')) l[--k] = 0;
        if (*l) st_lines[st_n++] = l;
    }
}

static void st_init(void) {
    st_on = 0;
    const char* script = NULL;
    for (int i = 0; i < __system_argc; i++)
        if (!strncmp(__system_argv[i], "--selftest=", 11)) script = __system_argv[i] + 11;
    if (!script) return;
    snprintf(st_dir, sizeof st_dir, "sdmc:/3ds/testagent/results");
    mkdir("sdmc:/3ds/testagent", 0777);
    mkdir(st_dir, 0777);
    char lp[300];
    snprintf(lp, sizeof lp, "%s/run.log", st_dir);
    st_log = fopen(lp, "w");
    st_load(script);
    st_logf("selftest: %d steps from %s\n", st_n, script);
    st_on = 1;
}

// One step of the script per frame; returns when the step needs more frames.
static void st_step(void) {
    st_prev = st_held;
    if (st_tapFrames) st_tapFrames--;
    if (st_stickFrames && !--st_stickFrames) st_stick = 0;
    if (st_wait > 0) {
        if (--st_wait == 0) st_held &= st_hold;   // a press ends; held buttons stay
        return;
    }
    while (st_pc < st_n) {
        char cmd[32] = "", a1[64] = "", a2[64] = "", a3[64] = "", a4[64] = "";
        sscanf(st_lines[st_pc++], "%31s %63s %63s %63s %63s", cmd, a1, a2, a3, a4);
        st_logf("> %s %s %s %s %s\n", cmd, a1, a2, a3, a4);
        if (!strcmp(cmd, "wait")) { st_wait = atol(a1); return; }
        if (!strcmp(cmd, "press")) { st_held |= st_button(a1); st_wait = *a2 ? atol(a2) : 6; return; }
        if (!strcmp(cmd, "hold")) { st_hold |= st_button(a1); st_held |= st_button(a1); continue; }
        if (!strcmp(cmd, "release")) { st_hold &= ~st_button(a1); st_held &= ~st_button(a1); continue; }
        if (!strcmp(cmd, "chord")) { st_held |= st_button(a1) | st_button(a2); st_wait = *a3 ? atol(a3) : 6; return; }
        if (!strcmp(cmd, "tap")) { st_tx = atoi(a1); st_ty = atoi(a2); st_tapFrames = *a3 ? atoi(a3) : 6; st_held |= KEY_TOUCH; st_wait = st_tapFrames; return; }
        if (!strcmp(cmd, "stick")) { st_stick = a1[0] == 'C' ? 2 : 1; st_sx = atoi(a2); st_sy = atoi(a3); st_stickFrames = atoi(a4); continue; }
        if (!strcmp(cmd, "shot")) { st_shot(a1); continue; }
        if (!strcmp(cmd, "hang")) continue;   // the emulator suite checks hangs; here the agent times out
        if (!strcmp(cmd, "end")) break;
    }
    st_finish("end of script");
}

void __wrap_hidScanInput(void) {
    __real_hidScanInput();
    if (st_on < 0) st_init();
    if (st_on != 1) return;
    st_frame++;
    if (st_done) {
        st_held = st_prev = 0;
        if (st_frame > st_doneFrame + 600) exit(0);
        return;
    }
    st_step();
}
bool __wrap_aptMainLoop(void) {
    if (st_done) return false;
    return __real_aptMainLoop();
}
u32 __wrap_hidKeysHeld(void) { return st_on == 1 ? st_held : __real_hidKeysHeld(); }
u32 __wrap_hidKeysDown(void) { return st_on == 1 ? (st_held & ~st_prev) : __real_hidKeysDown(); }
u32 __wrap_hidKeysDownRepeat(void) { return st_on == 1 ? (st_held & ~st_prev) : __real_hidKeysDownRepeat(); }
u32 __wrap_hidKeysUp(void) { return st_on == 1 ? (st_prev & ~st_held) : __real_hidKeysUp(); }
void __wrap_hidTouchRead(touchPosition* pos) {
    if (st_on != 1) { __real_hidTouchRead(pos); return; }
    pos->px = st_tapFrames ? st_tx : 0;
    pos->py = st_tapFrames ? st_ty : 0;
}
void __wrap_hidCircleRead(circlePosition* pos) {
    if (st_on != 1) { __real_hidCircleRead(pos); return; }
    pos->dx = st_stick == 1 ? st_sx : 0;
    pos->dy = st_stick == 1 ? st_sy : 0;
}
void __wrap_irrstCstickRead(circlePosition* pos) {
    if (st_on != 1) { __real_irrstCstickRead(pos); return; }
    pos->dx = st_stick == 2 ? st_sx : 0;
    pos->dy = st_stick == 2 ? st_sy : 0;
}

#ifdef __cplusplus
}
#endif
