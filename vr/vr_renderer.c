/*
 * vr_renderer.c - frame loop, eye buffers and composition layers (XonoticQuest).
 * Ported from Team Beef's CSVR engine/vr/VrRenderer.c with these changes:
 *  - eye buffers use the runtime's recommended size (no forced square viewport) and the
 *    projection layer submits each eye's real asymmetric fov;
 *  - screen mode composites a 16:9 sub-rect of eye buffer 0 as a cylinder (or quad) layer.
 */
#include "vr_base.h"
#include "vr_input.h"
#include "vr_renderer.h"

#include <malloc.h>
#include <string.h>

static XrView projections[ovrMaxNumEyes];
static XrPosef headPose;
static bool headTracked = false;
static bool menuYawValid = false;   /* flat screen placed in front of the user? */
static bool initialized = false;
static bool recenterCalled = false;
static bool stageBoundsDirty = true;
static bool stageSupported = false;
static int vrConfig[VR_CONFIG_MAX];
static float vrConfigFloat[VR_CONFIG_FLOAT_MAX];
static PFN_xrGetDisplayRefreshRateFB pfnGetDisplayRefreshRate = NULL;
static PFN_xrRequestDisplayRefreshRateFB pfnRequestDisplayRefreshRate = NULL;

static void VR_UpdateStageBounds(ovrApp* pappState) {
	XrExtent2Df stageBounds;
	XrResult result;
	memset(&stageBounds, 0, sizeof(stageBounds));
	OXR(result = xrGetReferenceSpaceBoundsRect(pappState->Session, XR_REFERENCE_SPACE_TYPE_STAGE, &stageBounds));
	if (result != XR_SUCCESS || pappState->StageSpace == XR_NULL_HANDLE) {
		pappState->CurrentSpace = pappState->FakeStageSpace;
	}
	if (pappState->CurrentSpace == XR_NULL_HANDLE)
		pappState->CurrentSpace = pappState->FakeStageSpace;
}

void VR_GetResolution(engine_t* engine, int *pWidth, int *pHeight) {
	static int width = 0;
	static int height = 0;

	if (engine) {
		uint32_t viewCount = 0;
		XrViewConfigurationView elements[ovrMaxNumEyes];
		for (uint32_t e = 0; e < ovrMaxNumEyes; e++) {
			memset(&elements[e], 0, sizeof(elements[e]));
			elements[e].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		}
		OXR(xrEnumerateViewConfigurationViews(engine->appState.Instance, engine->appState.SystemId,
				XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, ovrMaxNumEyes, &viewCount, elements));
		for (uint32_t e = 0; e < viewCount && e < ovrMaxNumEyes; e++)
			engine->appState.ViewConfigurationView[e] = elements[e];

		width = engine->appState.ViewConfigurationView[0].recommendedImageRectWidth;
		height = engine->appState.ViewConfigurationView[0].recommendedImageRectHeight;
		// the runtime hands out whatever debug.oculus.textureWidth/Height says (Quest Games Optimizer
		// sets e.g. 2800x2933 on Quest 3; bare default is 1680x1760). Clamp to the panel-native baseline
		// so resolution stays under vr_supersampling's control and means the same thing with or without QGO
		if (width > 1680) {
			height = (int)((long long)height * 1680 / width);
			width = 1680;
		}
		ALOGV("Recommended eye buffer: %ix%i (max %ix%i)", width, height,
				engine->appState.ViewConfigurationView[0].maxImageRectWidth,
				engine->appState.ViewConfigurationView[0].maxImageRectHeight);
	}
	*pWidth = width;
	*pHeight = height;

	float supersampling = VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING);
	if (supersampling > 0) {
		*pWidth = (int)(*pWidth * supersampling);
		*pHeight = (int)(*pHeight * supersampling);
	}
	// cap the eye buffer: the runtime may recommend very large sizes on Quest 3, which the
	// DarkPlaces GLES path cannot fill at 72-90 Hz
	{
		const int maxWidth = 2048;
		if (*pWidth > maxWidth) {
			*pHeight = (int)((long long)*pHeight * maxWidth / *pWidth);
			*pWidth = maxWidth;
		}
	}
	// QCOM tiled foveation wants tile-aligned buffers; unaligned sizes cause visible artifacts
	*pWidth &= ~31;
	*pHeight &= ~31;
	// keep dimensions even
	*pWidth &= ~1;
	*pHeight &= ~1;
	VR_SetConfig(VR_CONFIG_VIEWPORT_WIDTH, *pWidth);
	VR_SetConfig(VR_CONFIG_VIEWPORT_HEIGHT, *pHeight);

	// flat screen sub-rect: full width, 16:9
	int sw = *pWidth;
	int sh = sw * 9 / 16;
	if (sh > *pHeight) { sh = *pHeight; sw = sh * 16 / 9; }
	VR_SetConfig(VR_CONFIG_SCREEN_WIDTH, sw & ~1);
	VR_SetConfig(VR_CONFIG_SCREEN_HEIGHT, sh & ~1);
}

