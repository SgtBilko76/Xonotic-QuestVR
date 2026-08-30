/*
 * vr_host.c - DarkPlaces side of the VR bridge (XonoticQuest).
 *
 * Implements the VRH_* interface of vr_api.h on top of the OpenXR layer (vr_base/vr_renderer/vr_input)
 * and provides the callbacks the VR layer needs from the engine. This is the only file in vr/ that
 * includes quakedef.h.
 *
 * Coordinate conventions: OpenXR is x right / y up / -z forward (metres); Quake is x forward /
 * y left / z up (units).  quake = { -xr.z, -xr.x, xr.y } * vr_worldscale.
 */
#ifdef VR_QUEST

#include "quakedef.h"
#include "vr_api.h"
#include "vr_base.h"
#include "vr_renderer.h"
#include "vr_input.h"
#include "vr_math.h"

#include <SDL.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

cvar_t vr_worldscale = {CF_CLIENT | CF_ARCHIVE, "vr_worldscale", "39", "Xonotic units per metre (player is 64 units tall)"};
cvar_t vr_weaponscale = {CF_CLIENT | CF_ARCHIVE, "vr_weaponscale", "1", "scale of the hand-held weapon model"};
cvar_t vr_weaponpitchadjust = {CF_CLIENT | CF_ARCHIVE, "vr_weaponpitchadjust", "-20", "pitch correction applied to the controller aim pose (degrees)"};
cvar_t vr_weaponyawadjust = {CF_CLIENT | CF_ARCHIVE, "vr_weaponyawadjust", "0", "yaw correction applied to the controller aim pose (degrees)"};
cvar_t vr_yawmode = {CF_CLIENT | CF_ARCHIVE, "vr_yawmode", "0", "0: snap turning by cl_comfort degrees, 1: smooth turning at vr_turnspeed"};
cvar_t cl_comfort = {CF_CLIENT | CF_ARCHIVE, "cl_comfort", "45", "snap turn angle in degrees"};
cvar_t vr_turnspeed = {CF_CLIENT | CF_ARCHIVE, "vr_turnspeed", "120", "smooth turn speed in degrees per second"};
cvar_t cl_walkdirection = {CF_CLIENT | CF_ARCHIVE, "cl_walkdirection", "0", "0: thumbstick movement is relative to the head, 1: relative to the off-hand controller"};
cvar_t cl_righthanded = {CF_CLIENT | CF_ARCHIVE, "cl_righthanded", "1", "1: aim with the right controller, 0: aim with the left"};
cvar_t vr_eyeseparation = {CF_CLIENT | CF_ARCHIVE, "vr_eyeseparation", "1", "multiplier for the per-eye camera offset (1 normal, 0 mono, -1 mirrored - for debugging)"};
cvar_t vr_6dof = {CF_CLIENT | CF_ARCHIVE, "vr_6dof", "1", "apply positional head tracking to the view"};
cvar_t vr_hudscale = {CF_CLIENT | CF_ARCHIVE, "vr_hudscale", "0.55", "size of the 2D HUD canvas relative to the eye buffer"};
cvar_t vr_hudstereo = {CF_CLIENT | CF_ARCHIVE, "vr_hudstereo", "0.006", "per-eye horizontal HUD shift (fraction of width) that sets the HUD depth"};
cvar_t vr_screen_distance = {CF_CLIENT | CF_ARCHIVE, "vr_screen_distance", "5", "distance of the flat menu/console screen in metres"};
cvar_t vr_screen_curved = {CF_CLIENT | CF_ARCHIVE, "vr_screen_curved", "1", "1: cylinder layer for the menu screen, 0: flat quad"};
cvar_t vr_refreshrate = {CF_CLIENT | CF_ARCHIVE, "vr_refreshrate", "90", "requested display refresh rate (Hz)"};
cvar_t vr_msaa = {CF_CLIENT | CF_ARCHIVE, "vr_msaa", "2", "eye buffer MSAA samples (applied at startup)"};
cvar_t vr_supersampling = {CF_CLIENT | CF_ARCHIVE, "vr_supersampling", "1.0", "eye buffer resolution multiplier (applied at startup)"};
cvar_t vr_viewkick = {CF_CLIENT | CF_ARCHIVE, "vr_viewkick", "0", "scale of damage/weapon kick applied to the headset view (0 = none)"};
cvar_t vr_haptics = {CF_CLIENT | CF_ARCHIVE, "vr_haptics", "1", "controller vibration"};
cvar_t vr_thumbstick_deadzone = {CF_CLIENT | CF_ARCHIVE, "vr_thumbstick_deadzone", "0.15", "thumbstick dead zone"};
cvar_t vr_menu_pointer_scale = {CF_CLIENT | CF_ARCHIVE, "vr_menu_pointer_scale", "1.0", "sensitivity of the laser pointer on the menu screen"};

/* world-space aim of the weapon hand (engine reads these, see vr_api.h) */
float vr_gunorg[3];
float vr_gunangles[3];

