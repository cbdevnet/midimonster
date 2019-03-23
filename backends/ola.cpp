#include "ola.h"
#include <cstring>
#include <ola/DmxBuffer.h>
#include <ola/Logging.h>
#include <ola/OlaClientWrapper.h>
#include <ola/client/OlaClient.h>
#include <ola/io/SelectServer.h>
#include <ola/network/Socket.h>

#define BACKEND_NAME "ola"
static ola::io::SelectServer* ola_select = NULL;
static ola::OlaCallbackClient* ola_client = NULL;

int init(){
	backend ola = {
		.name = BACKEND_NAME,
		.conf = ola_configure,
		.create = ola_instance,
		.conf_instance = ola_configure_instance,
		.channel = ola_channel,
		.handle = ola_set,
		.process = ola_handle,
		.start = ola_start,
		.shutdown = ola_shutdown
	};

	//register backend
	if(mm_backend_register(ola)){
		fprintf(stderr, "Failed to register OLA backend\n");
		return 1;
	}

	ola::InitLogging(ola::OLA_LOG_WARN, ola::OLA_LOG_STDERR);
	return 0;
}

static int ola_configure(char* option, char* value){
	fprintf(stderr, "Unknown OLA backend option %s\n", option);
	return 1;
}

static instance* ola_instance(){
	ola_instance_data* data = NULL;
	instance* inst = mm_instance();
	if(!inst){
		return NULL;
	}

	data = (ola_instance_data*)calloc(1, sizeof(ola_instance_data));
	if(!data){
		fprintf(stderr, "Failed to allocate memory\n");
		return NULL;
	}

	inst->impl = data;
	return inst;
}

static int ola_configure_instance(instance* inst, char* option, char* value){
	ola_instance_data* data = (ola_instance_data*) inst->impl;

	if(!strcmp(option, "universe")){
		data->universe_id = strtoul(value, NULL, 0);
		return 0;
	}

	fprintf(stderr, "Unknown OLA option %s for instance %s\n", option, inst->name);
	return 1;
}

static channel* ola_channel(instance* inst, char* spec){
	ola_instance_data* data = (ola_instance_data*) inst->impl;
	char* spec_next = spec;
	unsigned chan_a = strtoul(spec, &spec_next, 10);
	unsigned chan_b = 0;

	//primary channel sanity check
	if(!chan_a || chan_a > 512){
		fprintf(stderr, "Invalid OLA channel specification %s\n", spec);
		return NULL;
	}
	chan_a--;

	//secondary channel setup
	if(*spec_next == '+'){
		chan_b = strtoul(spec_next + 1, NULL, 10);
		if(!chan_b || chan_b > 512){
			fprintf(stderr, "Invalid wide-channel spec %s\n", spec);
			return NULL;
		}
		chan_b--;

		//if mapped mode differs, bail
		if(IS_ACTIVE(data->data.map[chan_b]) && data->data.map[chan_b] != (MAP_FINE | chan_a)){
			fprintf(stderr, "Fine channel already mapped for OLA spec %s\n", spec);
			return NULL;
		}

		data->data.map[chan_b] = MAP_FINE | chan_a;
	}

	//check current map mode
	if(IS_ACTIVE(data->data.map[chan_a])){
		if((*spec_next == '+' && data->data.map[chan_a] != (MAP_COARSE | chan_b))
				|| (*spec_next != '+' && data->data.map[chan_a] != (MAP_SINGLE | chan_a))){
			fprintf(stderr, "Primary OLA channel already mapped at differing mode: %s\n", spec);
			return NULL;
		}
	}
	data->data.map[chan_a] = (*spec_next == '+') ? (MAP_COARSE | chan_b) : (MAP_SINGLE | chan_a);

	return mm_channel(inst, chan_a, 1);
}

static int ola_set(instance* inst, size_t num, channel** c, channel_value* v){
	size_t u, mark = 0;
	ola_instance_data* data = (ola_instance_data*) inst->impl;

	for(u = 0; u < num; u++){
		if(IS_WIDE(data->data.map[c[u]->ident])){
			uint32_t val = v[u].normalised * ((double) 0xFFFF);
			//the primary (coarse) channel is the one registered to the core, so we don't have to check for that
			if(data->data.data[c[u]->ident] != ((val >> 8) & 0xFF)){
				mark = 1;
				data->data.data[c[u]->ident] = (val >> 8) & 0xFF;
			}

			if(data->data.data[MAPPED_CHANNEL(data->data.map[c[u]->ident])] != (val & 0xFF)){
				mark = 1;
				data->data.data[MAPPED_CHANNEL(data->data.map[c[u]->ident])] = val & 0xFF;
			}
		}
		else if(data->data.data[c[u]->ident] != (v[u].normalised * 255.0)){
			mark = 1;
			data->data.data[c[u]->ident] = v[u].normalised * 255.0;
		}
	}

	if(mark){
		ola_client->SendDmx(data->universe_id, ola::DmxBuffer(data->data.data, 512));
	}

	return 0;
}

static int ola_handle(size_t num, managed_fd* fds){
	if(!num){
		return 0;
	}

	//defer input to ola via the scenic route...
	ola_select->RunOnce();
	return 0;
}