void VR_Recenter(engine_t* engine) {
	XrReferenceSpaceCreateInfo spaceCreateInfo;
	XrSpace newFake = XR_NULL_HANDLE, newStage = XR_NULL_HANDLE;
	XrResult r;
	float recenterYaw;

	memset(&spaceCreateInfo, 0, sizeof(spaceCreateInfo));
	spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceCreateInfo.poseInReferenceSpace = XrPosef_Identity();

	// Accumulate the current head yaw into the recenter yaw - only with a usable frame time and space
	// (the runtime sends reference-space events before the first frame; xrLocateSpace fails then).
	if (engine->appState.CurrentSpace != XR_NULL_HANDLE && engine->predictedDisplayTime > 0 && engine->appState.SessionActive) {
		XrSpaceLocation loc;
		memset(&loc, 0, sizeof(loc));
		loc.type = XR_TYPE_SPACE_LOCATION;
		r = xrLocateSpace(engine->appState.HeadSpace, engine->appState.CurrentSpace, engine->predictedDisplayTime, &loc);
		if (XR_SUCCEEDED(r) && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
			XrVector3f hmdangles = XrQuaternionf_ToEulerAngles(loc.pose.orientation);
			if (hmdangles.y == hmdangles.y) /* not NaN */
				VR_SetConfigFloat(VR_CONFIG_RECENTER_YAW, VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW) + hmdangles.y);
		}
	}
	recenterYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW));
	if (recenterYaw != recenterYaw) { recenterYaw = 0; VR_SetConfigFloat(VR_CONFIG_RECENTER_YAW, 0); }
	spaceCreateInfo.poseInReferenceSpace.orientation.x = 0;
	spaceCreateInfo.poseInReferenceSpace.orientation.y = sinf(recenterYaw / 2);
	spaceCreateInfo.poseInReferenceSpace.orientation.z = 0;
	spaceCreateInfo.poseInReferenceSpace.orientation.w = cosf(recenterYaw / 2);

	// LOCAL space with a floor offset: always available
	spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) {
		spaceCreateInfo.poseInReferenceSpace.position.y = -1.6750f;
	}
	OXR(r = xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &newFake));
	if (XR_FAILED(r) || newFake == XR_NULL_HANDLE) {
		// retry with an identity pose; keep the previous spaces if even that fails
		spaceCreateInfo.poseInReferenceSpace = XrPosef_Identity();
		if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR))
			spaceCreateInfo.poseInReferenceSpace.position.y = -1.6750f;
		OXR(r = xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &newFake));
		if (XR_FAILED(r) || newFake == XR_NULL_HANDLE) {
			ALOGE("VR_Recenter: could not create a reference space, keeping the old one");
			return;
		}
	}

	// STAGE space (floor level, boundary origin) when the runtime has one
	if (stageSupported) {
		spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		spaceCreateInfo.poseInReferenceSpace.position.y = 0.0;
		r = xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &newStage);
		if (XR_FAILED(r)) {
			ALOGE("STAGE reference space unavailable (%d), using LOCAL space with floor offset", r);
			newStage = XR_NULL_HANDLE;
		}
	}

	// swap in the new spaces
	if (engine->appState.StageSpace != XR_NULL_HANDLE)
		xrDestroySpace(engine->appState.StageSpace);
	if (engine->appState.FakeStageSpace != XR_NULL_HANDLE)
		xrDestroySpace(engine->appState.FakeStageSpace);
	engine->appState.FakeStageSpace = newFake;
	engine->appState.StageSpace = newStage;
	engine->appState.CurrentSpace = (newStage != XR_NULL_HANDLE && VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) ? newStage : newFake;
	ALOGV("VR_Recenter: CurrentSpace=%p (stage=%p fake=%p)", (void*)engine->appState.CurrentSpace, (void*)engine->appState.StageSpace, (void*)engine->appState.FakeStageSpace);

	// Update menu orientation: re-place the flat screen on the next frame
	menuYawValid = false;
	stageBoundsDirty = true;
	recenterCalled = true;
}

