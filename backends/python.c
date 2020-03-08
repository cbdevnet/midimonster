#define BACKEND_NAME "python"

#define PY_SSIZE_T_CLEAN
#include <string.h>
#include <Python.h>
#include "python.h"

#define MMPY_INSTANCE_KEY "midimonster_instance"

static PyThreadState* python_main = NULL;
static wchar_t* program_name = NULL;

static uint64_t last_timestamp = 0;
static uint32_t timer_interval = 0;
static size_t intervals = 0;
static mmpy_timer* interval = NULL;

MM_PLUGIN_API int init(){
	backend python = {
		.name = BACKEND_NAME,
		.conf = python_configure,
		.create = python_instance,
		.conf_instance = python_configure_instance,
		.channel = python_channel,
		.handle = python_set,
		.process = python_handle,
		.start = python_start,
		.interval = python_interval,
		.shutdown = python_shutdown
	};

	//register backend
	if(mm_backend_register(python)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
}

static uint32_t python_interval(){
	size_t u = 0;
	uint32_t next_timer = 1000;

	if(timer_interval){
		for(u = 0; u < intervals; u++){
			if(interval[u].interval &&
					interval[u].interval - interval[u].delta < next_timer){
				next_timer = interval[u].interval - interval[u].delta;
			}
		}
		DBGPF("Next timer fires in %" PRIu32, next_timer);
		return next_timer;
	}

	return 1000;
}

static void python_timer_recalculate(){
	uint64_t next_interval = 0, gcd, residual;
	size_t u;

	//find lower interval bounds
	for(u = 0; u < intervals; u++){
		if(interval[u].interval && (!next_interval || interval[u].interval < next_interval)){
			next_interval = interval[u].interval;
		}
	}

	if(next_interval){
		for(u = 0; u < intervals; u++){
			if(interval[u].interval){
				//calculate gcd of current interval and this timers interval
				gcd = interval[u].interval;
				while(gcd){
					residual = next_interval % gcd;
					next_interval = gcd;
					gcd = residual;
				}

				//10msec is absolute lower limit and minimum gcd due to rounding
				if(next_interval == 10){
					break;
				}
			}
		}
	}

	timer_interval = next_interval;
}

static int python_configure(char* option, char* value){
	LOG("No backend configuration possible");
	return 1;
}

static int python_prepend_str(PyObject* list, char* str){
	if(!list || !str){
		return 1;
	}

	PyObject* item = PyUnicode_FromString(str);
	if(!item){
		return 1;
	}

	if(PyList_Insert(list, 0, item) < 0){
		Py_DECREF(item);
		return 1;
	}
	Py_DECREF(item);
	return 0;
}

static PyObject* mmpy_output(PyObject* self, PyObject* args){
	instance* inst = *((instance**) PyModule_GetState(self));
	python_instance_data* data = (python_instance_data*) inst->impl;
	const char* channel_name = NULL;
	channel* chan = NULL;
	channel_value val = {
		{0}
	};
	size_t u;

	if(!PyArg_ParseTuple(args, "sd", &channel_name, &val.normalised)){
		return NULL;
	}

	val.normalised = clamp(val.normalised, 1.0, 0.0);

	for(u = 0; u < data->channels; u++){
		if(!strcmp(data->channel[u].name, channel_name)){
			DBGPF("Setting channel %s.%s to %f", inst->name, channel_name, val.normalised);
			chan = mm_channel(inst, u, 0);
			//this should never happen
			if(!chan){
				LOGPF("Failed to fetch parsed channel %s.%s", inst->name, channel_name);
				break;
			}
			data->channel[u].out = val.normalised;
			mm_channel_event(chan, val);
			break;
		}
	}

	if(u == data->channels){
		DBGPF("Output on unknown channel %s.%s, no event pushed", inst->name, channel_name);
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* mmpy_channel_value(PyObject* self, PyObject* args, uint8_t in){
	instance* inst = *((instance**) PyModule_GetState(self));
	python_instance_data* data = (python_instance_data*) inst->impl;
	const char* channel_name = NULL;
	size_t u;

	if(!PyArg_ParseTuple(args, "s", &channel_name)){
		return NULL;
	}

	for(u = 0; u < data->channels; u++){
		if(!strcmp(data->channel[u].name, channel_name)){
			return PyFloat_FromDouble(in ? data->channel[u].in : data->channel[u].out);
		}
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* mmpy_current_handler(PyObject* self, PyObject* args){
	instance* inst = *((instance**) PyModule_GetState(self));
	python_instance_data* data = (python_instance_data*) inst->impl;

	if(data->current_channel){
		return PyUnicode_FromString(data->current_channel->name);
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* mmpy_output_value(PyObject* self, PyObject* args){
	return mmpy_channel_value(self, args, 0);
}

static PyObject* mmpy_input_value(PyObject* self, PyObject* args){
	return mmpy_channel_value(self, args, 1);
}

static PyObject* mmpy_timestamp(PyObject* self, PyObject* args){
	return PyLong_FromUnsignedLong(mm_timestamp());
}

static PyObject* mmpy_interval(PyObject* self, PyObject* args){
	instance* inst = *((instance**) PyModule_GetState(self));
	python_instance_data* data = (python_instance_data*) inst->impl;
	unsigned long updated_interval = 0;
	PyObject* reference = NULL;
	size_t u;

	if(!PyArg_ParseTuple(args, "Ok", &reference, &updated_interval)){
		return NULL;
	}

	if(!PyCallable_Check(reference)){
		PyErr_SetString(PyExc_TypeError, "interval() requires a callable");
		return NULL;
	}

	//round interval
	if(updated_interval % 10 < 5){
		updated_interval -= updated_interval % 10;
	}
	else{
		updated_interval += (10 - (updated_interval % 10));
	}

	//find reference
	for(u = 0; u < intervals; u++){
		if(interval[u].interpreter == data->interpreter
				&& PyObject_RichCompareBool(reference, interval[u].reference, Py_EQ) == 1){
			DBGPF("Updating interval to %" PRIu64 " msec", updated_interval);
			break;
		}
	}

	//register new interval
	if(u == intervals && updated_interval){
		//create new interval slot
		DBGPF("Registering interval with %" PRIu64 " msec", updated_interval);
		interval = realloc(interval, (intervals + 1) * sizeof(mmpy_timer));
		if(!interval){
			intervals = 0;
			LOG("Failed to allocate memory");
			return NULL;
		}
		Py_INCREF(reference);
		interval[intervals].delta = 0;
		interval[intervals].reference = reference;
		interval[intervals].interpreter = data->interpreter;
		intervals++;
	}

	//update if existing or created
	if(u < intervals){
		interval[u].interval = updated_interval;
		python_timer_recalculate();
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* mmpy_manage_fd(PyObject* self, PyObject* args){
	instance* inst = *((instance**) PyModule_GetState(self));
	python_instance_data* data = (python_instance_data*) inst->impl;
	PyObject* handler = NULL, *sock = NULL, *fileno = NULL;
	size_t u = 0, last_free = 0;
	int fd = -1;

	if(!PyArg_ParseTuple(args, "OO", &handler, &sock)){
		return NULL;
	}

	if(handler != Py_None && !PyCallable_Check(handler)){
		PyErr_SetString(PyExc_TypeError, "manage() requires either None or a callable");
		return NULL;
	}

	fileno = PyObject_CallMethod(sock, "fileno", NULL);
	if(!fileno || fileno == Py_None || !PyLong_Check(fileno)){
		PyErr_SetString(PyExc_TypeError, "manage() requires a socket-like object");
		return NULL;
	}

	fd = PyLong_AsLong(fileno);
	if(fd < 0){
		PyErr_SetString(PyExc_TypeError, "manage() requires a (connected) socket-like object");
		return NULL;
	}

	//check if this socket instance was already registered
	last_free = data->sockets;
	for(u = 0; u < data->sockets; u++){
		if(!data->socket[u].socket){
			last_free = u;
		}
		else if(PyObject_RichCompareBool(sock, data->socket[u].socket, Py_EQ) == 1){
			break;
		}
	}

	if(u < data->sockets){
		//modify existing socket
		Py_XDECREF(data->socket[u].handler);
		if(handler != Py_None){
			DBGPF("Updating handler for fd %d on %s", fd, inst->name);
			data->socket[u].handler = handler;
			Py_INCREF(handler);
		}
		else{
			DBGPF("Unregistering fd %d on %s", fd, inst->name);
			mm_manage_fd(data->socket[u].fd, BACKEND_NAME, 0, NULL);
			Py_XDECREF(data->socket[u].socket);
			data->socket[u].handler = NULL;
			data->socket[u].socket = NULL;
			data->socket[u].fd = -1;
		}
	}
	else if(handler != Py_None){
		//check that the fd is not already registered with another socket instance
		for(u = 0; u < data->sockets; u++){
			if(data->socket[u].fd == fd){
				//FIXME this might also raise an exception
				LOGPF("Descriptor already registered with another socket on instance %s", inst->name);
				Py_INCREF(Py_None);
				return Py_None;
			}
		}

		DBGPF("Registering new fd %d on %s", fd, inst->name);
		if(last_free == data->sockets){
			//allocate a new socket instance
			data->socket = realloc(data->socket, (data->sockets + 1) * sizeof(mmpy_socket));
			if(!data->socket){
				data->sockets = 0;
				LOG("Failed to allocate memory");
				return NULL;
			}
			data->sockets++;
		}

		//store new reference
		//FIXME check this for errors
		mm_manage_fd(fd, BACKEND_NAME, 1, inst);
		data->socket[last_free].fd = fd;
		Py_INCREF(handler);
		data->socket[last_free].handler = handler;
		Py_INCREF(sock);
		data->socket[last_free].socket = sock;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static int mmpy_exec(PyObject* module) {
	instance** inst = (instance**) PyModule_GetState(module);
	//FIXME actually use interpreter dict (from python 3.8) here at some point
	PyObject* capsule = PyDict_GetItemString(PyThreadState_GetDict(), MMPY_INSTANCE_KEY);
	if(capsule && inst){
		*inst = PyCapsule_GetPointer(capsule, NULL);
		return 0;
	}

	PyErr_SetString(PyExc_AssertionError, "Failed to pass instance pointer for initialization");
	return -1;
}

static int python_configure_instance(instance* inst, char* option, char* value){
	python_instance_data* data = (python_instance_data*) inst->impl;
	PyObject* module = NULL;

	//load python script
	if(!strcmp(option, "module")){
		//swap to interpreter
		PyEval_RestoreThread(data->interpreter);
		//import the module
		module = PyImport_ImportModule(value);
		if(!module){
			LOGPF("Failed to import module %s to instance %s", value, inst->name);
			PyErr_Print();
		}
		Py_XDECREF(module);
		PyEval_ReleaseThread(data->interpreter);
		return 0;
	}

	LOGPF("Unknown instance parameter %s for instance %s", option, inst->name);
	return 1;
}

static PyObject* mmpy_init(){
	static PyModuleDef_Slot mmpy_slots[] = {
		{Py_mod_exec, (void*) mmpy_exec},
		{0}
	};

	static PyMethodDef mmpy_methods[] = {
		{"output", mmpy_output, METH_VARARGS, "Output a channel event"},
		{"inputvalue", mmpy_input_value, METH_VARARGS, "Get last input value for a channel"},
		{"outputvalue", mmpy_output_value, METH_VARARGS, "Get the last output value for a channel"},
		{"current", mmpy_current_handler, METH_VARARGS, "Get the name of the currently executing channel handler"},
		{"timestamp", mmpy_timestamp, METH_VARARGS, "Get the core timestamp (in milliseconds)"},
		{"manage", mmpy_manage_fd, METH_VARARGS, "(Un-)register a socket or file descriptor for notifications"},
		{"interval", mmpy_interval, METH_VARARGS, "Register or update an interval handler"},
		{0}
	};

	static struct PyModuleDef mmpy = {
		PyModuleDef_HEAD_INIT,
		"midimonster",
		NULL, /*doc size*/
		sizeof(instance*),
		mmpy_methods,
		mmpy_slots
	};

	//single-phase init
	//return PyModule_Create(&mmpy);

	//multi-phase init
	return PyModuleDef_Init(&mmpy);
}

static int python_instance(instance* inst){
	python_instance_data* data = calloc(1, sizeof(python_instance_data));
	PyObject* interpreter_dict = NULL;
	char current_directory[8192];
	if(!data){
		LOG("Failed to allocate memory");
		return 1;
	}

	//lazy-init because we need the interpreter running before _start,
	//but don't want it running if no instances are defined
	if(!python_main){
		LOG("Initializing main python interpreter");
		if(PyImport_AppendInittab("midimonster", &mmpy_init)){
			LOG("Failed to extend python inittab for main interpreter");
		}
		program_name = Py_DecodeLocale("midimonster", NULL);
		Py_SetProgramName(program_name);
		//initialize python
		Py_InitializeEx(0);
		//create, acquire and release the GIL
		PyEval_InitThreads();
		python_main = PyEval_SaveThread();
	}

	//acquire the GIL before creating a new interpreter
	PyEval_RestoreThread(python_main);
	//create subinterpreter for new instance
	data->interpreter = Py_NewInterpreter();

	//push cwd as import path
	if(getcwd(current_directory, sizeof(current_directory))){
		if(python_prepend_str(PySys_GetObject("path"), current_directory)){
			LOG("Failed to push current working directory to python");
			goto bail;
		}
	}

	//push the instance pointer for later module initialization
	//FIXME python 3.8 introduces interpreter_dict = PyInterpreterState_GetDict(data->interpreter->interp);
	//for now use thread state...
	interpreter_dict = PyThreadState_GetDict();
	if(!interpreter_dict){
		LOG("Failed to access per-interpreter data storage");
		goto bail;
	}
	//FIXME this might leak a reference to the capsule
	if(PyDict_SetItemString(interpreter_dict, MMPY_INSTANCE_KEY, PyCapsule_New(inst, NULL, NULL))){
		LOG("Failed to set per-interpreter instance pointer");
		goto bail;
	}

	//NewInterpreter leaves us with the GIL, drop it
	PyEval_ReleaseThread(data->interpreter);
	inst->impl = data;
	return 0;

bail:
	if(data->interpreter){
		PyEval_ReleaseThread(data->interpreter);
	}
	free(data);
	return 1;
}

static channel* python_channel(instance* inst, char* spec, uint8_t flags){
	python_instance_data* data = (python_instance_data*) inst->impl;
	size_t u;

	for(u = 0; u < data->channels; u++){
		if(!strcmp(data->channel[u].name, spec)){
			break;
		}
	}

	if(u == data->channels){
		data->channel = realloc(data->channel, (data->channels + 1) * sizeof(mmpython_channel));
		if(!data->channel){
			data->channels = 0;
			LOG("Failed to allocate memory");
			return NULL;
		}
		memset(data->channel + u, 0, sizeof(mmpython_channel));

		data->channel[u].name = strdup(spec);
		if(!data->channel[u].name){
			LOG("Failed to allocate memory");
			return NULL;
		}
		data->channels++;
	}

	return mm_channel(inst, u, 1);
}

static int python_set(instance* inst, size_t num, channel** c, channel_value* v){
	python_instance_data* data = (python_instance_data*) inst->impl;
	mmpython_channel* chan = NULL;
	PyObject* result = NULL;
	size_t u;

	//swap to interpreter
	PyEval_RestoreThread(data->interpreter);

	for(u = 0; u < num; u++){
		chan = data->channel + c[u]->ident;

		//update input value buffer
		chan->in = v[u].normalised;

		//call handler if present
		if(chan->handler){
			DBGPF("Calling handler for %s.%s", inst->name, chan->name);
			data->current_channel = chan;
			result = PyObject_CallFunction(chan->handler, "d", chan->in);
			Py_XDECREF(result);
			data->current_channel = NULL;
			DBGPF("Done with handler for %s.%s", inst->name, chan->name);
		}
	}

	//release interpreter
	PyEval_ReleaseThread(data->interpreter);
	return 0;
}

static int python_handle(size_t num, managed_fd* fds){
	instance* inst = NULL;
	python_instance_data* data = NULL;
	PyObject* result = NULL;
	size_t u, p;

	//handle intervals
	if(timer_interval){
		uint64_t delta = mm_timestamp() - last_timestamp;
		last_timestamp = mm_timestamp();

		//add delta to all active timers
		for(u = 0; u < intervals; u++){
			if(interval[u].interval){
				interval[u].delta += delta;

				//if timer expired, call handler
				if(interval[u].delta >= interval[u].interval){
					interval[u].delta %= interval[u].interval;

					//swap to interpreter
					PyEval_RestoreThread(interval[u].interpreter);
					//call handler
					result = PyObject_CallFunction(interval[u].reference, NULL);
					Py_XDECREF(result);
					//release interpreter
					PyEval_ReleaseThread(interval[u].interpreter);
					DBGPF("Calling interval handler %" PRIsize_t, u);
				}
			}
		}
	}

	for(u = 0; u < num; u++){
		inst = (instance*) fds[u].impl;
		data = (python_instance_data*) inst->impl;

		//swap to interpreter
		PyEval_RestoreThread(data->interpreter);

		//handle callbacks
		for(p = 0; p < data->sockets; p++){
			if(data->socket[p].socket
					&& data->socket[p].fd == fds[u].fd){
				//FIXME maybe close/unregister the socket on handling errors
				DBGPF("Calling descriptor handler on %s for fd %d", inst->name, data->socket[p].fd);
				result = PyObject_CallFunction(data->socket[p].handler, "O", data->socket[p].socket);
				Py_XDECREF(result);
			}
		}

		//release interpreter
		PyEval_ReleaseThread(data->interpreter);
	}

	return 0;
}

static int python_start(size_t n, instance** inst){
	python_instance_data* data = NULL;
	PyObject* module = NULL;
	size_t u, p;
	char* module_name = NULL, *channel_name = NULL;

	//resolve channel references to handler functions
	for(u = 0; u < n; u++){
		data = (python_instance_data*) inst[u]->impl;

		//switch to interpreter
		PyEval_RestoreThread(data->interpreter);
		for(p = 0; p < data->channels; p++){
			module = PyImport_AddModule("__main__");
			channel_name = data->channel[p].name;
			module_name = strchr(channel_name, '.');
			if(module_name){
				*module_name = 0;
				//returns borrowed reference
				module = PyImport_AddModule(channel_name);

				if(!module){
					LOGPF("Module %s for qualified channel %s.%s is not loaded on instance %s", channel_name, channel_name, module_name + 1, inst[u]->name);
					return 1;
				}

				*module_name = '.';
				channel_name = module_name + 1;
			}

			//returns new reference
			data->channel[p].handler = PyObject_GetAttrString(module, channel_name);
		}

		//release interpreter
		PyEval_ReleaseThread(data->interpreter);
	}
	return 0;
}

static int python_shutdown(size_t n, instance** inst){
	size_t u, p;
	python_instance_data* data = NULL;

	//clean up channels
	//this needs to be done before stopping the interpreters,
	//because the handler references are refcounted
	for(u = 0; u < n; u++){
		data = (python_instance_data*) inst[u]->impl;
		for(p = 0; p < data->channels; p++){
			free(data->channel[p].name);
			Py_XDECREF(data->channel[p].handler);
		}
		free(data->channel);
		//do not free data here, needed for shutting down interpreters
	}

	if(python_main){
		//just used to lock the GIL
		PyEval_RestoreThread(python_main);

		for(u = 0; u < n; u++){
			data = (python_instance_data*) inst[u]->impl;

			//close sockets
			for(p = 0; p < data->sockets; p++){
				close(data->socket[p].fd); //FIXME does python do this on its own?
				Py_XDECREF(data->socket[p].socket);
				Py_XDECREF(data->socket[p].handler);
			}

			//release interval references
			for(p = 0; p <intervals; p++){
				Py_XDECREF(interval[p].reference);
			}

			DBGPF("Shutting down interpreter for instance %s", inst[u]->name);
			//swap to interpreter and end it, GIL is held after this but state is NULL
			PyThreadState_Swap(data->interpreter);
			PyErr_Clear();
			//PyThreadState_Clear(data->interpreter);
			Py_EndInterpreter(data->interpreter);

			free(data);
		}

		//shut down main interpreter
		PyThreadState_Swap(python_main);
		if(Py_FinalizeEx()){
			LOG("Failed to destroy python interpreters");
		}
		PyMem_RawFree(program_name);
	}

	LOG("Backend shut down");
	return 0;
}