static void ola_data_receive(unsigned int universe, const ola::DmxBuffer& ola_dmx, const std::string& error) {
	size_t p, max_mark = 0;
	//this should really be size_t but ola is weird...
	unsigned int dmx_length = 512;
	uint8_t raw_dmx[dmx_length];
	uint16_t wide_val;
	channel* chan = NULL;
	channel_value val;
	instance* inst = mm_instance_find(BACKEND_NAME, universe);
	if(!inst){
		return;
	}
	ola_instance_data* data = (ola_instance_data*) inst->impl;
	ola_dmx.Get((uint8_t*)raw_dmx, &dmx_length);

	//read data into instance universe, mark changed channels
	for(p = 0; p < dmx_length; p++){
		if(IS_ACTIVE(data->data.map[p]) && raw_dmx[p] != data->data.data[p]){
			data->data.data[p] = raw_dmx[p];
			data->data.map[p] |= MAP_MARK;
			max_mark = p;
		}
	}

	//generate channel events
	for(p = 0; p <= max_mark; p++){
		if(data->data.map[p] & MAP_MARK){
			data->data.map[p] &= ~MAP_MARK;
			if(data->data.map[p] & MAP_FINE){
				chan = mm_channel(inst, MAPPED_CHANNEL(data->data.map[p]), 0);
			}
			else{
				chan = mm_channel(inst, p, 0);
			}
			
			if(!chan){
				fprintf(stderr, "Active channel %zu on %s not known to core\n", p, inst->name);
				return;
			}

			if(IS_WIDE(data->data.map[p])){
				data->data.map[MAPPED_CHANNEL(data->data.map[p])] &= ~MAP_MARK;
				wide_val = data->data.data[p] << ((data->data.map[p] & MAP_COARSE) ? 8 : 0);
				wide_val |= data->data.data[MAPPED_CHANNEL(data->data.map[p])] << ((data->data.map[p] & MAP_COARSE) ? 0 : 8);

				val.raw.u64 = wide_val;
				val.normalised = (double) wide_val / (double) 0xFFFF;
			}
			else{
				val.raw.u64 = data->data.data[p];
				val.normalised = (double) data->data.data[p] / 255.0;
			}

			if(mm_channel_event(chan, val)){
				fprintf(stderr, "Failed to push OLA channel event to core\n");
				return;
			}
		}
	}
}

static void ola_register_callback(const std::string &error) {
	if(!error.empty()){
		fprintf(stderr, "OLA backend failed to register for universe: %s\n", error.c_str());
	}
}

static int ola_start(){
	size_t n, u, p;
	instance** inst = NULL;
	ola_instance_data* data = NULL;

	ola_select = new ola::io::SelectServer();
	ola::network::IPV4SocketAddress ola_server(ola::network::IPV4Address::Loopback(), ola::OLA_DEFAULT_PORT);
	ola::network::TCPSocket* ola_socket = ola::network::TCPSocket::Connect(ola_server);
	if(!ola_socket){
		fprintf(stderr, "Failed to connect to OLA server\n");
		return 1;
	}

	ola_client = new ola::OlaCallbackClient(ola_socket);

	if(!ola_client->Setup()){
		fprintf(stderr, "Failed to start OLA client\n");
		goto bail;
	}

	ola_select->AddReadDescriptor(ola_socket);

	fprintf(stderr, "OLA backend registering %zu descriptors to core\n", 1);
	if(mm_manage_fd(ola_socket->ReadDescriptor(), BACKEND_NAME, 1, NULL)){
		goto bail;
	}

	ola_client->SetDmxCallback(ola::NewCallback(&ola_data_receive));

	//fetch all defined instances
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		goto bail;
	}

	//this should not happen anymore (backends without instances are not started anymore)
	if(!n){
		free(inst);
		return 0;
	}

	for(u = 0; u < n; u++){
		data = (ola_instance_data*) inst[u]->impl;
		inst[u]->ident = data->universe_id;

		//check for duplicate instances (using the same universe)
		for(p = 0; p < u; p++){
			if(inst[u]->ident == inst[p]->ident){
				fprintf(stderr, "OLA universe used in multiple instances, use one instance: %s - %s\n", inst[u]->name, inst[p]->name);
				goto bail;
			}
		}
		ola_client->RegisterUniverse(data->universe_id, ola::REGISTER, ola::NewSingleCallback(&ola_register_callback));
	}

	//run the ola select implementation to run all commands
	ola_select->RunOnce();
	free(inst);
	return 0;
bail:
	free(inst);
	delete ola_client;
	ola_client = NULL;
	delete ola_select;
	ola_select = NULL;
	return 1;
}

static int ola_shutdown(){
	size_t n, p;
	instance** inst = NULL;
	if(mm_backend_instances(BACKEND_NAME, &n, &inst)){
		fprintf(stderr, "Failed to fetch instance list\n");
		return 1;
	}

	for(p = 0; p < n; p++){
		free(inst[p]->impl);
	}
	free(inst);

	if(ola_client){
		ola_client->Stop();
		delete ola_client;
		ola_client = NULL;
	}

	if(ola_select){
		ola_select->Terminate();
		delete ola_select;
		ola_select = NULL;
	}

	fprintf(stderr, "OLA backend shut down\n");
	return 0;
}