bool VR_DidRecenter(void) {
	bool output = recenterCalled;
	recenterCalled = false;
	return output;
}

void VR_InitRenderer( engine_t* engine, bool multiview ) {
	if (initialized) {
		VR_DestroyRenderer(engine);
	}

	int eyeW, eyeH;
	VR_GetResolution(engine, &eyeW, &eyeH);

	// Get the viewport configuration info for the chosen viewport configuration type.
	engine->appState.ViewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
	OXR(xrGetViewConfigurationProperties(engine->appState.Instance, engine->appState.SystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &engine->appState.ViewportConfig));

	uint32_t numOutputSpaces = 0;
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, 0, &numOutputSpaces, NULL));
	XrReferenceSpaceType* referenceSpaces = (XrReferenceSpaceType*)malloc(numOutputSpaces * sizeof(XrReferenceSpaceType));
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, numOutputSpaces, &numOutputSpaces, referenceSpaces));

	for (uint32_t i = 0; i < numOutputSpaces; i++) {
		if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
			stageSupported = true;
			break;
		}
	}

	free(referenceSpaces);

	if (engine->appState.CurrentSpace == XR_NULL_HANDLE) {
		VR_Recenter(engine);
	}

	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		memset(&projections[eye], 0, sizeof(XrView));
		projections[eye].type = XR_TYPE_VIEW;
	}
	headPose = XrPosef_Identity();

	int msaa = VR_GetConfig(VR_CONFIG_VIEWPORT_MSAA);
	ovrRenderer_Create(engine->appState.Session, &engine->appState.Renderer, multiview, eyeW, eyeH, msaa > 0 ? msaa : 1);

	// fixed foveated rendering: render the lens periphery at reduced density
	if (VR_HasFoveationExt() && VR_GetConfig(VR_CONFIG_FOVEATION_LEVEL) > 0) {
		PFN_xrCreateFoveationProfileFB pfnCreateFoveationProfile = NULL;
		PFN_xrDestroyFoveationProfileFB pfnDestroyFoveationProfile = NULL;
		PFN_xrUpdateSwapchainFB pfnUpdateSwapchain = NULL;
		OXR(xrGetInstanceProcAddr(engine->appState.Instance, "xrCreateFoveationProfileFB", (PFN_xrVoidFunction*)(&pfnCreateFoveationProfile)));
		OXR(xrGetInstanceProcAddr(engine->appState.Instance, "xrDestroyFoveationProfileFB", (PFN_xrVoidFunction*)(&pfnDestroyFoveationProfile)));
		OXR(xrGetInstanceProcAddr(engine->appState.Instance, "xrUpdateSwapchainFB", (PFN_xrVoidFunction*)(&pfnUpdateSwapchain)));
		if (pfnCreateFoveationProfile && pfnUpdateSwapchain) {
			int eye;
			XrFoveationLevelProfileCreateInfoFB levelInfo;
			XrFoveationProfileCreateInfoFB profileInfo;
			XrFoveationProfileFB profile = XR_NULL_HANDLE;
			memset(&levelInfo, 0, sizeof(levelInfo));
			levelInfo.type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB;
			levelInfo.level = (XrFoveationLevelFB)VR_GetConfig(VR_CONFIG_FOVEATION_LEVEL);
			levelInfo.verticalOffset = 0;
			levelInfo.dynamic = XR_FOVEATION_DYNAMIC_DISABLED_FB;
			memset(&profileInfo, 0, sizeof(profileInfo));
			profileInfo.type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB;
			profileInfo.next = &levelInfo;
			OXR(pfnCreateFoveationProfile(engine->appState.Session, &profileInfo, &profile));
			if (profile != XR_NULL_HANDLE) {
				XrSwapchainStateFoveationFB foveationState;
				memset(&foveationState, 0, sizeof(foveationState));
				foveationState.type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB;
				foveationState.profile = profile;
				for (eye = 0; eye < (engine->appState.Renderer.Multiview ? 1 : ovrMaxNumEyes); eye++)
					OXR(pfnUpdateSwapchain(engine->appState.Renderer.FrameBuffer[eye].ColorSwapChain.Handle, (const XrSwapchainStateBaseHeaderFB*)&foveationState));
				if (pfnDestroyFoveationProfile)
					OXR(pfnDestroyFoveationProfile(profile));
				ALOGV("Foveated rendering enabled, level %i", VR_GetConfig(VR_CONFIG_FOVEATION_LEVEL));
			}
		}
	}
	initialized = true;
	ALOGV("Renderer initialised: eye buffers %ix%i, msaa %i, screen rect %ix%i, multiview %i", eyeW, eyeH, msaa,
			VR_GetConfig(VR_CONFIG_SCREEN_WIDTH), VR_GetConfig(VR_CONFIG_SCREEN_HEIGHT), multiview ? 1 : 0);
}

