/*
 * vr_api.h - the single interface between DarkPlaces and the OpenXR layer (XonoticQuest).
 *
 * Everything in vr/*.c except vr_host.c is engine-agnostic OpenXR plumbing (ported from
 * Team Beef's CSVR/QuakeQuest code) and must not include quakedef.h.  vr_host.c bridges the
 * two worlds: it implements the VRH_* functions the engine calls and the small VRH_* callback
 * set the VR layer uses to talk back into the engine (keys, commands, cvars, logging).
 *
 * All engine call sites are guarded with #ifdef VR_QUEST so desktop builds are unchanged.
 */
#ifndef VR_API_H
#define VR_API_H

#ifdef VR_QUEST

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifecycle (engine -> VR) ---- */
void  VRH_RegisterCvars(void);      /* early in Host_Init, before configs are executed */
void  VRH_Init(void);               /* after the GL context exists (Host_Init / VID_Start) */
void  VRH_Shutdown(void);
bool  VRH_Available(void);          /* OpenXR session created and usable */

/* ---- per frame (engine -> VR) ---- */
bool  VRH_FrameSetup(void);         /* poll events, xrWaitFrame/xrBeginFrame, input, poses; false => don't render */
void  VRH_BeginEye(int eye);        /* bind the eye swapchain as the engine's "screen" */
void  VRH_EndEye(int eye);
void  VRH_SubmitFrame(void);        /* xrEndFrame */
void  VRH_GetEyeResolution(int *width, int *height);
bool  VRH_GetHudRect(int *x, int *y, int *w, int *h);   /* 2D canvas rect for the current eye (false: full screen) */
bool  VRH_ScreenMode(void);         /* true: menu/console/loading -> mono render on a flat layer */

/* ---- view / projection ---- */
void  VRH_GetEyeOffset(int eye, float out_quake[3], float out_angles[3]); /* eye pose relative to the head: Quake units / Quake angles */
void  VRH_GetProjection(int eye, float znear, float zfar, float m16[16]);
void  VRH_GetUnionFovTangents(float *tanx, float *tany);
void  VRH_GetHMDAngles(float out_pitch_yaw_roll[3]);           /* Quake angles incl. artificial yaw */
void  VRH_GetHMDPosition(float out_quake[3]);                  /* head pos relative to recenter origin, Quake units */
float VRH_GetPlayerYawOffset(void);

/* ---- hands ---- */
extern float vr_gunorg[3];     /* world-space aim of the weapon hand (updated by VRH_GetGun) */
extern float vr_gunangles[3];  /* Quake angles of the weapon hand incl. artificial yaw */
bool  VRH_HasGun(void);
bool  VRH_GetGun(const float vieworg[3], float out_org_quake[3], float out_angles[3]); /* hand pose relative to the given view origin */
float VRH_GetWeaponScale(void);

/* ---- input (called from vid_sdl.c IN_Move) ---- */
void  VRH_HandleInput(void);
void  VRH_AddYaw(float degrees);
bool  VRH_GetMove(float *forward, float *side);   /* -1..1 thumbstick movement in the aim-yaw frame */
void  VRH_SetCursor(int x, int y);
bool  VRH_GetCursor(int *x, int *y);              /* laser-pointer cursor in screen pixels (top-left origin) */

/* ---- engine helpers used by the bridge (gl_backend.c) ---- */
void  GL_SetDefaultFramebuffer(int fbo);

/* ---- misc ---- */
void  VRH_Recenter(void);
float VRH_GetRefreshRate(void);
void  VRH_Vibrate(int hand, float duration_ms, float intensity);

/* ---- callbacks (VR -> engine), implemented in vr_host.c ---- */
void  VRH_KeyEvent(int key, int ascii, bool down);
void  VRH_Command(const char *cmd);
float VRH_CvarValue(const char *name);
void  VRH_Log(const char *fmt, ...);
bool  VRH_InGame(void);

#ifdef __cplusplus
}
#endif

#endif /* VR_QUEST */
#endif /* VR_API_H */
