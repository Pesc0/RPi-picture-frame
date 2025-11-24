#include "egl_util.h"

#include "gbm_util.h"

//#include <xf86drmMode.h>

#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <cassert>
#include <cstring>   // for strerror
//FIXME



#include <stdexcept>
#include "drm_util.h"


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
	if (has_ext(egl_exts_client, "EGL_EXT_platform_base")) this->eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

	if (this->eglGetPlatformDisplayEXT) {
		this->display = this->eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, gbm.dev, NULL);
	} else {
		this->display = eglGetDisplay((EGLNativeDisplayType)gbm.dev);
	}

	EGLint major, minor;
	if (!eglInitialize(this->display, &major, &minor)) {
		throw std::runtime_error("failed to initialize\n");
	}

	const char *egl_exts_dpy = eglQueryString(this->display, EGL_EXTENSIONS);
	if (has_ext(egl_exts_dpy, "EGL_KHR_image_base"))  			 this->eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR"); 
	if (has_ext(egl_exts_dpy, "EGL_KHR_image_base"))  			 this->eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR"); 
	if (has_ext(egl_exts_dpy, "EGL_KHR_fence_sync"))  			 this->eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR"); 
	if (has_ext(egl_exts_dpy, "EGL_KHR_fence_sync"))  			 this->eglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR"); 
	if (has_ext(egl_exts_dpy, "EGL_KHR_fence_sync"))  			 this->eglWaitSyncKHR = (PFNEGLWAITSYNCKHRPROC)eglGetProcAddress("eglWaitSyncKHR"); 
	if (has_ext(egl_exts_dpy, "EGL_KHR_fence_sync"))  			 this->eglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR"); 
	if (has_ext(egl_exts_dpy, "EGL_ANDROID_native_fence_sync"))  this->eglDupNativeFenceFDANDROID = (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID"); 

	this->modifiers_supported = has_ext(egl_exts_dpy, "EGL_EXT_image_dma_buf_import_modifiers");

	printf("Using display %p with EGL version %d.%d\n", this->display, major, minor);
	printf("===================================\n");
	printf("EGL information:\n");
	printf("  version: \"%s\"\n", eglQueryString(this->display, EGL_VERSION));
	printf("  vendor: \"%s\"\n", eglQueryString(this->display, EGL_VENDOR));
	printf("  client extensions: \"%s\"\n", egl_exts_client);
	printf("  display extensions: \"%s\"\n", egl_exts_dpy);
	printf("===================================\n");

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		throw std::runtime_error("failed to bind api EGL_OPENGL_ES_API\n");
	}

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
	if (this->context == EGL_NO_CONTEXT) {
		throw std::runtime_error("failed to create context\n");
	}


	this->surface = eglCreateWindowSurface(this->display, this->config, (EGLNativeWindowType)gbm.surface, NULL);
	if (this->surface == EGL_NO_SURFACE) {
		throw std::runtime_error("failed to create egl surface\n");
	}
	

	/* connect the context to the surface */
	eglMakeCurrent(this->display, this->surface, this->surface, this->context);

	const char *gl_exts = (char *) glGetString(GL_EXTENSIONS);
	printf("OpenGL ES 2.x information:\n");
	printf("  version: \"%s\"\n", glGetString(GL_VERSION));
	printf("  shading language version: \"%s\"\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	printf("  vendor: \"%s\"\n", glGetString(GL_VENDOR));
	printf("  renderer: \"%s\"\n", glGetString(GL_RENDERER));
	printf("  extensions: \"%s\"\n", gl_exts);
	printf("===================================\n");

	if (has_ext(gl_exts, "GL_OES_EGL_image"))  this->glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES"); 

    if (!this->eglDupNativeFenceFDANDROID ||
	    !this->eglCreateSyncKHR ||
	    !this->eglDestroySyncKHR ||
	    !this->eglWaitSyncKHR ||
	    !this->eglClientWaitSyncKHR)
			throw std::runtime_error("casino");
}

EGLSyncKHR EGL::create_fence(int fd)
{
	EGLint attrib_list[] = {
		EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd,
		EGL_NONE,
	};
	EGLSyncKHR fence = this->eglCreateSyncKHR(this->display,
			EGL_SYNC_NATIVE_FENCE_ANDROID, attrib_list);
	assert(fence);
	return fence;
}
















#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((((__u64)0) << 56) | ((1ULL << 56) - 1))
#endif


#define WEAK __attribute__((weak))

WEAK union gbm_bo_handle
gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane);

WEAK uint64_t
gbm_bo_get_modifier(struct gbm_bo *bo);

WEAK int
gbm_bo_get_plane_count(struct gbm_bo *bo);

WEAK uint32_t
gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane);

WEAK uint32_t
gbm_bo_get_offset(struct gbm_bo *bo, int plane);



