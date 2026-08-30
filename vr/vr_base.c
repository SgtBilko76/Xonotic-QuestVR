/*
 * vr_base.c - OpenXR loader/instance/system/session (XonoticQuest).
 * Ported from Team Beef's CSVR engine/vr/VrBase.c.
 */
#include "vr_base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool vr_platform[VR_PLATFORM_MAX];
static engine_t vr_engine;
int vr_initialized = 0;

static bool g_hasAndroidCreateInstanceExt = false;
static bool g_hasPerfSettingsExt = false;
static bool g_hasAndroidThreadSettingsExt = false;
static bool g_hasDisplayRefreshRateExt = false;
static bool g_hasCylinderLayerExt = false;

void OXR_CheckErrors(XrInstance instance, XrResult result, const char* function, bool failOnError) {
	if (XR_FAILED(result)) {
		char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
		xrResultToString(instance, result, errorBuffer);
		if (failOnError) {
			ALOGE("OpenXR error: %s: %s", function, errorBuffer);
		} else {
			ALOGV("OpenXR error: %s: %s", function, errorBuffer);
		}
	}
}

static bool XR_ExtensionSupported(const char* name, const XrExtensionProperties* props, uint32_t count)
{
	for (uint32_t i = 0; i < count; ++i) {
		if (strcmp(props[i].extensionName, name) == 0) {
			return true;
		}
	}
	return false;
}

bool VR_HasCylinderLayerExt(void) {
	return g_hasCylinderLayerExt;
}

