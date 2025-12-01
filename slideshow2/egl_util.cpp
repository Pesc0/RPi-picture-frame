#include "egl_util.h"

#include "gbm_util.h"

#include <cstring>
#include <cassert>
#include <stdexcept>

static bool has_ext(const char *extension_list, const char *ext)
{
	const char *ptr = extension_list;
	int len = strlen(ext);

	if (ptr == NULL || *ptr == '\0')
		return false;

	while (true) {
		ptr = strstr(ptr, ext);
		if (!ptr)
			return false;

		if (ptr[len] == ' ' || ptr[len] == '\0')
			return true;

		ptr += len;
	}
}



static int match_config_to_visual(EGLDisplay egl_display, EGLint visual_id,
		       EGLConfig *configs, int count)
{
	for (int i = 0; i < count; ++i) {
		EGLint id;

		if (!eglGetConfigAttrib(egl_display,
				configs[i], EGL_NATIVE_VISUAL_ID,
				&id))
			continue;

		if (id == visual_id)
			return i;
	}

	return -1;
}

static bool egl_choose_config(EGLDisplay egl_display, const EGLint *attribs,
                  EGLint visual_id, EGLConfig *config_out)
{
	int config_index = -1;
	EGLint count = 0;
	if (!eglGetConfigs(egl_display, NULL, 0, &count) || count < 1) {
		printf("No EGL configs to choose from.\n");
		return false;
	}
	EGLConfig *configs = (EGLConfig *)malloc(count * sizeof *configs);
	if (!configs)
		return false;

	EGLint matched = 0;
	if (!eglChooseConfig(egl_display, attribs, configs,
			      count, &matched) || !matched) {
		printf("No EGL configs with appropriate attributes.\n");
		goto out;
	}

	if (!visual_id)
		config_index = 0;

	if (config_index == -1)
		config_index = match_config_to_visual(egl_display,
						      visual_id,
						      configs,
						      matched);

	if (config_index != -1)
		*config_out = configs[config_index];

out:
	free(configs);
	if (config_index == -1)
		return false;

	return true;
}

EGL::EGL(const GBM &gbm)
{
	const char *egl_exts_client = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (has_ext(egl_exts_client, "EGL_EXT_platform_base")) 
		this->eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

	if (this->eglGetPlatformDisplayEXT) 
		this->display = this->eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, gbm.dev, NULL);
	else 
		this->display = eglGetDisplay((EGLNativeDisplayType)gbm.dev);

	EGLint major, minor;
	if (!eglInitialize(this->display, &major, &minor)) 
		throw std::runtime_error("EGL: failed to initialize");

	const char *egl_exts_dpy = eglQueryString(this->display, EGL_EXTENSIONS);
	if (has_ext(egl_exts_dpy, "EGL_KHR_fence_sync")) {
		this->eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR"); 
		this->eglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR"); 
		this->eglWaitSyncKHR = (PFNEGLWAITSYNCKHRPROC)eglGetProcAddress("eglWaitSyncKHR"); 
		this->eglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR"); 
	} 			 												
	if (has_ext(egl_exts_dpy, "EGL_ANDROID_native_fence_sync"))  
		this->eglDupNativeFenceFDANDROID = (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID"); 

	printf("Using display %p with EGL version %d.%d\n", this->display, major, minor);
	printf("===================================\n");
	printf("EGL information:\n");
	printf("  version: \"%s\"\n", eglQueryString(this->display, EGL_VERSION));
	printf("  vendor: \"%s\"\n", eglQueryString(this->display, EGL_VENDOR));
	//printf("  client extensions: \"%s\"\n", egl_exts_client);
	//printf("  display extensions: \"%s\"\n", egl_exts_dpy);
	printf("===================================\n");

	if (!eglBindAPI(EGL_OPENGL_ES_API)) 
		throw std::runtime_error("EGL: failed to bind api EGL_OPENGL_ES_API");

	const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SAMPLES, 0,
		EGL_NONE
	};
	if (!egl_choose_config(this->display, config_attribs, gbm.format, &this->config)) {
		throw std::runtime_error("failed to choose config\n");
	}

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	this->context = eglCreateContext(this->display, this->config, EGL_NO_CONTEXT, context_attribs);
	if (this->context == EGL_NO_CONTEXT) 
		throw std::runtime_error("EGL: failed to create context");
	
	this->surface = eglCreateWindowSurface(this->display, this->config, (EGLNativeWindowType)gbm.surface, NULL);
	if (this->surface == EGL_NO_SURFACE) 
		throw std::runtime_error("EGL: failed to create egl surface");
	
	/* connect the context to the surface */
	eglMakeCurrent(this->display, this->surface, this->surface, this->context);

	const char *gl_exts = (char *) glGetString(GL_EXTENSIONS);
	printf("OpenGL ES 2.x information:\n");
	printf("  version: \"%s\"\n", glGetString(GL_VERSION));
	printf("  shading language version: \"%s\"\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	printf("  vendor: \"%s\"\n", glGetString(GL_VENDOR));
	printf("  renderer: \"%s\"\n", glGetString(GL_RENDERER));
	//printf("  extensions: \"%s\"\n", gl_exts);
	printf("===================================\n");

    if (!this->eglDupNativeFenceFDANDROID ||
	    !this->eglCreateSyncKHR ||
	    !this->eglDestroySyncKHR ||
	    !this->eglWaitSyncKHR ||
	    !this->eglClientWaitSyncKHR)
			throw std::runtime_error("EGL: Does not have the required extensions for atomic drm");
}

EGLSyncKHR EGL::create_fence(int fd)
{
	EGLint attrib_list[] = {
		EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd,
		EGL_NONE,
	};
	EGLSyncKHR fence = this->eglCreateSyncKHR(this->display, EGL_SYNC_NATIVE_FENCE_ANDROID, attrib_list);
	assert(fence);
	return fence;
}