bool EGL::create_framebuffer(const struct egl *egl, struct gbm_bo *bo, struct framebuffer *fb) {
	assert(this->eglCreateImageKHR);
	assert(bo);
	assert(fb);

	// 1. Create EGLImage.
	int fd = gbm_bo_get_fd(bo);
	if (fd < 0) {
		printf("failed to get fd for bo: %d\n", fd);
		return false;
	}

	EGLint khr_image_attrs[17] = {
		EGL_WIDTH, gbm_bo_get_width(bo),
		EGL_HEIGHT, gbm_bo_get_height(bo),
		EGL_LINUX_DRM_FOURCC_EXT, (int)gbm_bo_get_format(bo),
		EGL_DMA_BUF_PLANE0_FD_EXT, fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, gbm_bo_get_stride(bo),
		EGL_NONE, EGL_NONE,	/* modifier lo */
		EGL_NONE, EGL_NONE,	/* modifier hi */
		EGL_NONE,
	};

	if (this->modifiers_supported) {
		const uint64_t modifier = gbm_bo_get_modifier(bo);
		size_t attrs_index = 12;
		khr_image_attrs[attrs_index++] =
		    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
		khr_image_attrs[attrs_index++] = modifier & 0xfffffffful;
		khr_image_attrs[attrs_index++] =
		    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
		khr_image_attrs[attrs_index++] = modifier >> 32;
	}

	fb->image = this->eglCreateImageKHR(this->display, EGL_NO_CONTEXT,
			EGL_LINUX_DMA_BUF_EXT, NULL /* no client buffer */,
			khr_image_attrs);

	if (fb->image == EGL_NO_IMAGE_KHR) {
		printf("failed to make image from buffer object\n");
		return false;
	}

	// EGLImage takes the fd ownership.
	close(fd);

	// 2. Create GL texture and framebuffer.
	glGenTextures(1, &fb->tex);
	glBindTexture(GL_TEXTURE_2D, fb->tex);
	this->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, fb->image);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &fb->fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb->fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			fb->tex, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("failed framebuffer check for created target buffer\n");
		glDeleteFramebuffers(1, &fb->fb);
		glDeleteTextures(1, &fb->tex);
		return false;
	}

	return true;
}





static void
drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
	struct drm_fb *fb = (drm_fb*)data;

	if (fb->fb_id)
		drmModeRmFB(drm_fd, fb->fb_id);

	free(fb);
}

struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo)
{
	int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
	struct drm_fb *fb = (drm_fb*)gbm_bo_get_user_data(bo);
	uint32_t width, height, format,
		 strides[4] = {0}, handles[4] = {0},
		 offsets[4] = {0}, flags = 0;
	int ret = -1;

	if (fb)
		return fb;

	fb = (drm_fb*)calloc(1, sizeof *fb);
	fb->bo = bo;

	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	format = gbm_bo_get_format(bo);

	if (gbm_bo_get_handle_for_plane && gbm_bo_get_modifier &&
	    gbm_bo_get_plane_count && gbm_bo_get_stride_for_plane &&
	    gbm_bo_get_offset) {

		uint64_t modifiers[4] = {0};
		modifiers[0] = gbm_bo_get_modifier(bo);
		const int num_planes = gbm_bo_get_plane_count(bo);
		for (int i = 0; i < num_planes; i++) {
			handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
			strides[i] = gbm_bo_get_stride_for_plane(bo, i);
			offsets[i] = gbm_bo_get_offset(bo, i);
			modifiers[i] = modifiers[0];
		}

		if (modifiers[0] && modifiers[0] != DRM_FORMAT_MOD_INVALID) {
			flags = DRM_MODE_FB_MODIFIERS;
			printf("Using modifier %" PRIx64 "\n", modifiers[0]);
		}

		ret = drmModeAddFB2WithModifiers(drm_fd, width, height,
				format, handles, strides, offsets,
				modifiers, &fb->fb_id, flags);
	}

	if (ret) {
		if (flags)
			fprintf(stderr, "Modifiers failed!\n");

		memcpy(handles, (uint32_t [4]){gbm_bo_get_handle(bo).u32,0,0,0}, 16);
		memcpy(strides, (uint32_t [4]){gbm_bo_get_stride(bo),0,0,0}, 16);
		memset(offsets, 0, 16);
		ret = drmModeAddFB2(drm_fd, width, height, format,
				handles, strides, offsets, &fb->fb_id, 0);
	}

	if (ret) {
		uint32_t handle = gbm_bo_get_handle(bo).u32;
		uint32_t bpp = gbm_bo_get_bpp(bo);
		uint8_t depth = bpp;
		uint32_t pitch = gbm_bo_get_stride(bo);

		ret = drmModeAddFB(drm_fd, width, height, depth, bpp, pitch,
				handle, &fb->fb_id);

	}

	if (ret) {
		printf("failed to create fb: %s\n", strerror(errno));
		free(fb);
		return NULL;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

	return fb;
}


