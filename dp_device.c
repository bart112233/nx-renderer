#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dp.h"
#include "dp_common.h"

static const char *const connector_names[] = {
	"Unknown",
	"VGA",
	"DVI-I",
	"DVI-D",
	"DVI-A",
	"Composite",
	"SVIDEO",
	"LVDS",
	"Component",
	"9PinDIN",
	"DisplayPort",
	"HDMI-A",
	"HDMI-B",
	"TV",
	"eDP",
	"Virtual",
	"DSI",
};

static void dp_device_probe_screens(struct dp_device *device)
{
	unsigned int counts[ARRAY_SIZE(connector_names)];
	struct dp_screen *screen;
	drmModeRes *res;
	int i;

	memset(counts, 0, sizeof(counts));

	res = drmModeGetResources(device->fd);
	if (!res) {
		DP_ERR("fail : drmModeGetResources %m\n");
		return;
	}

	device->screens = calloc(res->count_connectors, sizeof(screen));
	if (!device->screens){
		DP_ERR("fail : calloc for res->count_connectors %m\n");
		return;
	}

	DP_DBG("count connectors:%d\n", res->count_connectors);
	for (i = 0; i < res->count_connectors; i++) {
		unsigned int *count;
		const char *type;
		int len;

		screen = dp_screen_create(device, res->connectors[i]);
		if (!screen) {
			DP_ERR("Failed to create screen for %d\n", i);
			continue;
		} else {
			/* assign a unique name to this screen */
			type = connector_names[screen->type];
			count = &counts[screen->type];

			len = snprintf(NULL, 0, "%s-%u", type, *count);

			screen->name = malloc(len + 1);
			if (!screen->name) {
				DP_ERR("Failed to malloc screen name for %d\n", i);
				free(screen);
				continue;
			} else {
				snprintf(screen->name, len + 1, "%s-%u", type, *count);
				(*count)++;
				device->screens[device->num_screens] = screen;
				device->num_screens++;
			}
		}
	}

	drmModeFreeResources(res);
}

static void dp_device_probe_crtcs(struct dp_device *device)
{
	struct dp_crtc *crtc;
	drmModeRes *res;
	int i;

	res = drmModeGetResources(device->fd);
	if (!res) {
		DP_ERR("fail : drmModeGetResources %m\n");
		return;
	}

	device->crtcs = calloc(res->count_crtcs, sizeof(crtc));
	if (!device->crtcs) {
		DP_ERR("fail : calloc for res->count_crtcs %m\n");
		return;
	}

	for (i = 0; i < res->count_crtcs; i++) {
		crtc = dp_crtc_create(device, res->crtcs[i]);
		if (!crtc)
			continue;

		DP_DBG("crtc id %d probed\n", crtc->id);
		device->crtcs[i] = crtc;
		device->num_crtcs++;
	}

	drmModeFreeResources(res);
}

static void dp_device_probe_planes(struct dp_device *device)
{
	struct dp_plane *plane;
	drmModePlaneRes *res;
	unsigned int i;

	res = drmModeGetPlaneResources(device->fd);
	if (!res) {
		DP_ERR("fail : drmModeGetPlaneResources %m\n");
		return;
	}

	device->planes = calloc(res->count_planes, sizeof(plane));
	if (!device->planes) {
		DP_ERR("fail : calloc for res->count_planes %m\n");
		return;
	}

	for (i = 0; i < res->count_planes; i++) {
		plane = dp_plane_create(device, res->planes[i]);
		if (!plane)
			continue;

		device->planes[i] = plane;
		device->num_planes++;
	}

	drmModeFreePlaneResources(res);
}

static void dp_device_probe(struct dp_device *device)
{
	dp_device_probe_screens(device);
	dp_device_probe_crtcs(device);
	dp_device_probe_planes(device);
}

static struct dp_screen *get_connector_by_name(struct dp_device *dev, const char *name)
{
	struct dp_screen *connector;
	int i;

	for (i = 0; i < dev->num_screens; i++) {
		connector = dev->screens[i];

		if (strcmp(connector->name, name) == 0)
			return connector;
	}

	return NULL;
}

static struct dp_screen *get_connector_by_id(struct dp_device *dev, uint32_t id)
{
	struct dp_screen *connector;
	int i;

	DP_DBG("connector id = %d\n", id);

	for (i = 0; i < dev->num_screens; i++) {
		connector = dev->screens[i];
		DP_DBG("[%d] id = %d\n", i, connector->id);
		if (connector && connector->id == id)
			return connector;
	}

	DP_DBG("Failed to get connector has id:%d\n",id);
	return NULL;
}