void VR_DestroyRenderer( engine_t* engine ) {
	ovrRenderer_Destroy(&engine->appState.Renderer);
	initialized = false;
}

bool VR_InitFrame( engine_t* engine ) {
	if (ovrApp_HandleXrEvents(&engine->appState)) {
		VR_Recenter(engine);
	}
	if (engine->appState.SessionActive == false) {
		return false;
	}

	if (stageBoundsDirty) {
		VR_UpdateStageBounds(&engine->appState);
		stageBoundsDirty = false;
	}

	XrFrameState frameState;
	memset(&frameState, 0, sizeof(frameState));
	frameState.type = XR_TYPE_FRAME_STATE;
	frameState.next = NULL;
	OXR(xrWaitFrame(engine->appState.Session, NULL, &frameState));
	engine->predictedDisplayTime = frameState.predictedDisplayTime;

	// Update HMD
	XrViewLocateInfo projectionInfo;
	memset(&projectionInfo, 0, sizeof(projectionInfo));
	projectionInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	projectionInfo.viewConfigurationType = engine->appState.ViewportConfig.viewConfigurationType;
	projectionInfo.displayTime = frameState.predictedDisplayTime;
	projectionInfo.space = engine->appState.CurrentSpace;
	XrViewState viewState = {XR_TYPE_VIEW_STATE, NULL};
	uint32_t projectionCapacityInput = ovrMaxNumEyes;
	uint32_t projectionCountOutput = projectionCapacityInput;
	OXR(xrLocateViews(
			engine->appState.Session,
			&projectionInfo,
			&viewState,
			projectionCapacityInput,
			&projectionCountOutput,
			projections));

	XrSpaceLocation loc;
	memset(&loc, 0, sizeof(loc));
	loc.type = XR_TYPE_SPACE_LOCATION;
	OXR(xrLocateSpace(engine->appState.HeadSpace, engine->appState.CurrentSpace, engine->predictedDisplayTime, &loc));
	headPose = loc.pose;
	headTracked = (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);

	// Update controllers
	IN_VRInputFrame(engine);

	float fovx = 0;
	float fovy = 0;
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		fovx += fabsf(projections[eye].fov.angleRight - projections[eye].fov.angleLeft) / 2.0f;
		fovy += fabsf(projections[eye].fov.angleUp - projections[eye].fov.angleDown) / 2.0f;
	}
	VR_SetConfigFloat(VR_CONFIG_VIEWPORT_FOVX, ToDegrees(fovx));
	VR_SetConfigFloat(VR_CONFIG_VIEWPORT_FOVY, ToDegrees(fovy));
	return true;
}

