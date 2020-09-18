#include "midimonster.h"

MM_PLUGIN_API int init();
static int ptz_configure(char* option, char* value);
static int ptz_configure_instance(instance* inst, char* option, char* value);
static int ptz_instance(instance* inst);
static channel* ptz_channel(instance* inst, char* spec, uint8_t flags);
static int ptz_set(instance* inst, size_t num, channel** c, channel_value* v);
static int ptz_handle(size_t num, managed_fd* fds);
static int ptz_start(size_t n, instance** inst);
static int ptz_shutdown(size_t n, instance** inst);

#define VISCA_BUFFER_LENGTH 50

typedef struct /*_ptz_instance_data*/ {
	int fd;
	uint8_t cam_address;
	uint16_t x;
	uint16_t y;
	uint8_t panspeed;
	uint8_t tiltspeed;
} ptz_instance_data;

enum /*ptz_channels*/ {
	pan = 0,
	tilt,
	panspeed,
	tiltspeed,
	zoom,
	focus,
	focus_mode,
	wb_red,
	wb_blue,
	wb_mode,
	call,
	store,
	home,
	stop,
	sentinel
};

typedef size_t (*ptz_channel_set)(instance*, channel*, channel_value*, uint8_t* msg);
static size_t ptz_set_pantilt(instance* inst, channel* c, channel_value* v, uint8_t* msg);
static size_t ptz_set_ptspeed(instance* inst, channel* c, channel_value* v, uint8_t* msg);
static size_t ptz_set_zoom(instance* inst, channel* c, channel_value* v, uint8_t* msg);
static size_t ptz_set_focus(instance* inst, channel* c, channel_value* v, uint8_t* msg);
static size_t ptz_set_focus_mode(instance* inst, channel* c, channel_value* v, uint8_t* msg);
static size_t ptz_set_wb_mode(instance* inst, channel* c, channel_value* v, uint8_t* msg);
static size_t ptz_set_wb(instance* inst, channel* c, channel_value* v, uint8_t* msg);
static size_t ptz_set_memory(instance* inst, channel* c, channel_value* v, uint8_t* msg);
static size_t ptz_set_memory_store(instance* inst, channel* c, channel_value* v, uint8_t* msg);

//relative move test
static size_t ptz_set_stop(instance* inst, channel* c, channel_value* v, uint8_t* msg);

static struct {
	char* name;
	size_t bytes;
	uint8_t pattern[VISCA_BUFFER_LENGTH];
	size_t min; //channel range = max - min
	size_t max;
	size_t offset; //channel value = normalised * range - offset
	ptz_channel_set set;
} ptz_channels[] = {
	[pan] = {"pan", 15, {0x80, 0x01, 0x06, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF}, 0, 0x990 * 2, 0x990, ptz_set_pantilt},
	[tilt] = {"tilt", 15, {0x80, 0x01, 0x06, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF}, 0, 0x510 * 2, 0x510, ptz_set_pantilt},
	[panspeed] = {"panspeed", 0, {0}, 0x01, 0x18, 0, ptz_set_ptspeed},
	[tiltspeed] = {"tiltspeed", 0, {0}, 0x01, 0x14, 0, ptz_set_ptspeed},
	[zoom] = {"zoom", 9, {0x80, 0x01, 0x04, 0x47, 0, 0, 0, 0, 0xFF}, 0, 0x4000, 0, ptz_set_zoom},
	[focus] = {"focus", 9, {0x80, 0x01, 0x04, 0x48, 0, 0, 0, 0, 0xFF}, 0, 0x4000, 0, ptz_set_focus},
	[focus_mode] = {"autofocus", 6, {0x80, 0x01, 0x04, 0x38, 0, 0xFF}, 0, 1, 0, ptz_set_focus_mode},
	[wb_mode] = {"wb.auto", 6, {0x80, 0x01, 0x04, 0x35, 0, 0xFF}, 0, 1, 0, ptz_set_wb_mode},
	[wb_red] = {"wb.red", 9, {0x80, 0x01, 0x04, 0x43, 0x00, 0x00, 0, 0, 0xFF}, 0, 255, 0, ptz_set_wb},
	[wb_blue] = {"wb.blue", 9, {0x80, 0x01, 0x04, 0x44, 0x00, 0x00, 0, 0, 0xFF}, 0, 255, 0, ptz_set_wb},
	[call] = {"memory", 7, {0x80, 0x01, 0x04, 0x3F, 0x02, 0, 0xFF}, 0, 254, 0, ptz_set_memory},
	[store] = {"store", 7, {0x80, 0x01, 0x04, 0x3F, 0x01, 0, 0xFF}, 0, 254, 0, ptz_set_memory_store},
	[home] = {"home", 5, {0x80, 0x01, 0x06, 0x04, 0xFF}, 0, 0, 0, NULL},
	//relative move test
	[stop] = {"stop", 9, {0x80, 0x01, 0x06, 0x01, 0, 0, 0x03, 0x03, 0xFF}, 0, 0, 0, ptz_set_stop}
};
