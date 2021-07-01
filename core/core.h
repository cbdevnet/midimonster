/* 
 * MIDIMonster frontend API
 * 
 * These APIs expose the core as a linkable module. Frontends will use these calls
 * as primary interface to interact with the MIDIMonster core.
 *
 * The lifecycle is as follows:
 *
 * 	* Initially, only the following API calls are valid:
 * 			config_add_override()
 * 			core_initialize()
 * 		This allows the frontend to configure overrides for any configuration
 * 		loaded later (e.g. by parsing command line arguments) before initializing
 * 		the core.
 * 	* Calling core_initialize() attaches all backend modules to the system and
 * 		performs platform specific startup operations. From this point on,
 * 		core_shutdown() must be called before terminating the frontend.
 * 		All frontend API calls except `core_iteration` are now valid.
 * 		The core is now in the configuration stage in which the frontend
 * 		will push any configuration files.
 * 	* Calling core_start() marks the transition from the configuration phase
 * 		to the translation phase. The core will activate any configured backends
 * 		and provide them with the information required to connect to their data
 * 		sources and sinks. In this stage, only the following API calls are valid:
 * 			core_iteration()
 * 			core_shutdown()
 * 	* The frontend will now repeatedly call core_iteration() to process any incoming
 * 		events. This API will block execution until either one or more events have
 * 		been registered or an internal timeout expires.
 *	* Calling core_shutdown() releases all memory allocated by the core and any
 *		attached modules or plugins, including all configuration, overrides,
 *		mappings, statistics, etc. The core is now ready to exit or be
 *		reinitialized using core_initialize().
 */

int core_initialize();
int core_start();
int core_iteration();
void core_shutdown();

/* Public backend API */
MM_API uint64_t mm_timestamp();
MM_API int mm_manage_fd(int new_fd, char* back, int manage, void* impl);
