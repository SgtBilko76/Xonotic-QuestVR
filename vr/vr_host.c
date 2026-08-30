/*
 * vr_host.c - DarkPlaces side of the VR bridge (XonoticQuest).
 *
 * Phase 1 stub: registers the VR cvars and logs; the OpenXR session comes in Phase 2.
 */
#ifdef VR_QUEST

#include "quakedef.h"
#include "vr_api.h"

#include <stdarg.h>
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
cvar_t vr_6dof = {CF_CLIENT | CF_ARCHIVE, "vr_6dof", "1", "apply positional head tracking to the view"};
cvar_t vr_hudscale = {CF_CLIENT | CF_ARCHIVE, "vr_hudscale", "0.55", "size of the 2D HUD canvas relative to the eye buffer"};
cvar_t vr_hudstereo = {CF_CLIENT | CF_ARCHIVE, "vr_hudstereo", "0.006", "per-eye horizontal HUD shift (fraction of width) that sets the HUD depth"};
cvar_t vr_screen_distance = {CF_CLIENT | CF_ARCHIVE, "vr_screen_distance", "5", "distance of the flat menu/console screen in metres"};
cvar_t vr_screen_curved = {CF_CLIENT | CF_ARCHIVE, "vr_screen_curved", "1", "1: cylinder layer for the menu screen, 0: flat quad"};
cvar_t vr_refreshrate = {CF_CLIENT | CF_ARCHIVE, "vr_refreshrate", "90", "requested display refresh rate (Hz)"};
cvar_t vr_msaa = {CF_CLIENT | CF_ARCHIVE, "vr_msaa", "2", "eye buffer MSAA samples"};
cvar_t vr_supersampling = {CF_CLIENT | CF_ARCHIVE, "vr_supersampling", "1.0", "eye buffer resolution multiplier"};
cvar_t vr_viewkick = {CF_CLIENT | CF_ARCHIVE, "vr_viewkick", "0", "scale of damage/weapon kick applied to the headset view (0 = none)"};
cvar_t vr_haptics = {CF_CLIENT | CF_ARCHIVE, "vr_haptics", "1", "controller vibration"};

static bool vrh_initialized = false;

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

void VRH_Init(void)
{
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
	Cvar_RegisterVariable(&vr_hudscale);
	Cvar_RegisterVariable(&vr_hudstereo);
	Cvar_RegisterVariable(&vr_screen_distance);
	Cvar_RegisterVariable(&vr_screen_curved);
	Cvar_RegisterVariable(&vr_refreshrate);
	Cvar_RegisterVariable(&vr_msaa);
	Cvar_RegisterVariable(&vr_supersampling);
	Cvar_RegisterVariable(&vr_viewkick);
	Cvar_RegisterVariable(&vr_haptics);
	vrh_initialized = true;
	VRH_Log("VR host initialised (phase 1: no OpenXR session yet)");
}

void VRH_Shutdown(void)
{
	vrh_initialized = false;
}

bool VRH_Available(void)
{
	return false;
}

bool VRH_InGame(void)
{
	return cls.state == ca_connected && cls.signon == SIGNONS && key_dest == key_game;
}

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

#endif /* VR_QUEST */
