
#pragma once



#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <gbm.h>
#include <stdbool.h>
#include <stddef.h>

extern const struct vertex {
        GLfloat position[3]; // x, y, z
        GLfloat color[3]; // r, g, b
        GLfloat normal[3]; // x, y, z
        GLfloat texCoord[2]; // s, t
} vertices[6 * 4]; // 6 faces with 4 corners vertices each

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* from mesa's util/macros.h: */
#define MIN2( A, B )   ( (A)<(B) ? (A) : (B) )
#define MAX2( A, B )   ( (A)>(B) ? (A) : (B) )
#define MIN3( A, B, C ) ((A) < (B) ? MIN2(A, C) : MIN2(B, C))
#define MAX3( A, B, C ) ((A) > (B) ? MAX2(A, C) : MAX2(B, C))

static inline unsigned
u_minify(unsigned value, unsigned levels)
{
	return MAX2(1, value >> levels);
}

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif

#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((((__u64)0) << 56) | ((1ULL << 56) - 1))
#endif

#ifndef EGL_KHR_platform_gbm
#define EGL_KHR_platform_gbm 1
#define EGL_PLATFORM_GBM_KHR              0x31D7
#endif /* EGL_KHR_platform_gbm */

#ifndef EGL_EXT_platform_base
#define EGL_EXT_platform_base 1
typedef EGLDisplay (EGLAPIENTRYP PFNEGLGETPLATFORMDISPLAYEXTPROC) (EGLenum platform, void *native_display, const EGLint *attrib_list);
typedef EGLSurface (EGLAPIENTRYP PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC) (EGLDisplay dpy, EGLConfig config, void *native_window, const EGLint *attrib_list);
typedef EGLSurface (EGLAPIENTRYP PFNEGLCREATEPLATFORMPIXMAPSURFACEEXTPROC) (EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLint *attrib_list);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLDisplay EGLAPIENTRY eglGetPlatformDisplayEXT (EGLenum platform, void *native_display, const EGLint *attrib_list);
EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformWindowSurfaceEXT (EGLDisplay dpy, EGLConfig config, void *native_window, const EGLint *attrib_list);
EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformPixmapSurfaceEXT (EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLint *attrib_list);
#endif
#endif /* EGL_EXT_platform_base */

#ifndef EGL_VERSION_1_5
#define EGLImage EGLImageKHR
#endif /* EGL_VERSION_1_5 */

#define WEAK __attribute__((weak))
#define NOSTRING __attribute__((__nonstring__))

/* Define tokens from EGL_EXT_image_dma_buf_import_modifiers */
#ifndef EGL_EXT_image_dma_buf_import_modifiers
#define EGL_EXT_image_dma_buf_import_modifiers 1
#define EGL_DMA_BUF_PLANE3_FD_EXT         0x3440
#define EGL_DMA_BUF_PLANE3_OFFSET_EXT     0x3441
#define EGL_DMA_BUF_PLANE3_PITCH_EXT      0x3442
#define EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT 0x3443
#define EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT 0x3444
#define EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT 0x3445
#define EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT 0x3446
#define EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT 0x3447
#define EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT 0x3448
#define EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT 0x3449
#define EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT 0x344A
#endif

#define NUM_BUFFERS 2

struct gbm {
	struct gbm_device *dev;
	struct gbm_surface *surface;
	struct gbm_bo *bos[NUM_BUFFERS];    /* for the surfaceless case */
	uint32_t format;
	int width, height;
};

const struct gbm * init_gbm(int drm_fd, int w, int h, uint32_t format, uint64_t modifier, bool surfaceless);

struct framebuffer {
	EGLImageKHR image;
	GLuint tex;
	GLuint fb;
};

struct egl {
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;
	EGLSurface surface;
	struct framebuffer fbs[NUM_BUFFERS];    /* for the surfaceless case */

	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
	PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
	PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
	PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
	PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
	PFNEGLWAITSYNCKHRPROC eglWaitSyncKHR;
	PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
	PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID;

	bool modifiers_supported;
};

static inline int __egl_check(void *ptr, const char *name)
{
	if (!ptr) {
		printf("no %s\n", name);
		return -1;
	}
	return 0;
}

#define egl_check(egl, name) __egl_check((egl)->name, #name)

struct egl * init_egl(const struct gbm *gbm, int samples);
int create_program(const char *vs_src, const char *fs_src, GLuint *out_program);

struct cube {
	void (*draw)(unsigned i);
};

enum mode {
	SMOOTH,        /* smooth-shaded */
	RGBA,          /* single-plane RGBA */
	NV12_2IMG,     /* NV12, handled as two textures and converted to RGB in shader */
	NV12_1IMG,     /* NV12, imported as planar YUV eglimg */
	VIDEO,         /* video textured cube */
	SHADERTOY,     /* display shadertoy shader */
};

const struct cube * init_cube_smooth(const struct egl *egl, const struct gbm *gbm);
const struct cube * init_cube_tex(const struct egl *egl, const struct gbm *gbm, enum mode mode);


#ifdef HAVE_GST

struct decoder;
struct decoder * video_init(const struct egl *egl, const struct gbm *gbm, const char *filename);
GLint video_frame(struct decoder *dec);
void video_deinit(struct decoder *dec);

const struct cube * init_cube_video(const struct egl *egl, const struct gbm *gbm, const char *video);

#else
static inline const struct cube *
init_cube_video(const struct egl *egl, const struct gbm *gbm, const char *video)
{
	(void)egl; (void)gbm; (void)video;
	printf("no GStreamer support!\n");
	return NULL;
}
#endif



int buf_to_fd(const struct gbm *gbm,
              uint32_t width, uint32_t height, uint32_t bpp, const void *ptr,
              uint32_t *pstride, uint64_t *modifier);



#include <xf86drm.h>
#include <xf86drmMode.h>

struct gbm;
struct egl;

struct plane {
	drmModePlane *plane;
	drmModeObjectProperties *props;
	drmModePropertyRes **props_info;
};

struct crtc {
	drmModeCrtc *crtc;
	drmModeObjectProperties *props;
	drmModePropertyRes **props_info;
};

struct connector {
	drmModeConnector *connector;
	drmModeObjectProperties *props;
	drmModePropertyRes **props_info;
};

struct drm {
	int fd;

	/* only used for atomic: */
	struct plane *plane;
	struct crtc *crtc;
	struct connector *connector;
	int crtc_index;
	int kms_in_fence_fd;
	int kms_out_fence_fd;

	drmModeModeInfo *mode;
	uint32_t crtc_id;
	uint32_t connector_id;

	/* number of frames to run for: */
	unsigned int count;
	bool nonblocking;
	/* used only for write to png option */
	uint8_t *pixels;

};



const struct drm * init_drm_atomic(const char *device, const char *mode_str, int connector_id, unsigned int vrefresh, unsigned int count, bool nonblocking);