void VR_BeginFrame( engine_t* engine, int fboIndex ) {
	if (engine->appState.Renderer.Multiview)
		fboIndex = 0;   /* one 2-layer buffer serves both eyes */
	if (fboIndex == 0) {
		XrFrameBeginInfo beginFrameDesc;
		memset(&beginFrameDesc, 0, sizeof(beginFrameDesc));
		beginFrameDesc.type = XR_TYPE_FRAME_BEGIN_INFO;
		beginFrameDesc.next = NULL;
		OXR(xrBeginFrame(engine->appState.Session, &beginFrameDesc));
	}

	ovrFramebuffer_Acquire(&engine->appState.Renderer.FrameBuffer[fboIndex]);
	ovrFramebuffer_SetCurrent(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

void VR_EndFrame( engine_t* engine, int fboIndex ) {
	if (engine->appState.Renderer.Multiview)
		fboIndex = 0;
	VR_BindFramebuffer(engine, fboIndex);

	// Show mouse cursor
	int vrMode = vrConfig[VR_CONFIG_MODE];
	if (vrMode == VR_MODE_MONO_SCREEN && (vrConfig[VR_CONFIG_MOUSE_SIZE] > 0)) {
		int sx = vrConfig[VR_CONFIG_MOUSE_SIZE];
		ovrRenderer_MouseCursor(&engine->appState.Renderer, vrConfig[VR_CONFIG_MOUSE_X], vrConfig[VR_CONFIG_MOUSE_Y], sx, sx);
	}

	ovrFramebuffer_Resolve(&engine->appState.Renderer.FrameBuffer[fboIndex]);
	ovrFramebuffer_Release(&engine->appState.Renderer.FrameBuffer[fboIndex]);
	ovrFramebuffer_SetNone();
}

void VR_FinishFrame( engine_t* engine ) {
	int layerCount = 0;
	ovrCompositorLayer_Union layerUnion[ovrMaxLayerCount];
	memset(layerUnion, 0, sizeof(ovrCompositorLayer_Union) * ovrMaxLayerCount);

	int vrWidth = vrConfig[VR_CONFIG_VIEWPORT_WIDTH];
	int vrHeight = vrConfig[VR_CONFIG_VIEWPORT_HEIGHT];

	int vrMode = vrConfig[VR_CONFIG_MODE];
	XrCompositionLayerProjectionView projection_layer_elements[2];
	memset(projection_layer_elements, 0, sizeof(projection_layer_elements));
	if (vrMode == VR_MODE_STEREO_6DOF) {
		// keep the flat screen ready to appear in front of the user when it is next shown
		VR_SetConfigFloat(VR_CONFIG_MENU_YAW, XrQuaternionf_ToEulerAngles(headPose.orientation).y);
		menuYawValid = false;

		for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
			// multiview: both eyes live in the layers of frame buffer 0
			ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[engine->appState.Renderer.Multiview ? 0 : eye];
			projection_layer_elements[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projection_layer_elements[eye].pose = projections[eye].pose;
			projection_layer_elements[eye].fov = projections[eye].fov;
			projection_layer_elements[eye].subImage.swapchain = frameBuffer->ColorSwapChain.Handle;
			projection_layer_elements[eye].subImage.imageRect.offset.x = 0;
			projection_layer_elements[eye].subImage.imageRect.offset.y = 0;
			projection_layer_elements[eye].subImage.imageRect.extent.width = vrWidth;
			projection_layer_elements[eye].subImage.imageRect.extent.height = vrHeight;
			projection_layer_elements[eye].subImage.imageArrayIndex = engine->appState.Renderer.Multiview ? eye : 0;
		}

		XrCompositionLayerProjection projection_layer;
		memset(&projection_layer, 0, sizeof(projection_layer));
		projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		projection_layer.space = engine->appState.CurrentSpace;
		projection_layer.viewCount = ovrMaxNumEyes;
		projection_layer.views = projection_layer_elements;

		layerUnion[layerCount++].Projection = projection_layer;
	} else {
		// Flat screen: the 16:9 sub-rect of eye buffer 0, on a cylinder centred on the head
		int sw = vrConfig[VR_CONFIG_SCREEN_WIDTH];
		int sh = vrConfig[VR_CONFIG_SCREEN_HEIGHT];
		float distance = VR_GetConfigFloat(VR_CONFIG_CANVAS_DISTANCE);
		if (distance <= 0.5f) distance = 5.0f;
		// place the screen where the user is looking when it appears (and after a recenter)
		if (!menuYawValid && headTracked) {
			VR_SetConfigFloat(VR_CONFIG_MENU_YAW, XrQuaternionf_ToEulerAngles(headPose.orientation).y);
			menuYawValid = true;
		}
		float menuYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_MENU_YAW));
		XrVector3f yawAxis = {0, 1, 0};
		XrQuaternionf yaw = XrQuaternionf_CreateFromVectorAngle(yawAxis, menuYaw);
		float aspect = (float)sw / (float)sh;
		float centralAngle = 1.2f; /* ~69 degrees of arc */

		if (VR_HasCylinderLayerExt()) {
			XrCompositionLayerCylinderKHR cylinder_layer;
			memset(&cylinder_layer, 0, sizeof(cylinder_layer));
			cylinder_layer.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
			cylinder_layer.space = engine->appState.CurrentSpace;
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Handle;
			cylinder_layer.subImage.imageRect.offset.x = 0;
			cylinder_layer.subImage.imageRect.offset.y = 0;
			cylinder_layer.subImage.imageRect.extent.width = sw;
			cylinder_layer.subImage.imageRect.extent.height = sh;
			cylinder_layer.subImage.imageArrayIndex = 0;
			cylinder_layer.pose.orientation = yaw;
			cylinder_layer.pose.position = headPose.position; /* cylinder axis through the head */
			cylinder_layer.radius = distance;
			cylinder_layer.centralAngle = centralAngle;
			cylinder_layer.aspectRatio = aspect;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
		} else {
			XrCompositionLayerQuad quad_layer;
			memset(&quad_layer, 0, sizeof(quad_layer));
			quad_layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
			quad_layer.space = engine->appState.CurrentSpace;
			quad_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			quad_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Handle;
			quad_layer.subImage.imageRect.extent.width = sw;
			quad_layer.subImage.imageRect.extent.height = sh;
			quad_layer.pose.orientation = yaw;
			quad_layer.pose.position.x = headPose.position.x - sinf(menuYaw) * distance;
			quad_layer.pose.position.y = headPose.position.y;
			quad_layer.pose.position.z = headPose.position.z - cosf(menuYaw) * distance;
			quad_layer.size.width = distance * centralAngle;
			quad_layer.size.height = quad_layer.size.width / aspect;
			layerUnion[layerCount++].Quad = quad_layer;
		}
	}

	// Compose the layers for this frame.
	const XrCompositionLayerBaseHeader* layers[ovrMaxLayerCount];
	for (int i = 0; i < layerCount; i++) {
		layers[i] = (const XrCompositionLayerBaseHeader*)&layerUnion[i];
	}

	XrFrameEndInfo endFrameInfo;
	memset(&endFrameInfo, 0, sizeof(endFrameInfo));
	endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
	endFrameInfo.displayTime = engine->predictedDisplayTime;
	endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endFrameInfo.layerCount = layerCount;
	endFrameInfo.layers = layers;
	OXR(xrEndFrame(engine->appState.Session, &endFrameInfo));

	if (VR_GetConfig(VR_CONFIG_NEED_RECENTER)) {
		VR_SetConfig(VR_CONFIG_NEED_RECENTER, false);
		VR_Recenter(engine);
	}
}