static bool vrh_initialized = false;   /* VRH_Init ran */
static bool vrh_session = false;       /* OpenXR session + renderer exist */
static bool vrh_frame_active = false;  /* between a successful VRH_FrameSetup and VRH_SubmitFrame */
static bool vrh_screenmode = true;

static float vrh_yawoffset = 0;        /* accumulated artificial (snap/smooth) yaw, degrees */
static float vrh_hmdangles[3];         /* Quake pitch/yaw/roll of the HMD (no yaw offset) */
static float vrh_hmdpos_xr[3];         /* head position in stage space, metres */
static float vrh_recenter_xr[3];       /* stage-space head position at the last recenter */
static float vrh_playerheight = 0;     /* stage-space head height at the last recenter */
static bool vrh_needcalibrate = true;
static float vrh_eyeoffset[2][3];      /* per-eye offset from the head, Quake units */
static float vrh_eyeangles[2][3];      /* per-eye rotation relative to the head (canted displays), Quake angles */
static bool vrh_eyeangles_logged = false;
static float vrh_gunrel[3];            /* hand position relative to the head, world Quake units */
static bool vrh_gunvalid = false;

/* input state shared with VRH_HandleInput (vr_host_input part) */
static uint32_t vrh_buttons_prev[2];

void VRH_Log(const char *fmt, ...)
{
	char msg[MAX_INPUTLINE];
	va_list ap;
	va_start(ap, fmt);
	dpvsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	__android_log_print(ANDROID_LOG_INFO, "XonoticVR", "%s", msg);
	Con_Printf("VR: %s\n", msg);
}

/* ---------------------------------------------------------------- helpers */

static void XrToQuake(const float xr[3], float out[3], float scale)
{
	out[0] = -xr[2] * scale;
	out[1] = -xr[0] * scale;
	out[2] =  xr[1] * scale;
}

/* rotate a Quake-space vector about z by yaw degrees */
static void RotateYaw(float v[3], float yaw)
{
	float s = sinf(yaw * (float)M_PI / 180.0f), c = cosf(yaw * (float)M_PI / 180.0f);
	float x = v[0], y = v[1];
	v[0] = x * c - y * s;
	v[1] = x * s + y * c;
}

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

static int vrh_gldebug_count = 0;
static void GL_APIENTRY VRH_GLDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam)
{
	(void)source; (void)id; (void)length; (void)userParam;
	if (type != GL_DEBUG_TYPE_ERROR_KHR && type != GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR)
		return;
	if (vrh_gldebug_count++ < 200)
		__android_log_print(ANDROID_LOG_WARN, "XonoticGL", "type %x severity %x: %s", type, severity, message);
}

static void VRH_InitGLDebug(void)
{
	PFNGLDEBUGMESSAGECALLBACKKHRPROC pfnCallback = (PFNGLDEBUGMESSAGECALLBACKKHRPROC)eglGetProcAddress("glDebugMessageCallbackKHR");
	if (!pfnCallback || !Sys_CheckParm("-gldebug"))
		return;
	glEnable(GL_DEBUG_OUTPUT_KHR);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
	pfnCallback(VRH_GLDebugCallback, NULL);
	VRH_Log("KHR_debug output enabled");
}

static void VRH_InitPlatformFlags(void)
{
	const char *manufacturer = getenv("xr_manufacturer");
	if (!manufacturer)
		manufacturer = "";
	VRH_Log("xr_manufacturer = %s", manufacturer);
	if (strcmp(manufacturer, "PICO") == 0)
	{
		VR_SetPlatformFLag(VR_PLATFORM_CONTROLLER_PICO, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_INSTANCE, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PERFORMANCE, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_REFRESH, true);
	}
	else
	{
		VR_SetPlatformFLag(VR_PLATFORM_CONTROLLER_QUEST, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PERFORMANCE, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_REFRESH, true);
	}
	VR_SetPlatformFLag(VR_PLATFORM_TRACKING_FLOOR, true);
}

/* ---------------------------------------------------------------- lifecycle */

static bool vrh_cvars_registered = false;

void VRH_RegisterCvars(void)
{
	if (vrh_cvars_registered)
		return;
	vrh_cvars_registered = true;
	Cvar_RegisterVariable(&vr_worldscale);
	Cvar_RegisterVariable(&vr_weaponscale);
	Cvar_RegisterVariable(&vr_weaponpitchadjust);
	Cvar_RegisterVariable(&vr_weaponyawadjust);
	Cvar_RegisterVariable(&vr_yawmode);
	Cvar_RegisterVariable(&cl_comfort);
	Cvar_RegisterVariable(&vr_turnspeed);
	Cvar_RegisterVariable(&cl_walkdirection);
	Cvar_RegisterVariable(&cl_righthanded);
	Cvar_RegisterVariable(&vr_6dof);
	Cvar_RegisterVariable(&vr_eyeseparation);
	Cvar_RegisterVariable(&vr_hudscale);
	Cvar_RegisterVariable(&vr_hudstereo);
	Cvar_RegisterVariable(&vr_screen_distance);
	Cvar_RegisterVariable(&vr_screen_curved);
	Cvar_RegisterVariable(&vr_refreshrate);
	Cvar_RegisterVariable(&vr_msaa);
	Cvar_RegisterVariable(&vr_supersampling);
	Cvar_RegisterVariable(&vr_viewkick);
	Cvar_RegisterVariable(&vr_haptics);
	Cvar_RegisterVariable(&vr_thumbstick_deadzone);
	Cvar_RegisterVariable(&vr_menu_pointer_scale);
	R_LaserSights_Init();
}

