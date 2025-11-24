#pragma once


#include <xf86drm.h>
#include <xf86drmMode.h>


class DRM {
public:
    DRM();
    int drm_atomic_commit(uint32_t fb_id, uint32_t flags);

public:
    struct Plane {
        drmModePlane *plane;
        drmModeObjectProperties *props;
        drmModePropertyRes **props_info;
    };

    struct Crtc {
        drmModeCrtc *crtc;
        drmModeObjectProperties *props;
        drmModePropertyRes **props_info;
    };

    struct Connector {
        drmModeConnector *connector;
        drmModeObjectProperties *props;
        drmModePropertyRes **props_info;
    };

	int fd;

	/* only used for atomic: */
	struct Plane *plane;
	struct Crtc *crtc;
	struct Connector *connector;
	int crtc_index;
	int kms_in_fence_fd;
	int kms_out_fence_fd;

	drmModeModeInfo *mode;
	uint32_t crtc_id;
	uint32_t connector_id;
};