int VR_GetConfig(enum VRConfig config ) {
	return vrConfig[config];
}

void VR_SetConfig(enum VRConfig config, int value) {
	vrConfig[config] = value;
}

float VR_GetConfigFloat(enum VRConfigFloat config) {
	return vrConfigFloat[config];
}

void VR_SetConfigFloat(enum VRConfigFloat config, float value) {
	vrConfigFloat[config] = value;
}

void VR_BindFramebuffer(engine_t *engine, int fboIndex) {
	if (!initialized) return;
	if (engine->appState.Renderer.Multiview) fboIndex = 0;
	ovrFramebuffer_SetCurrent(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

unsigned int VR_GetEyeFBO(engine_t *engine, int fboIndex) {
	if (!initialized) return 0;
	if (engine->appState.Renderer.Multiview) fboIndex = 0;
	return ovrFramebuffer_GetCurrentFBO(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

XrPosef VR_GetView(int eye) {
	return projections[eye].pose;
}

XrFovf VR_GetFov(int eye) {
	return projections[eye].fov;
}

XrPosef VR_GetHeadPose(void) {
	return headPose;
}

bool VR_HeadTracked(void) {
	return headTracked;
}

int VR_GetRefreshRate(void) {
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_REFRESH)) {
		if (!pfnGetDisplayRefreshRate) {
			OXR(xrGetInstanceProcAddr(
					VR_GetEngine()->appState.Instance,
					"xrGetDisplayRefreshRateFB",
					(PFN_xrVoidFunction*)(&pfnGetDisplayRefreshRate)));
		}
		if (pfnGetDisplayRefreshRate) {
			float currentDisplayRefreshRate = 0.0f;
			OXR(pfnGetDisplayRefreshRate(VR_GetEngine()->appState.Session, &currentDisplayRefreshRate));
			return (int)currentDisplayRefreshRate;
		}
	}
	return 72;
}

void VR_SetRefreshRate(int refresh) {
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_REFRESH)) {
		if (!pfnRequestDisplayRefreshRate) {
			OXR(xrGetInstanceProcAddr(
					VR_GetEngine()->appState.Instance,
					"xrRequestDisplayRefreshRateFB",
					(PFN_xrVoidFunction*)(&pfnRequestDisplayRefreshRate)));
		}
		if (pfnRequestDisplayRefreshRate) {
			OXR(pfnRequestDisplayRefreshRate(VR_GetEngine()->appState.Session, (float)refresh));
		}
	}
}