void VRH_Init(void)
{
	if (vrh_initialized)
		return;
	VRH_RegisterCvars();
	vrh_initialized = true;

	if (Sys_CheckParm("-novr"))
	{
		VRH_Log("-novr: OpenXR disabled");
		return;
	}
	if (cls.state == ca_dedicated)
		return;

	// JavaVM / activity from SDL
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	ovrJava java;
	memset(&java, 0, sizeof(java));
	java.Env = env;
	java.ActivityObject = activity;
	(*env)->GetJavaVM(env, &java.Vm);

	VRH_InitGLDebug();
	VRH_InitPlatformFlags();
	VR_Init(&java, "Xonotic", 1);

	// session bound to SDL's current EGL context
	engine_t *engine = VR_GetEngine();
	VR_EnterVR(engine);
	IN_VRInit(engine);

	VR_SetConfig(VR_CONFIG_VIEWPORT_MSAA, bound(1, vr_msaa.integer, 4));
	VR_SetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING, bound(0.5f, vr_supersampling.value, 2.0f));
	VR_SetConfigFloat(VR_CONFIG_CANVAS_DISTANCE, vr_screen_distance.value);
	VR_InitRenderer(engine, false);
	if (vr_refreshrate.integer > 0)
		VR_SetRefreshRate(vr_refreshrate.integer);

	vrh_session = true;
	vrh_needcalibrate = true;
	VRH_Log("OpenXR session created, refresh %i Hz", VR_GetRefreshRate());
}

void VRH_Shutdown(void)
{
	if (!vrh_session)
		return;
	engine_t *engine = VR_GetEngine();
	VR_DestroyRenderer(engine);
	VR_LeaveVR(engine);
	VR_Destroy(engine);
	vrh_session = false;
}

bool VRH_Available(void)
{
	return vrh_session;
}

bool VRH_InGame(void)
{
	return cls.state == ca_connected && cls.signon == SIGNONS && key_dest == key_game;
}

bool VRH_ScreenMode(void)
{
	return vrh_screenmode;
}

/* ---------------------------------------------------------------- per frame */

static void VRH_UpdatePoses(void)
{
	engine_t *engine = VR_GetEngine();
	XrPosef head = VR_GetHeadPose();
	float ws = vr_worldscale.value;
	int eye;

	// head orientation -> Quake angles
	XrVector3f a = XrQuaternionf_ToEulerAngles(head.orientation);
	vrh_hmdangles[PITCH] = a.x;
	vrh_hmdangles[YAW] = a.y;
	vrh_hmdangles[ROLL] = a.z;

	vrh_hmdpos_xr[0] = head.position.x;
	vrh_hmdpos_xr[1] = head.position.y;
	vrh_hmdpos_xr[2] = head.position.z;
	if (vrh_needcalibrate && VR_HeadTracked() && head.position.y > 0.3f)
	{
		VectorCopy(vrh_hmdpos_xr, vrh_recenter_xr);
		vrh_playerheight = head.position.y;
		vrh_needcalibrate = false;
		VRH_Log("calibrated: head height %.2f m", vrh_playerheight);
	}

	// per-eye offset in head-local space, then to Quake axes
	for (eye = 0; eye < 2; eye++)
	{
		XrPosef view = VR_GetView(eye);
		XrVector3f d = { view.position.x - head.position.x, view.position.y - head.position.y, view.position.z - head.position.z };
		XrVector3f local = XrQuaternionf_Rotate(XrQuaternionf_Inverse(head.orientation), d);
		float l[3] = { local.x, local.y, local.z };
		XrToQuake(l, vrh_eyeoffset[eye], ws);
		// eye orientation relative to the head (Quest 3 views are canted outwards)
		{
			XrQuaternionf rel = XrQuaternionf_Multiply(XrQuaternionf_Inverse(head.orientation), view.orientation);
			XrVector3f ea = XrQuaternionf_ToEulerAngles(rel);
			vrh_eyeangles[eye][PITCH] = ea.x;
			vrh_eyeangles[eye][YAW] = ea.y;
			vrh_eyeangles[eye][ROLL] = ea.z;
		}
	}
	if (!vrh_eyeangles_logged && VR_HeadTracked())
	{
		vrh_eyeangles_logged = true;
		VRH_Log("eye offsets L(%.2f %.2f %.2f) R(%.2f %.2f %.2f) units, eye cant yaw L %.2f R %.2f deg",
			vrh_eyeoffset[0][0], vrh_eyeoffset[0][1], vrh_eyeoffset[0][2], vrh_eyeoffset[1][0], vrh_eyeoffset[1][1], vrh_eyeoffset[1][2],
			vrh_eyeangles[0][YAW], vrh_eyeangles[1][YAW]);
	}

	// weapon hand
	int hand = cl_righthanded.integer ? 1 : 0;
	vrh_gunvalid = IN_VRIsActive(hand);
	XrPosef aim = IN_VRGetPose(hand);
	{
		XrVector3f g = XrQuaternionf_ToQuakeAngles(aim.orientation, vr_weaponpitchadjust.value, vr_weaponyawadjust.value, 0);
		vr_gunangles[PITCH] = g.x;
		vr_gunangles[YAW] = g.y + vrh_yawoffset;
		vr_gunangles[ROLL] = g.z;
		float rel_xr[3] = { aim.position.x - head.position.x, aim.position.y - head.position.y, aim.position.z - head.position.z };
		XrToQuake(rel_xr, vrh_gunrel, ws);
		RotateYaw(vrh_gunrel, vrh_yawoffset);
	}
	(void)engine;
}

