/*
OBS Audio Video Sync Dock
Copyright (C) 2023 Norihiro Kamae <norihiro@nagater.net>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include "quirc.h"

#include "plugin-macros.generated.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define CONFIG_SECTION_NAME "AudioVideoSyncDock"

void *create_sync_test_dock();
void register_sync_test_output();
void register_sync_test_monitor(bool list);

#if LIBOBS_API_VER <= MAKE_SEMANTIC_VERSION(29, 1, 3)
bool obs_frontend_add_dock_by_id_compat(const char *id, const char *title, void *widget);
#define obs_frontend_add_dock_by_id obs_frontend_add_dock_by_id_compat
#endif

const char *obs_module_name(void)
{
	return obs_module_text("Module.Name");
}

bool obs_module_load(void)
{
#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(31, 0, 0)
	config_t *cfg = obs_frontend_get_global_config();
#else
	config_t *cfg = obs_frontend_get_app_config();
#endif
	bool list_source = cfg && config_get_bool(cfg, CONFIG_SECTION_NAME, "ListMonitor");

	register_sync_test_output();
	register_sync_test_monitor(list_source);
	obs_frontend_add_dock_by_id(ID_PREFIX ".main", obs_module_text("SyncTestDock.Title"), create_sync_test_dock());
	blog(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	blog(LOG_INFO, "quirc (version %s)", quirc_version());
	return true;
}
