/* vr_renderer.h - frame loop, eye buffers, composition layers (ported from CSVR engine/vr/VrRenderer.h) */
#pragma once

#include "vr_base.h"
#include "vr_math.h"

enum VRConfig {
	VR_CONFIG_MODE, VR_CONFIG_NEED_RECENTER,
	VR_CONFIG_MOUSE_SIZE, VR_CONFIG_MOUSE_X, VR_CONFIG_MOUSE_Y,
	VR_CONFIG_VIEWPORT_WIDTH, VR_CONFIG_VIEWPORT_HEIGHT, VR_CONFIG_VIEWPORT_MSAA,
	VR_CONFIG_SCREEN_WIDTH, VR_CONFIG_SCREEN_HEIGHT,   /* sub-rect of eye buffer 0 used in screen mode */
	VR_CONFIG_MAX
};

enum VRConfigFloat {
	VR_CONFIG_CANVAS_DISTANCE, VR_CONFIG_MENU_YAW, VR_CONFIG_RECENTER_YAW,
	VR_CONFIG_VIEWPORT_FOVX, VR_CONFIG_VIEWPORT_FOVY, VR_CONFIG_VIEWPORT_SUPERSAMPLING,
	VR_CONFIG_FLOAT_MAX
};

enum VRMode {
	VR_MODE_MONO_SCREEN,   /* flat menu/console screen, rendered once into eye buffer 0 */
	VR_MODE_STEREO_6DOF    /* full stereo world rendering */
};

void VR_GetResolution( engine_t* engine, int *pWidth, int *pHeight );
void VR_InitRenderer( engine_t* engine, bool multiview );
void VR_DestroyRenderer( engine_t* engine );

bool VR_InitFrame( engine_t* engine );
void VR_BeginFrame( engine_t* engine, int fboIndex );
void VR_EndFrame( engine_t* engine, int fboIndex );
void VR_FinishFrame( engine_t* engine );

int VR_GetConfig( enum VRConfig config );
void VR_SetConfig( enum VRConfig config, int value);
float VR_GetConfigFloat( enum VRConfigFloat config );
void VR_SetConfigFloat( enum VRConfigFloat config, float value );

void VR_BindFramebuffer(engine_t *engine, int fboIndex);
unsigned int VR_GetEyeFBO(engine_t *engine, int fboIndex);
void VR_Recenter(engine_t* engine);
bool VR_DidRecenter(void);
XrPosef VR_GetView(int eye);          /* eye pose in CurrentSpace */
XrFovf VR_GetFov(int eye);            /* per-eye asymmetric fov (radians) */
XrPosef VR_GetHeadPose(void);         /* head pose in CurrentSpace for this frame */
int VR_GetRefreshRate(void);
void VR_SetRefreshRate(int refresh);
