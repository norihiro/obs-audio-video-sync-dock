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
#include <inttypes.h>
#include <deque>
#include "quirc.h"

#include "plugin-macros.generated.h"

#define N_VIDEO_BUF 8
#define AUDIO_CYCLES 32
#define AUDIO_FREQUENCY 442
#define N_CORNERS 4

struct st_video_buf
{
	float sum;
	uint64_t timestamp;
};

struct st_audio_buffer
{
	std::deque<std::pair<int16_t, int16_t>> buffer0;
	std::deque<std::pair<int16_t, int16_t>> buffer1;
	int32_t sum0r = 0, sum0i = 0;
	int32_t sum1r = 0, sum1i = 0;

	void push_back(int16_t xr, int16_t xi, size_t length)
	{
		buffer0.push_back(std::make_pair(xr, xi));
		sum0r += xr;
		sum0i += xi;

		if (buffer0.size() <= length)
			return;

		int16_t vr = buffer0.front().first;
		int16_t vi = buffer0.front().second;
		buffer0.pop_front();
		sum0r -= vr;
		sum0i -= vi;

		buffer1.push_back(std::make_pair(vr, vi));
		sum1r += vr;
		sum1i += vi;

		if (buffer1.size() <= length)
			return;

		vr = buffer1.front().first;
		vi = buffer1.front().second;
		buffer1.pop_front();
		sum1r -= vr;
		sum1i -= vi;
	};
};

struct marker_finder
{
	uint64_t cand_ts = 0;
	uint64_t last_ts = 0;

	float cand_score = 0.0f;
	float last_score = 0.0f;

	static float dumping(uint64_t ts_last, uint64_t ts_next)
	{
		if (ts_next <= ts_last)
			return 1.0f;
		if (ts_next - ts_last > 2000000000)
			return 0.0f;
		float f = (ts_next - ts_last) * 0.5e-9f;
		return 1.0f - f * f;
	}

	bool append(float score, uint64_t ts, uint64_t wait_ts)
	{
		if (score > cand_score * dumping(cand_ts, ts)) {
			cand_ts = ts;
			cand_score = score;
			/* When updating candidate, which is the recent peak,
			 * there might be larger score coming next. */
			return false;
		}

		if (cand_ts + wait_ts > ts)
			return false;

		if (cand_ts > last_ts && cand_score > last_score * dumping(last_ts, cand_ts)) {
			last_ts = cand_ts;
			last_score = cand_score;
			return true;
		}

		return false;
	}
};

struct corner_type
{
	uint32_t x, y;
	uint32_t r;
	bool w;
};

struct sync_test_output
{
	obs_output_t *context;

	uint64_t start_ts = 0;

	uint32_t video_width = 0, video_height = 0;
	uint32_t video_pixelsize = 0;
	uint32_t video_pixeloffset = 0;

	uint32_t audio_sample_rate = 0;
	size_t audio_channels = 0;

	struct quirc *qr = nullptr;
	uint32_t qr_step;
	struct corner_type qr_corners[N_CORNERS];
	size_t qr_corner_size = 0;

	struct st_video_buf video_buf[N_VIDEO_BUF];
	size_t video_buf_start = 0, video_buf_end = 0, video_buf_size = 0;
	struct marker_finder video_marker_finder;

	struct st_audio_buffer audio_buffer[MAX_AV_PLANES];
	struct marker_finder audio_marker_finder[MAX_AV_PLANES];

	~sync_test_output()
	{
		if (qr)
			quirc_destroy(qr);
	}
};

static const char *st_get_name(void *)
{
	return "sync-test-output";
}

static void *st_create(obs_data_t *, obs_output_t *output)
{
	static const char *signals[] = {
		"void video_marker_found(int timestamp, float score)",
		"void audio_marker_found(int channel, int timestamp, float score)",
		NULL,
	};
	signal_handler_add_array(obs_output_get_signal_handler(output), signals);

	auto *st = new sync_test_output;
	st->context = output;

	return st;
}

static void st_destroy(void *data)
{
	auto *st = (struct sync_test_output *)data;
	delete st;
}