bool VRH_FrameSetup(void)
{
	if (!vrh_session)
		return false;
	engine_t *engine = VR_GetEngine();

	if (!VR_InitFrame(engine))
	{
		vrh_frame_active = false;
		return false;
	}
	vrh_frame_active = true;

	if (VR_DidRecenter())
		vrh_needcalibrate = true;

	VRH_UpdatePoses();

	// flat screen for anything that is not the 3D game view
	vrh_screenmode = cls.state != ca_connected || cls.signon != SIGNONS || key_dest == key_menu || scr_loading || (key_consoleactive & KEY_CONSOLEACTIVE_USER) != 0;
	VR_SetConfig(VR_CONFIG_MODE, vrh_screenmode ? VR_MODE_MONO_SCREEN : VR_MODE_STEREO_6DOF);
	VR_SetConfigFloat(VR_CONFIG_CANVAS_DISTANCE, vr_screen_distance.value);

	VRH_HandleInput();

	return true;
}

void VRH_GetEyeResolution(int *width, int *height)
{
	if (vrh_screenmode)
	{
		*width = VR_GetConfig(VR_CONFIG_SCREEN_WIDTH);
		*height = VR_GetConfig(VR_CONFIG_SCREEN_HEIGHT);
	}
	else
	{
		*width = VR_GetConfig(VR_CONFIG_VIEWPORT_WIDTH);
		*height = VR_GetConfig(VR_CONFIG_VIEWPORT_HEIGHT);
	}
}

bool VRH_GetHudRect(int *x, int *y, int *w, int *h)
{
	float scale, shift, tl, tr, tu, td, cxfrac, cyfrac_top;
	XrFovf fov;
	if (!vrh_session || vrh_screenmode)
		return false;
	scale = bound(0.2f, vr_hudscale.value, 1.0f);
	shift = bound(-0.1f, vr_hudstereo.value, 0.1f) * vid.mode.width;
	*w = (int)(vid.mode.width * scale);
	*h = (int)(vid.mode.height * scale);
	// Each eye's fov is asymmetric, so the same pixel points in different directions per eye.
	// Centre the canvas on this eye's straight-ahead direction so the HUD fuses between the eyes.
	fov = VR_GetFov(bound(0, r_stereo_side, 1));
	tl = tanf(fov.angleLeft); tr = tanf(fov.angleRight);
	td = tanf(fov.angleDown); tu = tanf(fov.angleUp);
	cxfrac = (0.0f - tl) / (tr - tl);          /* fraction from the left edge */
	cyfrac_top = tu / (tu - td);               /* fraction from the top edge */
	// converge slightly on top: left eye's canvas shifted right, right eye's shifted left
	*x = (int)(cxfrac * vid.mode.width) - *w / 2 + (int)(r_stereo_side == 0 ? shift : -shift);
	*y = (int)(cyfrac_top * vid.mode.height) - *h / 2;
	return true;
}

void VRH_BeginEye(int eye)
{
	engine_t *engine = VR_GetEngine();
	int w, h;
	VR_BeginFrame(engine, eye);
	// make the eye swapchain image the engine's "screen"
	GL_SetDefaultFramebuffer(VR_GetEyeFBO(engine, eye));
	VRH_GetEyeResolution(&w, &h);
	vid.mode.width = w;
	vid.mode.height = h;
	r_stereo_side = eye;
}

void VRH_EndEye(int eye)
{
	VR_EndFrame(VR_GetEngine(), eye);
}

void VRH_SubmitFrame(void)
{
	if (!vrh_frame_active)
		return;
	VR_FinishFrame(VR_GetEngine());
	GL_SetDefaultFramebuffer(0);
	vrh_frame_active = false;
}

/* ---------------------------------------------------------------- view */

