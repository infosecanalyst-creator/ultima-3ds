#!/usr/bin/env python3
"""Drive the patched native ScummVM under Xvfb with xdotool; grab screenshots
with scrot and depth dumps from the STEREO3DS_DUMP hook.

  drive.py <scummvm bin> <gameid> <datadir> <outdir> "<script>"
script tokens: key names (xdotool), 'type:Text', 'shot', numbers = seconds.
Each 'shot' also records which depth dump index was current, so the two can
be paired for make_stereo.py.
"""
import os, subprocess, sys, time, glob, signal

binp, game, data, out, script = sys.argv[1:6]
os.makedirs(out + "/user", exist_ok=True)
disp = os.environ.get("XDISPLAY", ":79")
ini = out + "/user/scummvm.ini"
open(ini, "w").write("[scummvm]\nextrapath=%s/dists/engine-data\nmute=true\nfullscreen=false\n[%s]\ngameid=%s\npath=%s\nengineid=ultima\n"
                     % (os.path.dirname(os.path.abspath(binp)), game, game, data))
xv = subprocess.Popen(["Xvfb", disp, "-screen", "0", "1280x800x24"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1)
env = dict(os.environ, DISPLAY=disp, STEREO3DS_DUMP=out, STEREO3DS_EVERY=os.environ.get("EVERY", "1"))
log = open(out + "/log", "w")
sv = subprocess.Popen([binp, "-c", ini, game], env=env, stdout=log, stderr=log)
time.sleep(float(os.environ.get("STARTUP", "12")))

def xdo(*a):
    subprocess.run(["xdotool"] + list(a), env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# focus the game window (Xvfb has no window manager)
w = subprocess.run(["xdotool", "search", "--name", "ScummVM"], env=env, capture_output=True, text=True).stdout.split()
if w:
    xdo("windowactivate", "--sync", w[0]); xdo("windowfocus", "--sync", w[0])

n = 0
pairs = []
for tok in script.split():
    if tok == "shot":
        n += 1
        d = sorted(glob.glob(out + "/depth_*.pgm"))
        subprocess.run(["scrot", "-o", "%s/shot_%03d.png" % (out, n)], env=env)
        pairs.append((n, d[-1] if d else None))
        time.sleep(0.4)
    elif tok.startswith("click:"):
        # click:fx:fy  fractions of the game window (found as the non-black bbox of a screenshot)
        fx, fy = [float(v) for v in tok[6:].split(":")]
        subprocess.run(["scrot", "-o", out + "/_probe.png"], env=env)
        from PIL import Image
        bb = Image.open(out + "/_probe.png").getbbox()
        x = int(bb[0] + (bb[2] - bb[0]) * fx); y = int(bb[1] + (bb[3] - bb[1]) * fy)
        xdo("mousemove", str(x), str(y)); time.sleep(0.2); xdo("click", "1"); time.sleep(0.3)
    elif tok.startswith("type:"):
        xdo("type", "--delay", "120", tok[5:]); time.sleep(0.3)
    else:
        try:
            time.sleep(float(tok))
        except ValueError:
            xdo("key", "--delay", "80", tok); time.sleep(0.3)

sv.send_signal(signal.SIGTERM); time.sleep(1); sv.kill(); xv.kill()
open(out + "/pairs.txt", "w").write("\n".join("%d %s" % p for p in pairs))
print("shots:", n, "depth dumps:", len(glob.glob(out + "/depth_*.pgm")))
