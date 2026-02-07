#pragma once

#include <obs-module.h>

struct st_qr_data
{
	uint32_t f = 0;
	uint32_t c = 0;
	uint32_t q_ms = 0;
	uint32_t index = -1;
	uint32_t index_max = 256;
	uint32_t type_flags = 0;
	bool valid = 0;

	bool _decode_kv(char *param)
	{
		char *saveptr;
		char *key = strtok_r(param, "=", &saveptr);
		if (!key || key[1] != 0)
			return false;

		char *val = strtok_r(NULL, "=", &saveptr);
		if (!val)
			return false;

		switch (key[0]) {
		case 'f':
			f = (uint32_t)atoi(val);
			return true;
		case 'c':
			c = (uint32_t)atoi(val);
			return true;
		case 'q':
			q_ms = (uint32_t)atoi(val);
			return true;
		case 'i':
			index = (uint32_t)atoi(val);
			return true;
		case 'I':
			index_max = (uint32_t)atoi(val);
			return true;
		case 't':
			type_flags = (uint32_t)atoi(val);
			return true;
		default:
			/* Ignored */
			return true;
		}

		return false;
	}

	bool check()
	{
		if (f < 10 || 32000 < f) {
			blog(LOG_WARNING, "f: out of range: %u", f);
			return false;
		}
		if (c < 1 || f < c) {
			blog(LOG_WARNING, "c: out of range: %u", c);
			return false;
		}
		if (q_ms < 1 || 1000 < q_ms) {
			blog(LOG_WARNING, "q: out of range: %u", q_ms);
			return false;
		}
		if (index & ~0xFF) {
			blog(LOG_WARNING, "i: out of range: %u", index);
			return false;
		}
		return true;
	}

	bool decode(char *payload)
	{
		valid = false;
		char *saveptr;
		char *param = strtok_r(payload, ",", &saveptr);
		while (param) {
			if (!_decode_kv(param))
				return false;
			param = strtok_r(NULL, ",", &saveptr);
		}
		if (!check())
			return false;
		valid = true;
		return true;
	}
};

struct video_marker_found_s
{
	uint64_t timestamp;
	float score;
	struct st_qr_data qr_data;
};

struct audio_marker_found_s
{
	uint64_t timestamp;
	int index;
	float score;
	uint32_t index_max;
};

struct sync_index
{
	int index = -1;
	uint64_t video_ts = 0;
	uint64_t audio_ts = 0;
	uint32_t index_max = 256;
};

struct frame_drop_event_s
{
	uint64_t timestamp;
	int expected_index;
	int received_index;
	int dropped_count;
	uint64_t total_received;
	uint64_t total_dropped;
};