void VRH_GetEyeOffset(int eye, float out[3], float out_angles[3])
{
	eye = bound(0, eye, 1);
	VectorScale(vrh_eyeoffset[eye], vr_eyeseparation.value, out);
	VectorCopy(vrh_eyeangles[eye], out_angles);
}

void VRH_GetProjection(int eye, float znear, float zfar, float m16[16])
{
	XrFovf fov = VR_GetFov(bound(0, eye, 1));
	float l = tanf(fov.angleLeft), r = tanf(fov.angleRight), d = tanf(fov.angleDown), u = tanf(fov.angleUp);
	// sniper zoom: render a narrower frustum into the same eye fov -> magnification
	float zoom = (cl.viewzoom > 0.01f && cl.viewzoom < 1.0f) ? cl.viewzoom : 1.0f;
	l *= zoom; r *= zoom; u *= zoom; d *= zoom;
	(void)znear; (void)zfar;
	// asymmetric frustum: only the x/y scale and offset terms differ from DarkPlaces' symmetric matrix
	m16[0] = 2.0f / (r - l);
	m16[5] = 2.0f / (u - d);
	m16[8] = (r + l) / (r - l);
	m16[9] = (u + d) / (u - d);
	{
		static int logged[2];
		if (logged[bound(0, eye, 1)]++ < 2)
			VRH_Log("projection eye %i: fov L %.1f R %.1f U %.1f D %.1f deg -> m0 %.3f m5 %.3f m8 %.3f m9 %.3f (near %.2f far %.0f)",
				eye, fov.angleLeft * 180 / M_PI, fov.angleRight * 180 / M_PI, fov.angleUp * 180 / M_PI, fov.angleDown * 180 / M_PI,
				m16[0], m16[5], m16[8], m16[9], znear, zfar);
	}
}

void VRH_GetUnionFovTangents(float *tanx, float *tany)
{
	float mx = 0, my = 0;
	int eye;
	for (eye = 0; eye < 2; eye++)
	{
		XrFovf fov = VR_GetFov(eye);
		mx = max(mx, max(fabsf(tanf(fov.angleLeft)), fabsf(tanf(fov.angleRight))));
		my = max(my, max(fabsf(tanf(fov.angleDown)), fabsf(tanf(fov.angleUp))));
	}
	if (mx < 0.1f) mx = 1.0f;
	if (my < 0.1f) my = 1.0f;
	*tanx = mx;
	*tany = my;
}

void VRH_GetHMDAngles(float out[3])
{
	out[PITCH] = vrh_hmdangles[PITCH];
	out[YAW] = vrh_hmdangles[YAW] + vrh_yawoffset;
	out[ROLL] = vrh_hmdangles[ROLL];
}

void VRH_GetHMDPosition(float out[3])
{
	float d[3];
	if (!vr_6dof.integer)
	{
		VectorClear(out);
		return;
	}
	d[0] = vrh_hmdpos_xr[0] - vrh_recenter_xr[0];
	d[1] = vrh_hmdpos_xr[1] - vrh_playerheight;
	d[2] = vrh_hmdpos_xr[2] - vrh_recenter_xr[2];
	XrToQuake(d, out, vr_worldscale.value);
	RotateYaw(out, vrh_yawoffset);
}

float VRH_GetPlayerYawOffset(void)
{
	return vrh_yawoffset;
}

void VRH_AddYaw(float degrees)
{
	vrh_yawoffset = ANGLEMOD(vrh_yawoffset + degrees);
}

void VRH_ServerSetAngles(const float angles[3])
{
	if (!vrh_session)
		return;
	// make (hmd yaw + offset) equal the server's requested yaw; pitch/roll stay with the headset
	vrh_yawoffset = ANGLEMOD(angles[YAW] - vrh_hmdangles[YAW]);
}

bool VRH_HasGun(void)
{
	return vrh_session && vrh_gunvalid;
}

float VRH_GetWeaponScale(void)
{
	return vr_weaponscale.value > 0 ? vr_weaponscale.value : 1.0f;
}

/* vieworg must already include the head position offset (VRH_GetHMDPosition) */
bool VRH_GetGun(const float vieworg[3], float out_org[3], float out_angles[3])
{
	if (!vrh_gunvalid)
		return false;
	VectorAdd(vieworg, vrh_gunrel, out_org);
	VectorCopy(vr_gunangles, out_angles);
	VectorCopy(out_org, vr_gunorg);
	return true;
}

/* ---------------------------------------------------------------- misc */

void VRH_Recenter(void)
{
	VR_SetConfig(VR_CONFIG_NEED_RECENTER, true);
	vrh_needcalibrate = true;
	vrh_yawoffset = 0;
}

float VRH_GetRefreshRate(void)
{
	return vrh_session ? (float)VR_GetRefreshRate() : 72.0f;
}

void VRH_Vibrate(int hand, float duration_ms, float intensity)
{
	if (!vrh_session || !vr_haptics.integer)
		return;
	IN_VR_Vibrate(duration_ms * 0.001f, hand, intensity);
}

