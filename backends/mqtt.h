#include "midimonster.h"

MM_PLUGIN_API int init();
static int mqtt_configure(char* option, char* value);
static int mqtt_configure_instance(instance* inst, char* option, char* value);
static int mqtt_instance(instance* inst);
static channel* mqtt_channel(instance* inst, char* spec, uint8_t flags);
static int mqtt_set(instance* inst, size_t num, channel** c, channel_value* v);
static int mqtt_handle(size_t num, managed_fd* fds);
static int mqtt_start(size_t n, instance** inst);
static int mqtt_shutdown(size_t n, instance** inst);

#define MQTT_PORT "1883"
#define MQTT_TLS_PORT "8883"
#define MQTT_BUFFER_LENGTH 8192
#define MQTT_KEEPALIVE 10 
#define MQTT_VERSION_DEFAULT 0x05

#define MQTT5_NO_LOCAL 0x04

enum {
	MSG_RESERVED = 0x00,
	MSG_CONNECT = 0x10,
	MSG_CONNACK = 0x20,
	MSG_PUBLISH = 0x30,
	MSG_PUBACK = 0x40,
	MSG_PUBREC = 0x50,
	MSG_PUBREL = 0x60,
	MSG_PUBCOMP = 0x70,
	MSG_SUBSCRIBE = 0x82,
	MSG_SUBACK = 0x90,
	MSG_UNSUBSCRIBE = 0xA0,
	MSG_UNSUBACK = 0xB0,
	MSG_PINGREQ = 0xC0,
	MSG_PINGRESP = 0xD0,
	MSG_DISCONNECT = 0xE0,
	MSG_AUTH = 0xF0
};

//qos, subscribe
typedef struct /*_mqtt_channel*/ {
	char* topic;
	uint16_t topic_alias;
	uint8_t flags;
} mqtt_channel_data;

typedef struct /*_mqtt_instance_data*/ {
	uint8_t tls;
	char* host;
	char* port;
	uint8_t mqtt_version;

	char* user;
	char* password;
	char* client_id;

	size_t nchannels;
	mqtt_channel_data* channel;

	int fd;
	uint8_t receive_buffer[MQTT_BUFFER_LENGTH];
	size_t receive_offset;

	uint64_t last_control;
	uint16_t packet_identifier;
} mqtt_instance_data;
