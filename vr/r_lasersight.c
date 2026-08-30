/*
 * r_lasersight.c - laser aiming beam for XonoticQuest, ported from QuakeQuest.
 * Draws a thin beam from the weapon hand along the aim direction to the first hit surface.
 */
#ifdef VR_QUEST

#include "quakedef.h"
#include "cl_collision.h"
#include "vr/vr_api.h"

cvar_t r_lasersight = {CF_CLIENT | CF_ARCHIVE, "r_lasersight", "1", "VR laser sight: 0 off, 1 beam"};
cvar_t r_lasersight_thickness = {CF_CLIENT | CF_ARCHIVE, "r_lasersight_thickness", "0.4", "thickness of the laser sight beam"};
cvar_t r_lasersight_color_red = {CF_CLIENT | CF_ARCHIVE, "r_lasersight_color_red", "0.8", "laser sight red"};
cvar_t r_lasersight_color_green = {CF_CLIENT | CF_ARCHIVE, "r_lasersight_color_green", "0.1", "laser sight green"};
cvar_t r_lasersight_color_blue = {CF_CLIENT | CF_ARCHIVE, "r_lasersight_color_blue", "0", "laser sight blue"};

void R_LaserSights_Init(void)
{
	Cvar_RegisterVariable(&r_lasersight);
	Cvar_RegisterVariable(&r_lasersight_thickness);
	Cvar_RegisterVariable(&r_lasersight_color_red);
	Cvar_RegisterVariable(&r_lasersight_color_green);
	Cvar_RegisterVariable(&r_lasersight_color_blue);
}

static const unsigned short laserelements[36] =
{
	0, 1, 2, 2, 1, 3,
	4, 5, 6, 6, 5, 7,
	0, 2, 4, 4, 2, 6,
	1, 5, 3, 3, 5, 7,
	0, 4, 1, 1, 4, 5,
	2, 3, 6, 6, 3, 7
};

static void R_DrawLaserBeamMesh(const vec3_t start, const vec3_t end, float thickness, float cr, float cg, float cb, float ca)
{
	int i;
	float vertex3f[8*3], color4f[8*4];
	vec3_t dir, side, up;

	// build a thin box along the beam
	VectorSubtract(end, start, dir);
	VectorNormalize(dir);
	VectorVectors(dir, side, up);

	for (i = 0; i < 8; i++)
	{
		const float s = (i & 1) ? 0.5f : -0.5f;
		const float u = (i & 2) ? 0.5f : -0.5f;
		const float *base = (i & 4) ? end : start;
		vertex3f[i*3+0] = base[0] + (side[0]*s + up[0]*u) * thickness;
		vertex3f[i*3+1] = base[1] + (side[1]*s + up[1]*u) * thickness;
		vertex3f[i*3+2] = base[2] + (side[2]*s + up[2]*u) * thickness;
	}
	R_FillColors(color4f, 8, cr, cg, cb, ca);

	RSurf_ActiveModelEntity(r_refdef.scene.worldentity, false, false, false);
	GL_BlendFunc(GL_SRC_ALPHA, GL_ONE);
	GL_DepthMask(false);
	GL_DepthRange(0, 1);
	GL_DepthTest(true);
	GL_CullFace(GL_NONE);
	GL_PolygonOffset(r_refdef.polygonfactor, r_refdef.polygonoffset);
	R_EntityMatrix(&identitymatrix);
	R_Mesh_PrepareVertices_Generic_Arrays(8, vertex3f, color4f, NULL);
	R_Mesh_ResetTextureState();
	R_SetupShader_Generic_NoTexture(false, true);
	R_Mesh_Draw(0, 8, 0, 12, NULL, NULL, 0, laserelements, NULL, 0);
}

void R_DrawLaserSights(void)
{
	vec3_t start, end, muzzle;
	vec3_t forward, right, up;
	trace_t trace;

	if (!r_lasersight.integer || !VRH_Available() || VRH_ScreenMode() || !VRH_HasGun())
		return;
	if (cls.state != ca_connected || cls.signon != SIGNONS)
		return;

	AngleVectors(vr_gunangles, forward, right, up);
	// start slightly in front of the hand so the beam doesn't clip into the weapon model
	VectorMA(vr_gunorg, 8.0f, forward, muzzle);
	VectorMA(vr_gunorg, 8192.0f, forward, end);
	trace = CL_TraceLine(muzzle, end, MOVE_NORMAL, NULL, SUPERCONTENTS_SOLID | SUPERCONTENTS_BODY, 0, 0, collision_extendmovelength.value, true, true, NULL, false, false);
	VectorCopy(trace.endpos, end);
	VectorCopy(muzzle, start);

	R_DrawLaserBeamMesh(start, end, bound(0.05f, r_lasersight_thickness.value, 4.0f),
		r_lasersight_color_red.value, r_lasersight_color_green.value, r_lasersight_color_blue.value, 0.5f);
}

#endif /* VR_QUEST */