/* ---------------------------------------------------------------- callbacks (VR -> engine) */

void VRH_KeyEvent(int key, int ascii, bool down)
{
	Key_Event(key, ascii, down);
}

void VRH_Command(const char *cmd)
{
	Cbuf_AddText(cmd_local, cmd);
}

float VRH_CvarValue(const char *name)
{
	return Cvar_VariableValue(&cvars_all, name, ~0);
}

/* ---------------------------------------------------------------- input */

static void VRH_Button(int hand, uint32_t buttons, uint32_t mask, int key)
{
	bool now = (buttons & mask) != 0;
	bool was = (vrh_buttons_prev[hand] & mask) != 0;
	if (now != was)
		Key_Event(key, 0, now);
}

/* ---- grid text input (ported from QuakeQuest) ---- */
static bool vrh_textinput = false;
static int vrh_kb_shift = 0, vrh_kb_lgrid = 0, vrh_kb_rgrid = 0;
static const char kb_left_lower[3][10] = {"bcfihgdae", "klorqpmjn", "tuwzyxvs "};
static const char kb_left_shift[3][10] = {"BCFIHGDAE", "KLORQPMJN", "TUWZYXVS "};
static const char kb_right_lower[3][10] = {"236987415", "+-)]&[(?0", "         "};
static const char kb_right_shift[3][10] = {"\"*:|._~/#", "%^}>,<{\\@", "         "};
static const char *kb_left_map[2][3][3] = {
	{{"a  b  c", "j  k  l", "s  t  u"}, {"d  e  f", "m  n  o", "v     w"}, {"g  h  i", "p  q  r", "x  y  z"}},
	{{"A  B  C", "J  K  L", "S  T  U"}, {"D  E  F", "M  N  O", "V     W"}, {"G  H  I", "P  Q  R", "X  Y  Z"}},
};
static const char *kb_right_map[2][3][3] = {
	{{"1  2  3", "?  +  -", "       "}, {"4  5  6", "(  0  )", "       "}, {"7  8  9", "[  &  ]", "       "}},
	{{"/  \"  *", "\\  %  ^", "       "}, {"~  #  :", "{  @  }", "       "}, {"_  .  |", "<  ,  >", "       "}},
};

static int VRH_KeyboardCell(float x, float y)
{
	int c = 8;
	if (x < -0.3f || x > 0.3f || y < -0.3f || y > 0.3f)
	{
		if (x == 0.0f)
			c = (y > 0.0f) ? 0 : 4;
		else
		{
			float angle = atanf(y / x) / ((float)M_PI / 180.0f);
			if (x > 0.0f)
				c = (int)(((90.0f - angle) + 22.5f) / 45.0f);
			else
			{
				c = (int)(((90.0f - angle) + 22.5f) / 45.0f) + 4;
				if (c == 8)
					c = 0;
			}
		}
	}
	return c;
}

static void VRH_KeyboardButton(int hand, uint32_t buttons, uint32_t mask, int key, int ascii)
{
	bool now = (buttons & mask) != 0;
	bool was = (vrh_buttons_prev[hand] & mask) != 0;
	if (now != was)
		Key_Event(key, ascii, now);
}

static void VRH_HandleTextInput(uint32_t b0, uint32_t b1)
{
	XrVector2f sl = IN_VRGetJoystickState(0), sr = IN_VRGetJoystickState(1);
	int li = VRH_KeyboardCell(sl.x, sl.y);
	int ri = VRH_KeyboardCell(sr.x, sr.y);
	char lc, rc;
	char buffer[512];

	if ((b0 & ovrButton_Y) && !(vrh_buttons_prev[0] & ovrButton_Y))
	{
		vrh_textinput = false;
		SCR_CenterPrint("Text input: off");
		return;
	}
	if ((b0 & ovrButton_X) && !(vrh_buttons_prev[0] & ovrButton_X))
		vrh_kb_shift = 1 - vrh_kb_shift;
	if ((b0 & ovrButton_GripTrigger) && !(vrh_buttons_prev[0] & ovrButton_GripTrigger))
		vrh_kb_lgrid = (vrh_kb_lgrid + 1) % 3;
	if ((b1 & ovrButton_GripTrigger) && !(vrh_buttons_prev[1] & ovrButton_GripTrigger))
		vrh_kb_rgrid = (vrh_kb_rgrid + 1) % 3;

	lc = vrh_kb_shift ? kb_left_shift[vrh_kb_lgrid][li] : kb_left_lower[vrh_kb_lgrid][li];
	rc = vrh_kb_shift ? kb_right_shift[vrh_kb_rgrid][ri] : kb_right_lower[vrh_kb_rgrid][ri];

	VRH_KeyboardButton(1, b1, ovrButton_A, K_ENTER, 0);
	VRH_KeyboardButton(1, b1, ovrButton_B, K_BACKSPACE, 0);
	VRH_KeyboardButton(0, b0, ovrButton_Trigger, lc, lc);
	VRH_KeyboardButton(1, b1, ovrButton_Trigger, rc, rc);
	VRH_KeyboardButton(0, b0, ovrButton_Enter, K_ESCAPE, 0);

	dpsnprintf(buffer, sizeof(buffer),
		" %s       %s\n %s       %s\n %s       %s\n\nText input:   %c    %c\n(Y exit, X shift, grips cycle, triggers type, A enter, B delete)",
		kb_left_map[vrh_kb_shift][0][vrh_kb_lgrid], kb_right_map[vrh_kb_shift][0][vrh_kb_rgrid],
		kb_left_map[vrh_kb_shift][1][vrh_kb_lgrid], kb_right_map[vrh_kb_shift][1][vrh_kb_rgrid],
		kb_left_map[vrh_kb_shift][2][vrh_kb_lgrid], kb_right_map[vrh_kb_shift][2][vrh_kb_rgrid],
		lc, rc);
	SCR_CenterPrint(buffer);
}