void VR_Init( void* system, const char* name, int version ) {
	if (vr_initialized)
		return;

	ovrApp_Clear(&vr_engine.appState);

	PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
	xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
	if (xrInitializeLoaderKHR != NULL) {
		ovrJava* java = (ovrJava*)system;
		XrLoaderInitInfoAndroidKHR loaderInitializeInfo;
		memset(&loaderInitializeInfo, 0, sizeof(loaderInitializeInfo));
		loaderInitializeInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
		loaderInitializeInfo.next = NULL;
		loaderInitializeInfo.applicationVM = java->Vm;
		loaderInitializeInfo.applicationContext = java->ActivityObject;
		xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&loaderInitializeInfo);
	}

	uint32_t runtimeExtCount = 0;
	OXR(xrEnumerateInstanceExtensionProperties(NULL, 0, &runtimeExtCount, NULL));

	XrExtensionProperties runtimeExtProps[256];
	if (runtimeExtCount > (uint32_t)(sizeof(runtimeExtProps) / sizeof(runtimeExtProps[0]))) {
		runtimeExtCount = (uint32_t)(sizeof(runtimeExtProps) / sizeof(runtimeExtProps[0]));
	}
	for (uint32_t i = 0; i < runtimeExtCount; ++i) {
		memset(&runtimeExtProps[i], 0, sizeof(XrExtensionProperties));
		runtimeExtProps[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		runtimeExtProps[i].next = NULL;
	}
	OXR(xrEnumerateInstanceExtensionProperties(NULL, runtimeExtCount, &runtimeExtCount, runtimeExtProps));

	int extensionsCount = 0;
	const char* extensions[32];

	g_hasCylinderLayerExt = XR_ExtensionSupported(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME, runtimeExtProps, runtimeExtCount);
	if (g_hasCylinderLayerExt) {
		extensions[extensionsCount++] = XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME;
	}

	if (XR_ExtensionSupported(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, runtimeExtProps, runtimeExtCount)) {
		extensions[extensionsCount++] = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
	} else {
		ALOGE("OpenXR runtime does not support %s", XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
		exit(1);
	}

	g_hasAndroidCreateInstanceExt =
			VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_INSTANCE) &&
			XR_ExtensionSupported(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, runtimeExtProps, runtimeExtCount);
	if (g_hasAndroidCreateInstanceExt) {
		extensions[extensionsCount++] = XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME;
	}

	g_hasPerfSettingsExt =
			VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PERFORMANCE) &&
			XR_ExtensionSupported(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME, runtimeExtProps, runtimeExtCount);
	if (g_hasPerfSettingsExt) {
		extensions[extensionsCount++] = XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME;
	}

	g_hasAndroidThreadSettingsExt =
			VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PERFORMANCE) &&
			XR_ExtensionSupported(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME, runtimeExtProps, runtimeExtCount);
	if (g_hasAndroidThreadSettingsExt) {
		extensions[extensionsCount++] = XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME;
	}

	g_hasDisplayRefreshRateExt =
			VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_REFRESH) &&
			XR_ExtensionSupported(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, runtimeExtProps, runtimeExtCount);
	if (g_hasDisplayRefreshRateExt) {
		extensions[extensionsCount++] = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
	}

	XrApplicationInfo appInfo;
	memset(&appInfo, 0, sizeof(appInfo));
	strcpy(appInfo.applicationName, name);
	strcpy(appInfo.engineName, name);
	appInfo.applicationVersion = version;
	appInfo.engineVersion = version;
	appInfo.apiVersion = XR_CURRENT_API_VERSION;

	XrInstanceCreateInfo instanceCreateInfo;
	memset(&instanceCreateInfo, 0, sizeof(instanceCreateInfo));
	instanceCreateInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.next = NULL;
	instanceCreateInfo.createFlags = 0;
	instanceCreateInfo.applicationInfo = appInfo;
	instanceCreateInfo.enabledApiLayerCount = 0;
	instanceCreateInfo.enabledApiLayerNames = NULL;
	instanceCreateInfo.enabledExtensionCount = (uint32_t)extensionsCount;
	instanceCreateInfo.enabledExtensionNames = extensions;

	XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroid = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
	if (g_hasAndroidCreateInstanceExt) {
		ovrJava* java = (ovrJava*)system;
		instanceCreateInfoAndroid.applicationVM = java->Vm;
		instanceCreateInfoAndroid.applicationActivity = java->ActivityObject;
		instanceCreateInfo.next = (XrBaseInStructure*)&instanceCreateInfoAndroid;
	}

	XrResult initResult;
	OXR(initResult = xrCreateInstance(&instanceCreateInfo, &vr_engine.appState.Instance));
	if (initResult != XR_SUCCESS) {
		ALOGE("Failed to create XR instance: %d.", initResult);
		exit(1);
	}

	XrInstanceProperties instanceInfo;
	instanceInfo.type = XR_TYPE_INSTANCE_PROPERTIES;
	instanceInfo.next = NULL;
	OXR(xrGetInstanceProperties(vr_engine.appState.Instance, &instanceInfo));
	ALOGV("Runtime %s: Version : %u.%u.%u",
			instanceInfo.runtimeName,
			XR_VERSION_MAJOR(instanceInfo.runtimeVersion),
			XR_VERSION_MINOR(instanceInfo.runtimeVersion),
			XR_VERSION_PATCH(instanceInfo.runtimeVersion));

	XrSystemGetInfo systemGetInfo;
	memset(&systemGetInfo, 0, sizeof(systemGetInfo));
	systemGetInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	systemGetInfo.next = NULL;
	systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	XrSystemId systemId;
	OXR(initResult = xrGetSystem(vr_engine.appState.Instance, &systemGetInfo, &systemId));
	if (initResult != XR_SUCCESS) {
		ALOGE("Failed to get system.");
		exit(1);
	}

	PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetOpenGLESGraphicsRequirementsKHR = NULL;
	OXR(xrGetInstanceProcAddr(
			vr_engine.appState.Instance,
			"xrGetOpenGLESGraphicsRequirementsKHR",
			(PFN_xrVoidFunction*)(&pfnGetOpenGLESGraphicsRequirementsKHR)));

	XrGraphicsRequirementsOpenGLESKHR graphicsRequirements;
	memset(&graphicsRequirements, 0, sizeof(graphicsRequirements));
	graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
	OXR(pfnGetOpenGLESGraphicsRequirementsKHR(vr_engine.appState.Instance, systemId, &graphicsRequirements));

	vr_engine.appState.MainThreadTid = gettid();
	vr_engine.appState.SystemId = systemId;
	vr_initialized = 1;
}

void VR_Destroy( engine_t* engine ) {
	if (engine == &vr_engine) {
		xrDestroyInstance(engine->appState.Instance);
		ovrApp_Destroy(&engine->appState);
	}
}