static int get_crtc_index(struct dp_device *dev, uint32_t id)
{
	int i = -1;

	for (i = 0; i < dev->num_crtcs; ++i) {
		struct dp_crtc *crtc = dev->crtcs[i];
		if (crtc && crtc->id == id)
			return i;
	}
	return -1;
}


struct dp_device *dp_device_open(int fd)
{
	struct dp_device *device;

	device = calloc(1, sizeof(*device));
	if (!device) {
		DP_ERR("fail : calloc for dp_device %m\n");
		return NULL;
	}

	device->fd = fd;

	dp_device_probe(device);

	return device;
}

void dp_device_close(struct dp_device *device)
{
	unsigned int i;

	for (i = 0; i < device->num_planes; i++)
		dp_plane_free(device->planes[i]);

	free(device->planes);

	for (i = 0; i < device->num_crtcs; i++)
		dp_crtc_free(device->crtcs[i]);

	free(device->crtcs);

	for (i = 0; i < device->num_screens; i++)
		dp_screen_free(device->screens[i]);

	free(device->screens);

	if (device->fd >= 0)
		close(device->fd);

	free(device);
}

struct dp_plane *dp_device_find_plane_by_index_for_screen(struct dp_device *device,
		unsigned int connector_index, unsigned int crtc_index, unsigned int plane_index)
{
	struct dp_crtc *crtc = NULL;
	struct dp_screen *connector;
	int i;

	if (connector_index > device->num_screens -1) {
		DP_ERR("fail: connector index %d over max %d\n",
				connector_index, device->num_screens);
	}
	if (crtc_index > device->num_crtcs-1) {
		DP_ERR("fail : crtc index %d over max %d\n",
			crtc_index, device->num_crtcs);
		return NULL;
	}

	connector = get_connector_by_id(device, device->screens[connector_index]->id);
	if (!connector) {
		DP_ERR("fail : get connector by id:%d\n", device->screens[connector_index]->id);
		return NULL;
	}

	crtc = device->crtcs[crtc_index];

	for (i = 0; i < device->num_planes; i++) {
		if (crtc->id != device->planes[i]->crtc->id)
			continue;

		if ((i + plane_index) >= device->num_planes)
			return NULL;

		if (crtc->id != device->planes[i]->crtc->id) {
			DP_ERR("fail : crtc id %d not equal plane[%d]'s crtc id %d\n",
				crtc->id, i, device->planes[i]->crtc->id);
			return NULL;
		}

		DP_DBG("planes %d <%d.%d>: device->planes[%d]->type = 0x%x\n",
			device->num_planes, crtc_index, plane_index,
			i+plane_index, device->planes[i+plane_index]->type);

		return device->planes[i+plane_index];
	}
	DP_ERR("fail : planes not exist (num planes %d)\n",
		device->num_planes);

	return NULL;
}

struct dp_plane *dp_device_find_plane_by_index(struct dp_device *device,
						unsigned int crtc_index, unsigned int plane_index)
{
	struct dp_crtc *crtc;
	unsigned int i;

	if (crtc_index > device->num_crtcs-1) {
		DP_ERR("fail : crtc index %d over max %d\n",
			crtc_index, device->num_crtcs);
		return NULL;
	}

	crtc = device->crtcs[crtc_index];

	for (i = 0; i < device->num_planes; i++) {
		if (crtc->id != device->planes[i]->crtc->id)
			continue;

		if ((i + plane_index) >= device->num_planes)
			return NULL;

		if (crtc->id != device->planes[i]->crtc->id) {
			DP_ERR("fail : crtc id %d not equal plane[%d]'s crtc id %d\n",
				crtc->id, i, device->planes[i]->crtc->id);
			return NULL;
		}

		DP_DBG("planes %d <%d.%d>: device->planes[%d]->type = 0x%x\n",
			device->num_planes, crtc_index, plane_index,
			i+plane_index, device->planes[i+plane_index]->type);

		return device->planes[i+plane_index];
	}

	DP_ERR("fail : planes not exist (num planes %d)\n",
		device->num_planes);

	return NULL;
}

struct dp_plane *dp_device_find_plane_by_type(struct dp_device *device,
						uint32_t type, unsigned int index)
{
	unsigned int i;

	for (i = 0; i < device->num_planes; i++) {
		DP_DBG("planes %d <%d>: device->planes[%d]->type = 0x%x : 0x%x\n",
			device->num_planes, index, i, device->planes[i]->type, type);
		if (device->planes[i]->type == type) {
			if (index == 0)
				return device->planes[i];

			index--;
		}
	}

	return NULL;
}
