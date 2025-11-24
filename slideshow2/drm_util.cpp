#include "drm_util.h"

#include <cerrno>
#include <stdexcept>
#include <cstring> //strerror
#include <fcntl.h> //open
#include <unistd.h> //close

#define MAX_DRM_DEVICES 64

static int find_drm_device(drmModeRes **resources)
{
	drmDevicePtr devices[MAX_DRM_DEVICES] = { NULL };
	int num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
	if (num_devices < 0) {
		printf("drmGetDevices2 failed: %s\n", strerror(-num_devices));
		return -1;
	}

	int fd = -1;
	for (int i = 0; i < num_devices; i++) {
		drmDevicePtr device = devices[i];
		if (!(device->available_nodes & (1 << DRM_NODE_PRIMARY)))
			continue;
		/* OK, it's a primary device. If we can get the
		 * drmModeResources, it means it's also a
		 * KMS-capable device.
		 */
		fd = open(device->nodes[DRM_NODE_PRIMARY], O_RDWR);
		if (fd < 0) continue;
			
        *resources = drmModeGetResources(fd);
		if (*resources != NULL) break;
			
		close(fd);
		fd = -1;
	}

	drmFreeDevices(devices, num_devices);

	return fd;
}

static drmModeConnector * find_drm_connector(int fd, drmModeRes *resources)
{
	drmModeConnector *connector = NULL;
	for (int i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector && connector->connection == DRM_MODE_CONNECTED) {
			/* it's connected, let's use this! */
			break;
		}
		drmModeFreeConnector(connector);
		connector = NULL;
	}

	return connector;
}

static int32_t find_crtc_for_encoder(const drmModeRes *resources, const drmModeEncoder *encoder) {
	for (int i = 0; i < resources->count_crtcs; i++) {
		/* possible_crtcs is a bitmask as described here:
		 * https://dvdhrm.wordpress.com/2012/09/13/linux-drm-mode-setting-api
		 */
		const uint32_t crtc_mask = 1 << i;
		const uint32_t crtc_id = resources->crtcs[i];
		if (encoder->possible_crtcs & crtc_mask) {
			return crtc_id;
		}
	}

	/* no match found */
	return -1;
}

static int32_t find_crtc_for_connector(int fd, const drmModeRes *resources, const drmModeConnector *connector) {
	for (int i = 0; i < connector->count_encoders; i++) {
		const uint32_t encoder_id = connector->encoders[i];
		drmModeEncoder *encoder = drmModeGetEncoder(fd, encoder_id);

		if (encoder) {
			const int32_t crtc_id = find_crtc_for_encoder(resources, encoder);

			drmModeFreeEncoder(encoder);
			if (crtc_id != 0) {
				return crtc_id;
			}
		}
	}

	/* no match found */
	return -1;
}

/* Pick a plane.. something that at a minimum can be connected to
 * the chosen crtc, but prefer primary plane.
 *
 * Seems like there is some room for a drmModeObjectGetNamedProperty()
 * type helper in libdrm..
 */
static int get_plane_id(int fd, int crtc_index)
{
	int ret = -EINVAL;
	bool found_primary = false;

	drmModePlaneResPtr plane_resources = drmModeGetPlaneResources(fd);
	if (!plane_resources) {
		printf("drmModeGetPlaneResources failed: %s\n", strerror(errno));
		return -1;
	}

	for (uint32_t i = 0; (i < plane_resources->count_planes) && !found_primary; i++) {
		uint32_t id = plane_resources->planes[i];
		drmModePlanePtr plane = drmModeGetPlane(fd, id);
		if (!plane) {
			printf("drmModeGetPlane(%u) failed: %s\n", id, strerror(errno));
			continue;
		}

		if (plane->possible_crtcs & (1 << crtc_index)) {
			drmModeObjectPropertiesPtr props =
				drmModeObjectGetProperties(fd, id, DRM_MODE_OBJECT_PLANE);

			/* primary or not, this plane is good enough to use: */
			ret = id;

			for (uint32_t j = 0; j < props->count_props; j++) {
				drmModePropertyPtr p =
					drmModeGetProperty(fd, props->props[j]);

				if ((strcmp(p->name, "type") == 0) &&
						(props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY)) {
					/* found our primary plane, lets use that: */
					found_primary = true;
				}

				drmModeFreeProperty(p);
			}

			drmModeFreeObjectProperties(props);
		}

		drmModeFreePlane(plane);
	}

	drmModeFreePlaneResources(plane_resources);

	return ret;
}

