#ifndef BACKENDS_3DS_STEREO3DS_H
#define BACKENDS_3DS_STEREO3DS_H

#include <citro3d.h>

namespace N3DS {
class Sprite;

void stereo3dsInit();
void stereo3dsShutdown();
void stereo3dsSetEnabled(bool on);
bool stereo3dsEnabled();
bool stereo3dsActive();
int  stereo3dsEyes();
float stereo3dsSlider();
C3D_RenderTarget *stereo3dsTarget();
void stereo3dsPrepare(Sprite &game);
void stereo3dsRenderGame(int eye, Sprite &game, int modelviewLocation);
}

#endif