static bool st_start(void *data)
{
	auto *st = (struct sync_test_output *)data;

	const video_t *video = obs_output_video(st->context);
	if (!video) {
		blog(LOG_ERROR, "no video");
		return false;
	}
	const audio_t *audio = obs_output_audio(st->context);
	if (!audio) {
		blog(LOG_ERROR, "no audio");
		return false;
	}

	st->video_width = video_output_get_width(video);
	st->video_height = video_output_get_height(video);
	enum video_format video_format = video_output_get_format(video);
	switch (video_format) {
	case VIDEO_FORMAT_NV12:
		st->video_pixelsize = 1;
		st->video_pixeloffset = 0;
		break;
	case VIDEO_FORMAT_I444:
	case VIDEO_FORMAT_I420:
	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX:
	default:
		blog(LOG_ERROR, "unsupported pixel format %d", video_format);
		return false;
	}

	uint32_t qr_width = st->video_width;
	uint32_t qr_height = st->video_height;
	st->qr_step = 1;
	while (qr_width * qr_height > 640 * 480) {
		qr_width /= 2;
		qr_height /= 2;
		st->qr_step *= 2;
	}
	if (!st->qr)
		st->qr = quirc_new();
	if (!st->qr) {
		blog(LOG_ERROR, "failed to create QR code encoding context");
		return false;
	}
	if (quirc_resize(st->qr, qr_width, qr_height) < 0) {
		blog(LOG_ERROR, "failed to set-up QR code encoding context");
		return false;
	}

	st->audio_sample_rate = audio_output_get_sample_rate(audio);
	st->audio_channels = audio_output_get_channels(audio);

	obs_output_begin_data_capture(st->context, OBS_OUTPUT_VIDEO | OBS_OUTPUT_AUDIO);

	return true;
}

static void st_stop(void *data, uint64_t)
{
	auto *st = (struct sync_test_output *)data;

	obs_output_end_data_capture(st->context);
}

template<typename T> T sq(T x)
{
	return x * x;
}

static inline int qrcode_length(const struct quirc_point *corners)
{
	auto l01 = hypotf((float)(corners[0].x - corners[1].x), (float)(corners[0].y - corners[1].y));
	auto l03 = hypotf((float)(corners[0].x - corners[3].x), (float)(corners[0].y - corners[3].y));
	return (int)((l01 + l03) / 2.0f);
}

static void st_raw_video_qrcode_decode(struct sync_test_output *st, struct video_data *frame)
{
	int w, h;
	auto qr = st->qr;
	uint8_t *qrbuf = quirc_begin(qr, &w, &h);

	const auto qr_step = st->qr_step;
	const auto pixelsize = st->video_pixelsize * qr_step;
	const uint8_t *linedata = frame->data[0] + frame->linesize[0] * (qr_step / 2);
	auto *ptr = qrbuf;
	for (int y = 0; y < h; y++) {
		const uint8_t *data = linedata + st->video_pixeloffset + st->video_pixelsize * (qr_step / 2);
		for (int x = 0; x < w; x++) {
			*ptr++ = *data;
			data += pixelsize;
		}

		linedata += frame->linesize[0] * qr_step;
	}

	quirc_end(qr);

	int num_codes = quirc_count(qr);

	for (int i = 0; i < num_codes; i++) {
		struct quirc_code code;
		struct quirc_data data;
		quirc_extract(qr, i, &code);
		auto err = quirc_decode(&code, &data);
		if (!err) {
			st->qr_corner_size = 4;
			int r = qrcode_length(code.corners) * 3 / 4;
			for (int j = 0; j < 4; j++) {
				st->qr_corners[j].x = code.corners[j].x * st->qr_step;
				st->qr_corners[j].y = code.corners[j].y * st->qr_step;
				st->qr_corners[j].r = r;
				st->qr_corners[j].w = j == 1 || j == 2; // TODO: decode from the code
			}
		}
	}
}

static void st_raw_video_insert_to_video_buf(struct sync_test_output *st, struct video_data *frame)
{
	if (st->qr_corner_size != 4)
		return;

	int64_t sum = 0;

	const uint8_t *linedata = frame->data[0];

	for (size_t i = 0; i < N_CORNERS; i++) {
		const struct corner_type c = st->qr_corners[i];
		uint32_t x0 = c.x > c.r ? c.x - c.r : 0;
		uint32_t x1 = std::min(c.x + c.r, st->video_height);
		uint32_t y0 = c.y > c.r ? c.y - c.r : 0;
		uint32_t y1 = std::min(c.y + c.r, st->video_height);

		for (uint32_t y = y0; y < y1; y++) {
			const uint8_t *data = linedata + frame->linesize[0] * y + st->video_pixeloffset;

			for (uint32_t x = x0; x < x1; x++) {
				if (sq(x - c.x) + sq(y - c.y) > sq(c.r))
					continue;

				uint8_t v = data[st->video_pixelsize * x];
				if (c.w)
					sum += v;
				else
					sum -= v;
			}
		}
	}

	st->video_buf[st->video_buf_end].sum = sum / (127.5f * st->video_width * st->video_height);
	st->video_buf[st->video_buf_end].timestamp = frame->timestamp;
	if (st->video_buf_size < N_VIDEO_BUF) {
		st->video_buf_end = (st->video_buf_end + 1) % N_VIDEO_BUF;
		st->video_buf_size++;
	}
	else {
		st->video_buf_start = st->video_buf_end = (st->video_buf_start + 1) % N_VIDEO_BUF;
	}
}

