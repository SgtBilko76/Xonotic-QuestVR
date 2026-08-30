/*
 * vr_base.h - OpenXR instance/session/state for XonoticQuest.
 * Ported from Team Beef's CSVR (engine/vr/VrBase.h, VrFramebuffer.h). Engine-agnostic: no quakedef.h here.
 */
#pragma once

#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "XonoticVR", __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "XonoticVR", __VA_ARGS__)

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>
#ifndef XR_USE_PLATFORM_ANDROID
#define XR_USE_PLATFORM_ANDROID 1
#endif
#ifndef XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdbool.h>
#include <stdint.h>

void GLCheckErrors(const char* file, int line);
#define GL(func) func; GLCheckErrors(__FILE__ , __LINE__);

struct engine_s;
struct engine_s* VR_GetEngine(void);
void OXR_CheckErrors(XrInstance instance, XrResult result, const char* function, bool failOnError);
#define OXR(func) OXR_CheckErrors(VR_GetEngine()->appState.Instance, func, #func, true);

enum { ovrMaxLayerCount = 2 };
enum { ovrMaxNumEyes = 2 };

typedef union {
	XrCompositionLayerProjection Projection;
	XrCompositionLayerCylinderKHR Cylinder;
	XrCompositionLayerQuad Quad;
} ovrCompositorLayer_Union;

typedef struct {
	XrSwapchain Handle;
	uint32_t Width;
	uint32_t Height;
} ovrSwapChain;

typedef struct {
	int Width;
	int Height;
	int Multisamples;
	bool UseMultiview;
	uint32_t TextureSwapChainLength;
	uint32_t TextureSwapChainIndex;
	ovrSwapChain ColorSwapChain;
	XrSwapchainImageOpenGLESKHR* ColorSwapChainImage;
	unsigned int* DepthBuffers;
	unsigned int* FrameBuffers;
} ovrFramebuffer;

typedef struct {
	bool Multiview;
	ovrFramebuffer FrameBuffer[ovrMaxNumEyes];
} ovrRenderer;

typedef struct {
	int Focused;

	XrInstance Instance;
	XrSession Session;
	XrViewConfigurationProperties ViewportConfig;
	XrViewConfigurationView ViewConfigurationView[ovrMaxNumEyes];
	XrSystemId SystemId;
	XrSpace HeadSpace;
	XrSpace StageSpace;
	XrSpace FakeStageSpace;
	XrSpace CurrentSpace;
	int SessionActive;

	int SwapInterval;
	// These threads will be marked as performance threads.
	int MainThreadTid;
	int RenderThreadTid;
	ovrRenderer Renderer;
} ovrApp;

typedef struct {
	JavaVM* Vm;
	jobject ActivityObject;
	JNIEnv* Env;
} ovrJava;

typedef struct engine_s {
	uint64_t frameIndex;
	ovrApp appState;
	XrTime predictedDisplayTime;
} engine_t;

enum VRPlatformFlag {
	VR_PLATFORM_CONTROLLER_PICO,
	VR_PLATFORM_CONTROLLER_QUEST,
	VR_PLATFORM_EXTENSION_INSTANCE,
	VR_PLATFORM_EXTENSION_PERFORMANCE,
	VR_PLATFORM_EXTENSION_REFRESH,
	VR_PLATFORM_TRACKING_FLOOR,
	VR_PLATFORM_MAX
};

/* vr_base.c */
void VR_Init( void* system, const char* name, int version );
void VR_Destroy( engine_t* engine );
void VR_EnterVR( engine_t* engine );
void VR_LeaveVR( engine_t* engine );
bool VR_GetPlatformFlag(enum VRPlatformFlag flag);
void VR_SetPlatformFLag(enum VRPlatformFlag flag, bool value);
bool VR_HasCylinderLayerExt(void);

/* vr_framebuffer.c */
void ovrApp_Clear(ovrApp* app);
void ovrApp_Destroy(ovrApp* app);
int ovrApp_HandleXrEvents(ovrApp* app);

void ovrFramebuffer_Acquire(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_Resolve(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_Release(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_SetCurrent(ovrFramebuffer* frameBuffer);
void ovrFramebuffer_SetNone(void);
unsigned int ovrFramebuffer_GetCurrentFBO(ovrFramebuffer* frameBuffer);

void ovrRenderer_Create(XrSession session, ovrRenderer* renderer, bool useMultiview, int width, int height, int multisamples);
void ovrRenderer_Destroy(ovrRenderer* renderer);
void ovrRenderer_MouseCursor(ovrRenderer* renderer, int x, int y, int sx, int sy);
