#pragma once
#include "../base/base.h"

// Merged video mode support: reads the latest decoded video frame published
// by the player process (appsink mode) into the shared memory video frames
// ring, converts it to ARGB and lets the render engine composite it into the
// central controller's own UI window.

void video_live_init();
void video_live_shutdown();
bool video_live_has_data();
void video_live_update();
void video_live_set_enabled(bool bEnabled);
bool video_live_is_enabled();
u8* video_live_get_argb(int* piWidth, int* piHeight);