void VR_EnterVR( engine_t* engine ) {

	if (engine->appState.Session) {
		ALOGE("VR_EnterVR called with existing session");
		return;
	}

	// The session binds to the EGL context that is current on this thread: SDL's GL context.
	XrSessionCreateInfo sessionCreateInfo;
	XrGraphicsBindingOpenGLESAndroidKHR graphicsBindingGL;
	memset(&sessionCreateInfo, 0, sizeof(sessionCreateInfo));
	memset(&graphicsBindingGL, 0, sizeof(graphicsBindingGL));
	graphicsBindingGL.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
	graphicsBindingGL.display = eglGetCurrentDisplay();
	graphicsBindingGL.config = NULL;
	graphicsBindingGL.context = eglGetCurrentContext();
	if (graphicsBindingGL.context == EGL_NO_CONTEXT)
		ALOGE("VR_EnterVR: no EGL context is current on this thread!");
	sessionCreateInfo.next = &graphicsBindingGL;
	sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
	sessionCreateInfo.createFlags = 0;
	sessionCreateInfo.systemId = engine->appState.SystemId;

	XrResult initResult;
	OXR(initResult = xrCreateSession(engine->appState.Instance, &sessionCreateInfo, &engine->appState.Session));
	if (initResult != XR_SUCCESS) {
		ALOGE("Failed to create XR session: %d.", initResult);
		exit(1);
	}

	XrReferenceSpaceCreateInfo spaceCreateInfo;
	memset(&spaceCreateInfo, 0, sizeof(spaceCreateInfo));
	spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
	OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.HeadSpace));
	engine->appState.RenderThreadTid = gettid();

	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PERFORMANCE)) {
		if (g_hasPerfSettingsExt) {
			XrPerfSettingsLevelEXT cpuPerfLevel = XR_PERF_SETTINGS_LEVEL_BOOST_EXT;
			XrPerfSettingsLevelEXT gpuPerfLevel = XR_PERF_SETTINGS_LEVEL_BOOST_EXT;

			PFN_xrPerfSettingsSetPerformanceLevelEXT pfnPerfSettingsSetPerformanceLevelEXT = NULL;
			OXR(xrGetInstanceProcAddr(
					engine->appState.Instance,
					"xrPerfSettingsSetPerformanceLevelEXT",
					(PFN_xrVoidFunction*)(&pfnPerfSettingsSetPerformanceLevelEXT)));

			if (pfnPerfSettingsSetPerformanceLevelEXT) {
				OXR(pfnPerfSettingsSetPerformanceLevelEXT(engine->appState.Session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, cpuPerfLevel));
				OXR(pfnPerfSettingsSetPerformanceLevelEXT(engine->appState.Session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, gpuPerfLevel));
			}
		}

		if (g_hasAndroidThreadSettingsExt) {
			PFN_xrSetAndroidApplicationThreadKHR pfnSetAndroidApplicationThreadKHR = NULL;
			OXR(xrGetInstanceProcAddr(
					engine->appState.Instance,
					"xrSetAndroidApplicationThreadKHR",
					(PFN_xrVoidFunction*)(&pfnSetAndroidApplicationThreadKHR)));

			if (pfnSetAndroidApplicationThreadKHR) {
				OXR(pfnSetAndroidApplicationThreadKHR(engine->appState.Session,
				                                     XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR,
				                                     engine->appState.MainThreadTid));
				OXR(pfnSetAndroidApplicationThreadKHR(engine->appState.Session,
				                                     XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR,
				                                     engine->appState.RenderThreadTid));
			}
		}
	}
}

void VR_LeaveVR( engine_t* engine ) {
	if (engine->appState.Session) {
		OXR(xrDestroySpace(engine->appState.HeadSpace));

		if (engine->appState.StageSpace != XR_NULL_HANDLE) {
			OXR(xrDestroySpace(engine->appState.StageSpace));
		}
		OXR(xrDestroySpace(engine->appState.FakeStageSpace));
		engine->appState.CurrentSpace = XR_NULL_HANDLE;
		OXR(xrDestroySession(engine->appState.Session));
		engine->appState.Session = XR_NULL_HANDLE;
	}
}

engine_t* VR_GetEngine( void ) {
	return &vr_engine;
}

bool VR_GetPlatformFlag(enum VRPlatformFlag flag) {
	return vr_platform[flag];
}

void VR_SetPlatformFLag(enum VRPlatformFlag flag, bool value) {
	vr_platform[flag] = value;
}
