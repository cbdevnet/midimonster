#define BACKEND_NAME "python"

#define PY_SSIZE_T_CLEAN
#include <string.h>
#include <Python.h>
#include "python.h"

#define MMPY_INSTANCE_KEY "midimonster_instance"

/*
 * TODO might want to export the full MM_API set to python at some point
 */

static PyThreadState* python_main = NULL;
static wchar_t* program_name = NULL;

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
		.shutdown = python_shutdown
	};

	//register backend
	if(mm_backend_register(python)){
		LOG("Failed to register backend");
		return 1;
	}
	return 0;
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
		0
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

static int mmpy_exec(PyObject* module) {
	instance** inst = (instance**) PyModule_GetState(module);
	//FIXME actually use interpreter dict (from python 3.8) here at some point
	PyObject* capsule = PyDict_GetItemString(PyThreadState_GetDict(), MMPY_INSTANCE_KEY);
	if(capsule && inst){
		*inst = PyCapsule_GetPointer(capsule, NULL);
		return 0;
	}

	//TODO raise exception
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

	PyEval_ReleaseThread(data->interpreter);
	return 0;
}

static int python_handle(size_t num, managed_fd* fds){
	//TODO implement some kind of intervaling functionality before people get it in their heads to start `import threading`
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
