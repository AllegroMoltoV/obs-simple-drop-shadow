#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

void register_drop_shadow_filter(void);

extern "C" bool obs_module_load(void)
{
	register_drop_shadow_filter();
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

extern "C" void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}