/* current thumbstick input; consumed by IN_Move via VRH_GetMove */
static float vrh_move[2];

bool VRH_GetMove(float *forward, float *side)
{
	if (!vrh_session)
		return false;
	*forward = vrh_move[1];
	*side = vrh_move[0];
	return true;
}

void VRH_HandleInput(void)
{
	int hand = cl_righthanded.integer ? 1 : 0;       /* weapon hand */
	int offhand = 1 - hand;
	uint32_t b[2] = { IN_VRGetButtonState(0), IN_VRGetButtonState(1) };
	XrVector2f stickL = IN_VRGetJoystickState(0);
	XrVector2f stickR = IN_VRGetJoystickState(1);
	XrVector2f moveStick = cl_righthanded.integer ? stickL : stickR;
	XrVector2f turnStick = cl_righthanded.integer ? stickR : stickL;
	float dz = bound(0.0f, vr_thumbstick_deadzone.value, 0.9f);
	bool ingame = VRH_InGame() && !vrh_screenmode;

	// ----- grid text input consumes everything while active
	if (vrh_textinput)
	{
		vrh_move[0] = vrh_move[1] = 0;
		VRH_HandleTextInput(b[0], b[1]);
		vrh_buttons_prev[0] = b[0];
		vrh_buttons_prev[1] = b[1];
		return;
	}

	// ----- movement: off-hand stick, relative to the head (or off-hand controller) and re-expressed
	// in the frame of the aim yaw, because the server moves the player relative to cmd.viewangles
	vrh_move[0] = vrh_move[1] = 0;
	if (ingame)
	{
		float x = moveStick.x, y = moveStick.y;
		float len = sqrtf(x * x + y * y);
		if (len > dz)
		{
			float scale = (len - dz) / (1.0f - dz) / len;
			x *= scale; y *= scale;
			float refyaw;
			if (cl_walkdirection.integer)
			{
				XrPosef p = IN_VRGetPose(offhand);
				refyaw = XrQuaternionf_ToQuakeAngles(p.orientation, 0, 0, 0).y + vrh_yawoffset;
			}
			else
				refyaw = vrh_hmdangles[YAW] + vrh_yawoffset;
			float delta = (refyaw - vr_gunangles[YAW]) * (float)M_PI / 180.0f;
			float c = cosf(delta), s = sinf(delta);
			// (forward=y, right=x) rotated by delta about up: Quake's sidemove is positive to the right
			vrh_move[1] = y * c - x * s;   /* forward */
			vrh_move[0] = x * c + y * s;   /* side */
		}
	}

	// ----- turning: aim-hand stick left/right
	{
		static bool snapped = false;
		float x = turnStick.x;
		if (vr_yawmode.integer == 0)
		{
			if (fabsf(x) > 0.6f)
			{
				if (!snapped)
				{
					VRH_AddYaw(x > 0 ? -cl_comfort.value : cl_comfort.value);
					snapped = true;
				}
			}
			else if (fabsf(x) < 0.3f)
				snapped = false;
		}
		else if (fabsf(x) > dz)
		{
			float v = (fabsf(x) - dz) / (1.0f - dz) * (x > 0 ? -1.0f : 1.0f);
			VRH_AddYaw(v * vr_turnspeed.value * cl.realframetime);
		}
	}

	// ----- buttons (edge triggered -> engine keys, bound in vr.cfg / default binds)
	if (ingame)
	{
		VRH_Button(hand, b[hand], ovrButton_Trigger, K_MOUSE1);          /* +fire */
		if ((b[hand] & ovrButton_Trigger) && !(vrh_buttons_prev[hand] & ovrButton_Trigger))
			VRH_Vibrate(hand, 80, 0.4f);                                 /* fire feedback */
		VRH_Button(hand, b[hand], ovrButton_GripTrigger, K_MOUSE2);      /* +fire2 */
		VRH_Button(offhand, b[offhand], ovrButton_Trigger, K_SPACE);     /* +jump */
		VRH_Button(offhand, b[offhand], ovrButton_GripTrigger, K_MOUSE4);/* +hook */
		VRH_Button(1, b[1], ovrButton_A, K_MOUSE3);                      /* +zoom */
		VRH_Button(1, b[1], ovrButton_B, K_CTRL);                        /* +crouch */
		VRH_Button(0, b[0], ovrButton_X, K_JOY1);                        /* bindable (default: +use) */
		VRH_Button(0, b[0], ovrButton_Y, K_TAB);                         /* scoreboard */
		VRH_Button(offhand, b[offhand], ovrButton_Joystick, K_JOY2);     /* bindable (default: toggle laser) */
		// weapon switching on the aim-hand stick up/down
		VRH_Button(hand, b[hand], ovrButton_Up, K_MWHEELUP);
		VRH_Button(hand, b[hand], ovrButton_Down, K_MWHEELDOWN);
	}
	else
	{
		// menus / console: sticks navigate, A confirms, B backs out, triggers click
		VRH_Button(0, b[0], ovrButton_Up, K_UPARROW);
		VRH_Button(0, b[0], ovrButton_Down, K_DOWNARROW);
		VRH_Button(0, b[0], ovrButton_Left, K_LEFTARROW);
		VRH_Button(0, b[0], ovrButton_Right, K_RIGHTARROW);
		VRH_Button(1, b[1], ovrButton_Up, K_UPARROW);
		VRH_Button(1, b[1], ovrButton_Down, K_DOWNARROW);
		VRH_Button(1, b[1], ovrButton_Left, K_LEFTARROW);
		VRH_Button(1, b[1], ovrButton_Right, K_RIGHTARROW);
		VRH_Button(1, b[1], ovrButton_A, K_ENTER);
		VRH_Button(1, b[1], ovrButton_B, K_ESCAPE);
		VRH_Button(0, b[0], ovrButton_Trigger, K_MOUSE1);
		VRH_Button(1, b[1], ovrButton_Trigger, K_MOUSE1);
		// Y opens the grid keyboard (for chat, console, menu text fields)
		if ((b[0] & ovrButton_Y) && !(vrh_buttons_prev[0] & ovrButton_Y))
		{
			vrh_textinput = true;
			SCR_CenterPrint("Text input: on");
		}
	}
	// menu button (left controller) toggles the menu in both modes
	VRH_Button(0, b[0], ovrButton_Enter, K_ESCAPE);

	// recenter: click the right thumbstick (or both grips + menu)
	if ((b[1] & ovrButton_Joystick) && !(vrh_buttons_prev[1] & ovrButton_Joystick))
		VRH_Recenter();
	if ((b[0] & ovrButton_GripTrigger) && (b[1] & ovrButton_GripTrigger) && (b[0] & ovrButton_Enter) && !(vrh_buttons_prev[0] & ovrButton_Enter))
		VRH_Recenter();

	// ----- laser pointer -> mouse cursor on the flat screen
	if (vrh_screenmode)
	{
		XrPosef aim = IN_VRGetPose(hand);
		XrVector3f a = XrQuaternionf_ToEulerAngles(aim.orientation);
		float menuyaw = VR_GetConfigFloat(VR_CONFIG_MENU_YAW);
		float dyaw = a.y - menuyaw;
		while (dyaw > 180) dyaw -= 360;
		while (dyaw < -180) dyaw += 360;
		float dpitch = a.x;
		// the screen spans ~69 degrees horizontally (see vr_renderer.c centralAngle)
		float halfx = 34.5f / bound(0.2f, vr_menu_pointer_scale.value, 5.0f);
		float halfy = halfx * (float)vid.mode.height / (float)vid.mode.width;
		float u = 0.5f - dyaw / (2 * halfx);
		float v = 0.5f + dpitch / (2 * halfy);
		int mx = (int)(bound(0.0f, u, 1.0f) * vid.mode.width);
		int my = (int)(bound(0.0f, v, 1.0f) * vid.mode.height);
		VRH_SetCursor(mx, my);
	}

	vrh_buttons_prev[0] = b[0];
	vrh_buttons_prev[1] = b[1];
}

/* cursor position in screen pixels (top-left origin, like DarkPlaces' in_windowmouse) */
static int vrh_cursor_x = -1, vrh_cursor_y = -1;

void VRH_SetCursor(int x, int y)
{
	vrh_cursor_x = x;
	vrh_cursor_y = y;
	// GL / swapchain origin is bottom-left
	VR_SetConfig(VR_CONFIG_MOUSE_SIZE, max(4, vid.mode.height / 90));
	VR_SetConfig(VR_CONFIG_MOUSE_X, x);
	VR_SetConfig(VR_CONFIG_MOUSE_Y, vid.mode.height - y);
}

bool VRH_GetCursor(int *x, int *y)
{
	if (!vrh_session || !vrh_screenmode || vrh_cursor_x < 0)
		return false;
	*x = vrh_cursor_x;
	*y = vrh_cursor_y;
	return true;
}

#endif /* VR_QUEST */
