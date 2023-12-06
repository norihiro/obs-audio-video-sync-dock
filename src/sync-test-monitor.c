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
#include <util/threading.h>
#include <inttypes.h>

#include "plugin-macros.generated.h"

#define QR_RECT_COLOR 0xFF00FF00

struct st_monitor_s
{
	obs_weak_output_t *weak;

	pthread_mutex_t mutex;
	int x0, y0, x1, y1, x2, y2, x3, y3;
	volatile bool got_data;
};

static const char *get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Monitor.Name");
}

static bool find_output_cb(void *data, obs_output_t *o)
{
	obs_output_t **res = data;
	const char *id = obs_output_get_id(o);
	if (id && strcmp(id, OUTPUT_ID) == 0) {
		*res = o;
		return false;
	}
	return true;
}

static void cb_qrcode_found(void *param, calldata_t *data)
{
	struct st_monitor_s *s = param;
	long long timestamp;
	if (!calldata_get_int(data, "timestamp", &timestamp))
		return;

	pthread_mutex_lock(&s->mutex);
#define GET_INT(name)                                     \
	do {                                              \
		long long t;                              \
		if (!calldata_get_int(data, #name, &t)) { \
			pthread_mutex_unlock(&s->mutex);  \
			return;                           \
		}                                         \
		s->name = (int)t;                         \
	} while (false);

	GET_INT(x0);
	GET_INT(y0);
	GET_INT(x1);
	GET_INT(y1);
	GET_INT(x2);
	GET_INT(y2);
	GET_INT(x3);
	GET_INT(y3);
	pthread_mutex_unlock(&s->mutex);

	s->got_data = true;

#undef GET_INT
}

static void find_output(struct st_monitor_s *s)
{
	if (s->weak)
		return;

	obs_output_t *o = NULL;
	obs_enum_outputs(find_output_cb, &o);
	if (!o)
		return;

	s->weak = obs_output_get_weak_output(o);
	signal_handler_t *sh = obs_output_get_signal_handler(o);
	signal_handler_connect(sh, "qrcode_found", cb_qrcode_found, s);
}

static void release_output(struct st_monitor_s *s)
{
	if (!s->weak)
		return;

	obs_output_t *o = obs_weak_output_get_output(s->weak);
	obs_weak_output_release(s->weak);
	s->weak = NULL;

	if (!o)
		return;

	signal_handler_t *sh = obs_output_get_signal_handler(o);
	signal_handler_disconnect(sh, "qrcode_found", cb_qrcode_found, s);
	obs_output_release(o);
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	UNUSED_PARAMETER(source);
	struct st_monitor_s *s = bzalloc(sizeof(struct st_monitor_s));

	pthread_mutex_init(&s->mutex, NULL);

	return s;
}

static void destroy(void *data)
{
	struct st_monitor_s *s = data;

	if (s->weak)
		release_output(s);

	pthread_mutex_destroy(&s->mutex);

	bfree(s);
}

static inline uint32_t get_width_height(struct st_monitor_s *s, uint32_t (*func)(const obs_output_t *))
{
	uint32_t ret = 0;
	obs_output_t *o = obs_weak_output_get_output(s->weak);
	if (o) {
		ret = func(o);
		obs_output_release(o);
	}
	return ret;
}

static uint32_t get_width(void *data)
{
	return get_width_height(data, obs_output_get_width);
}

static uint32_t get_height(void *data)
{
	return get_width_height(data, obs_output_get_height);
}

static void video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct st_monitor_s *s = data;
	obs_output_t *o;

	if (!s->weak) {
		find_output(s);
	}
	else if ((o = obs_weak_output_get_output(s->weak))) {
		obs_output_release(o);
	}
	else {
		release_output(s);
		find_output(s);
	}
}

static void video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct st_monitor_s *s = data;

	if (s->got_data) {
		pthread_mutex_lock(&s->mutex);
		int x0 = s->x0, y0 = s->y0;
		int x1 = s->x1, y1 = s->y1;
		int x2 = s->x2, y2 = s->y2;
		int x3 = s->x3, y3 = s->y3;
		pthread_mutex_unlock(&s->mutex);

		gs_effect_t *e = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_effect_set_color(gs_effect_get_param_by_name(e, "color"), QR_RECT_COLOR);
		while (gs_effect_loop(e, "Solid")) {
			gs_render_start(false);
			gs_vertex2f((float)x0, (float)y0);
			gs_vertex2f((float)x1, (float)y1);
			gs_vertex2f((float)x2, (float)y2);
			gs_vertex2f((float)x3, (float)y3);
			gs_vertex2f((float)x0, (float)y0);
			gs_render_stop(GS_LINESTRIP);
		}
	}
}

void register_sync_test_monitor(bool list)
{
	struct obs_source_info info = {
		.id = MONITOR_ID,
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
		.get_name = get_name,
		.create = create,
		.destroy = destroy,
		.video_tick = video_tick,
		.video_render = video_render,
		.get_width = get_width,
		.get_height = get_height,
	};

	if (!list)
		info.output_flags |= OBS_SOURCE_CAP_DISABLED;

	obs_register_source(&info);
}