static void st_raw_video_search_marker(struct sync_test_output *st)
{
	if (st->video_buf_size != N_VIDEO_BUF)
		return;

	uint64_t timestamp = 0;
	float score = 0;

	for (size_t i = 0; i < N_VIDEO_BUF; i++) {
		if (i == N_VIDEO_BUF / 2) {
			timestamp = st->video_buf[(i + st->video_buf_start) % N_VIDEO_BUF].timestamp;
		}
		float v = st->video_buf[(i + st->video_buf_start) % N_VIDEO_BUF].sum;
		score += i < N_VIDEO_BUF / 2 ? -v : v;
	}

	// blog(LOG_INFO, "st_raw_video-plot: %.03f %f", (timestamp - st->start_ts) * 1e-9, score);

	if (st->video_marker_finder.append(
		    score, timestamp,
		    st->video_buf[(st->video_buf_start + N_VIDEO_BUF - 1) / N_VIDEO_BUF].timestamp -
			    st->video_buf[st->video_buf_start].timestamp)) {
		uint8_t stack[128];
		struct calldata cd;
		calldata_init_fixed(&cd, stack, sizeof(stack));
		auto *sh = obs_output_get_signal_handler(st->context);

		calldata_set_int(&cd, "timestamp", st->video_marker_finder.last_ts - st->start_ts);
		calldata_set_float(&cd, "score", st->video_marker_finder.last_score);
		signal_handler_signal(sh, "video_marker_found", &cd);
	}
}

static void st_raw_video(void *data, struct video_data *frame)
{
	auto *st = (struct sync_test_output *)data;

	if (!st->video_pixelsize)
		return;

	if (!st->start_ts)
		st->start_ts = frame->timestamp;

	st_raw_video_qrcode_decode(st, frame);
	st_raw_video_insert_to_video_buf(st, frame);
	st_raw_video_search_marker(st);
}

static void st_raw_audio(void *data, struct audio_data *frames)
{
	auto *st = (struct sync_test_output *)data;

	if (!st->start_ts)
		return;

	float phase = (frames->timestamp % 1000000000) * (float)(1e-9 * 2 * M_PI * AUDIO_FREQUENCY);
	float phase_step = (float)(2 * M_PI * AUDIO_FREQUENCY) / st->audio_sample_rate;
	size_t buffer_length = (size_t)(st->audio_sample_rate * AUDIO_CYCLES / AUDIO_FREQUENCY);
	uint64_t buffer_ns = util_mul_div64(buffer_length, 1000000000ULL, st->audio_sample_rate);
	uint64_t ts0 = frames->timestamp - buffer_ns;

	for (uint32_t i = 0; i < frames->frames; i++) {
		float osc0 = sinf(phase + phase_step * i);
		float osc1 = cosf(phase + phase_step * i);
		uint64_t ts = ts0 + util_mul_div64(i, 1000000000ULL, st->audio_sample_rate);

		for (size_t ch = 0; ch < st->audio_channels; ch++) {
			float v = ((float *)frames->data[ch])[i];
			int16_t vr = (int16_t)(v * osc0 * 32767.0f);
			int16_t vi = (int16_t)(v * osc1 * 32767.0f);
			st->audio_buffer[ch].push_back(vr, vi, buffer_length);

			float detr = (float)(st->audio_buffer[ch].sum0r - st->audio_buffer[ch].sum1r);
			float deti = (float)(st->audio_buffer[ch].sum0i - st->audio_buffer[ch].sum1i);
			float det = hypotf(detr, deti) / (32767.0f * buffer_length);
			// if (ch == 0) blog(LOG_INFO, "st_raw_audio-plot: %.05f %f %.05f %f", (ts + buffer_ns - st->start_ts) * 1e-9, v, (ts - st->start_ts) * 1e-9, det);

			if (st->audio_marker_finder[ch].append(det, ts, buffer_ns)) {
				uint8_t stack[256];
				struct calldata cd;
				calldata_init_fixed(&cd, stack, sizeof(stack));
				auto *sh = obs_output_get_signal_handler(st->context);

				calldata_set_int(&cd, "channel", ch);
				calldata_set_int(&cd, "timestamp", st->audio_marker_finder[ch].last_ts - st->start_ts);
				calldata_set_float(&cd, "score", st->audio_marker_finder[ch].last_score);
				signal_handler_signal(sh, "audio_marker_found", &cd);
			}
		}
	}
}

extern "C" void register_sync_test_output()
{
	struct obs_output_info info = {};
	info.id = ID_PREFIX "output";
	info.flags = OBS_OUTPUT_AV;
	info.get_name = st_get_name;
	info.create = st_create;
	info.destroy = st_destroy;
	info.start = st_start;
	info.stop = st_stop;
	info.raw_video = st_raw_video;
	info.raw_audio = st_raw_audio;

	obs_register_output(&info);
}