#define get_resource(type, Type, id) do { 					\
		this->type->type = drmModeGet##Type(this->fd, id);			\
		if (!this->type->type) {						\
			printf("could not get %s %i: %s\n",			\
					#type, id, strerror(errno));		\
			throw std::runtime_error("");						\
		}								\
	} while (0)

#define get_properties(type, TYPE, id) do {					\
		uint32_t i;							\
		this->type->props = drmModeObjectGetProperties(this->fd,		\
				id, DRM_MODE_OBJECT_##TYPE);			\
		if (!this->type->props) {						\
			printf("could not get %s %u properties: %s\n", 		\
					#type, id, strerror(errno));		\
			throw std::runtime_error("");						\
		}								\
		this->type->props_info = (drmModePropertyRes**)calloc(this->type->props->count_props,	\
				sizeof(*this->type->props_info));			\
		for (i = 0; i < this->type->props->count_props; i++) {		\
			this->type->props_info[i] = drmModeGetProperty(this->fd,	\
					this->type->props->props[i]);		\
		}								\
	} while (0)




DRM::DRM() {
	drmModeRes *resources;
	this->fd = find_drm_device(&resources);
	if (this->fd < 0) {
		throw std::runtime_error("could not open drm device\n");
	}
	if (!resources) {
		throw std::runtime_error("drmModeGetResources failed: " + std::string(strerror(errno)));
	}

    drmModeConnector *connector = find_drm_connector(this->fd, resources);
	if (!connector) {
		throw std::runtime_error("no connected connector!\n");
	}

	// find preferred or highest resolution mode
	for (int i = 0, area = 0; i < connector->count_modes; i++) {
		drmModeModeInfo *current_mode = &connector->modes[i];

		if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
			this->mode = current_mode;
			break;
		}

		int current_area = current_mode->hdisplay * current_mode->vdisplay;
		if (current_area > area) {
			this->mode = current_mode;
			area = current_area;
		}
	}

    if (!this->mode) {
		throw std::runtime_error("could not find mode!\n");
	}

    // find encoder
	drmModeEncoder *encoder = NULL;
	for (int i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(this->fd, resources->encoders[i]);
		if (encoder->encoder_id == connector->encoder_id)
			break;
		drmModeFreeEncoder(encoder);
		encoder = NULL;
	}

    if (encoder) {
		this->crtc_id = encoder->crtc_id;
	} else {
		int32_t crtc_id = find_crtc_for_connector(this->fd, resources, connector);
		if (crtc_id == -1) {
			throw std::runtime_error("no crtc found!\n");
		}

		this->crtc_id = crtc_id;
	}

	for (int i = 0; i < resources->count_crtcs; i++) {
		if (resources->crtcs[i] == this->crtc_id) {
			this->crtc_index = i;
			break;
		}
	}

	drmModeFreeResources(resources);

	this->connector_id = connector->connector_id;

    if (drmSetClientCap(this->fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
        throw std::runtime_error("no atomic modesetting support: " + std::string(strerror(errno)));
	}
    
	uint32_t plane_id = get_plane_id(this->fd, this->crtc_index);
	if (!plane_id) {
		throw std::runtime_error("could not find a suitable plane\n");
	}

	/* We only do single plane to single crtc to single connector, no
	 * fancy multi-monitor or multi-plane stuff.  So just grab the
	 * plane/crtc/connector property info for one of each:
	 */
	this->plane = (DRM::Plane*)calloc(1, sizeof(*this->plane));
	this->crtc = (DRM::Crtc*)calloc(1, sizeof(*this->crtc));
	this->connector = (DRM::Connector*)calloc(1, sizeof(*this->connector));

	get_resource(plane, Plane, plane_id);
	get_resource(crtc, Crtc, this->crtc_id);
	get_resource(connector, Connector, this->connector_id);

	get_properties(plane, PLANE, plane_id);
	get_properties(crtc, CRTC, this->crtc_id);
	get_properties(connector, CONNECTOR,this->connector_id);
}



static int add_connector_property(struct DRM::Connector *obj, drmModeAtomicReq *req, uint32_t obj_id,
					const char *name, uint64_t value)
{
	int prop_id = 0;
	for (unsigned int i = 0; i < obj->props->count_props ; i++) {
		if (strcmp(obj->props_info[i]->name, name) == 0) {
			prop_id = obj->props_info[i]->prop_id;
			break;
		}
	}

	if (prop_id < 0) {
		printf("no connector property: %s\n", name);
		return -EINVAL;
	}

	return drmModeAtomicAddProperty(req, obj_id, prop_id, value);
}

static int add_crtc_property(struct DRM::Crtc *obj, drmModeAtomicReq *req, uint32_t obj_id,
				const char *name, uint64_t value)
{
	int prop_id = -1;
	for (unsigned int i = 0; i < obj->props->count_props ; i++) {
		if (strcmp(obj->props_info[i]->name, name) == 0) {
			prop_id = obj->props_info[i]->prop_id;
			break;
		}
	}

	if (prop_id < 0) {
		printf("no crtc property: %s\n", name);
		return -EINVAL;
	}

	return drmModeAtomicAddProperty(req, obj_id, prop_id, value);
}

static int add_plane_property(struct DRM::Plane *obj, drmModeAtomicReq *req, uint32_t obj_id,
				const char *name, uint64_t value)
{
	int prop_id = -1;
	for (unsigned int i = 0; i < obj->props->count_props ; i++) {
		if (strcmp(obj->props_info[i]->name, name) == 0) {
			prop_id = obj->props_info[i]->prop_id;
			break;
		}
	}


	if (prop_id < 0) {
		printf("no plane property: %s\n", name);
		return -EINVAL;
	}

	return drmModeAtomicAddProperty(req, obj_id, prop_id, value);
}

int DRM::drm_atomic_commit(uint32_t fb_id, uint32_t flags)
{
	drmModeAtomicReq *req = drmModeAtomicAlloc();

	if (flags & DRM_MODE_ATOMIC_ALLOW_MODESET) {
		if (add_connector_property(this->connector, req, this->connector_id, "CRTC_ID", this->crtc_id) < 0)
			return -1;
	    
        uint32_t blob_id;
		if (drmModeCreatePropertyBlob(this->fd, this->mode, sizeof(*this->mode), &blob_id) != 0)
			return -1;

		if (add_crtc_property(this->crtc, req, this->crtc_id, "MODE_ID", blob_id) < 0)
			return -1;

		if (add_crtc_property(this->crtc, req, this->crtc_id, "ACTIVE", 1) < 0)
			return -1;
	}

	uint32_t plane_id = this->plane->plane->plane_id;
	add_plane_property(this->plane, req, plane_id, "FB_ID", fb_id);
	add_plane_property(this->plane, req, plane_id, "CRTC_ID", this->crtc_id);
	add_plane_property(this->plane, req, plane_id, "SRC_X", 0);
	add_plane_property(this->plane, req, plane_id, "SRC_Y", 0);
	add_plane_property(this->plane, req, plane_id, "SRC_W", this->mode->hdisplay << 16);
	add_plane_property(this->plane, req, plane_id, "SRC_H", this->mode->vdisplay << 16);
	add_plane_property(this->plane, req, plane_id, "CRTC_X", 0);
	add_plane_property(this->plane, req, plane_id, "CRTC_Y", 0);
	add_plane_property(this->plane, req, plane_id, "CRTC_W", this->mode->hdisplay);
	add_plane_property(this->plane, req, plane_id, "CRTC_H", this->mode->vdisplay);

	if (this->kms_in_fence_fd != -1) {
		add_crtc_property(this->crtc, req, this->crtc_id, "OUT_FENCE_PTR",
				(uint64_t)(unsigned long)(&this->kms_out_fence_fd));
		add_plane_property(this->plane, req, plane_id, "IN_FENCE_FD", this->kms_in_fence_fd);
	}

	int ret = drmModeAtomicCommit(this->fd, req, flags, NULL);
	if (ret) goto out;

	if (this->kms_in_fence_fd != -1) {
		close(this->kms_in_fence_fd);
		this->kms_in_fence_fd = -1;
	}

out:
	drmModeAtomicFree(req);

	return ret;
}

