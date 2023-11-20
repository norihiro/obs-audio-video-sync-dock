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
#include <list>
#include <stdlib.h>
#include <mutex>
#include <complex>
#include "quirc.h"
#include "sync-test-output.hpp"
#include "peak-finder.hpp"

#include "plugin-macros.generated.h"

#define N_VIDEO_BUF 4
#define N_CORNERS 4

#define N_AUDIO_SYMBOLS 16
#define N_SYMBOL_BUFFER 16

#define TYPE_AUDIO_START_AT_SYNC 1

struct st_video_buf
{
	float sum;
	uint64_t timestamp;
};

struct st_audio_buffer
{
	std::deque<std::pair<int32_t, int32_t>> buffer;

	void push_back(int16_t xr, int16_t xi, size_t length)
	{
		int32_t vr = xr, vi = xi;
		if (buffer.size()) {
			vr += buffer.back().first;
			vi += buffer.back().second;
		}
		buffer.push_back(std::make_pair(vr, vi));

		if (buffer.size() <= length)
			return;

		buffer.pop_front();
	};

	std::pair<int32_t, int32_t> sum(size_t n_from_last)
	{
		if (n_from_last >= buffer.size())
			return buffer[0];
		return buffer[buffer.size() - n_from_last - 1];
	}
};

std::pair<int32_t, int32_t> operator-(std::pair<int32_t, int32_t> a, std::pair<int32_t, int32_t> b)
{
	return std::make_pair(a.first - b.first, a.second - b.second);
}

std::complex<float> int16_to_complex(std::pair<int32_t, int32_t> x)
{
	return std::complex<float>((float)x.first, (float)x.second) / 32767.0f;
}

struct corner_type
{
	uint32_t x, y;
	uint32_t r = 0;
};

struct sync_index
{
	int index = -1;
	uint64_t video_ts = 0;
	uint64_t audio_ts = 0;
};

struct sync_test_output
{
	obs_output_t *context;

	/* Configuration from OBS output context */
	uint32_t video_width = 0, video_height = 0;
	uint32_t video_pixelsize = 0;
	uint32_t video_pixeloffset = 0;

	uint32_t audio_sample_rate = 0;
	size_t audio_channels = 0;

	/* Sync pattern detection from video */
	uint64_t start_ts = 0;

	struct quirc *qr = nullptr;
	uint32_t qr_step;
	struct corner_type qr_corners[N_CORNERS];
	st_qr_data qr_data;

	struct st_video_buf video_buf[N_VIDEO_BUF];
	uint32_t video_buf_start = 0, video_buf_end = 0, video_buf_size = 0;
	struct peak_finder video_marker_finder;

	/* Sync pattern detection from audio */
	struct st_audio_buffer audio_buffer;
	struct peak_finder audio_marker_finder;

	/* Multiplex sync pattern detection result */
	std::list<struct sync_index> sync_indices;

	std::mutex mutex;

	/* Audio pattern information from video to audio */
	uint32_t f = 0;
	uint32_t c = 0;

	uint32_t f_last = 0;
	uint32_t c_last = 0;

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
		"void video_marker_found(ptr data)",
		"void audio_marker_found(ptr data)",
		"void qrcode_found(int timestamp, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3)",
		"void sync_found(int index, int video_ts, int audio_ts, float score)",
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
		// (x0, y0): top left
		// (x1, y1): top right
		// (x2, y2): bottom right
		// (x3, y3): bottom left

		struct quirc_code code;
		struct quirc_data data;
		quirc_extract(qr, i, &code);
		auto err = quirc_decode(&code, &data);
		if (err)
			continue;

		uint8_t stack[384];
		struct calldata cd;
		calldata_init_fixed(&cd, stack, sizeof(stack));
		auto *sh = obs_output_get_signal_handler(st->context);

		calldata_set_int(&cd, "timestamp", frame->timestamp - st->start_ts);
		calldata_set_int(&cd, "x0", code.corners[0].x * st->qr_step);
		calldata_set_int(&cd, "y0", code.corners[0].y * st->qr_step);
		calldata_set_int(&cd, "x1", code.corners[1].x * st->qr_step);
		calldata_set_int(&cd, "y1", code.corners[1].y * st->qr_step);
		calldata_set_int(&cd, "x2", code.corners[2].x * st->qr_step);
		calldata_set_int(&cd, "y2", code.corners[2].y * st->qr_step);
		calldata_set_int(&cd, "x3", code.corners[3].x * st->qr_step);
		calldata_set_int(&cd, "y3", code.corners[3].y * st->qr_step);
		signal_handler_signal(sh, "qrcode_found", &cd);

		int r = qrcode_length(code.corners) * 3 / 8;
		for (int j = 0; j < 4; j++) {
			st->qr_corners[j].x = code.corners[j].x * st->qr_step;
			st->qr_corners[j].y = code.corners[j].y * st->qr_step;
			st->qr_corners[j].r = r;
		}

		data.payload[QUIRC_MAX_PAYLOAD - 1] = 0;
		if (!st->qr_data.decode((char *)data.payload))
			continue;

		if (st->qr_data.f > 0 && st->qr_data.c > 0) {
			std::unique_lock<std::mutex> lock(st->mutex);
			st->f = st->qr_data.f;
			st->c = st->qr_data.c;
		}
	}
}

static void st_raw_video_insert_to_video_buf(struct sync_test_output *st, struct video_data *frame)
{
	int64_t sum = 0;

	const uint8_t *linedata = frame->data[0];

	for (size_t i = 0; i < N_CORNERS; i++) {
		const struct corner_type c = st->qr_corners[i];
		if (c.r == 0)
			return;
		uint32_t x0 = c.x > c.r ? c.x - c.r : 0;
		uint32_t x1 = std::min(c.x + c.r, st->video_height);
		uint32_t y0 = c.y > c.r ? c.y - c.r : 0;
		uint32_t y1 = std::min(c.y + c.r, st->video_height);
		uint32_t sq_r = sq(c.r);

		for (uint32_t y = y0; y < y1; y++) {
			const uint8_t *data = linedata + frame->linesize[0] * y + st->video_pixeloffset;

			for (uint32_t x = x0; x < x1; x++) {
				if (sq(x - c.x) + sq(y - c.y) > sq_r)
					continue;

				uint8_t v = data[st->video_pixelsize * x];
				if (i & 1)
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

static void sync_index_found(struct sync_test_output *st, int index, uint64_t ts, bool is_video)
{
	std::unique_lock<std::mutex> lock(st->mutex);
	for (auto it = st->sync_indices.begin(); it != st->sync_indices.end();) {

		if ((it->video_ts && is_video) || (it->audio_ts && !is_video)) {
			if (((index - it->index) & 0xFF) > 0x7F) {
				st->sync_indices.erase(it++);
				continue;
			}
		}

		if (it->index != index) {
			it++;
			continue;
		}
		if ((it->video_ts && !is_video) || (it->audio_ts && is_video)) {
			(is_video ? it->video_ts : it->audio_ts) = ts;

			uint8_t stack[512];
			struct calldata cd;
			calldata_init_fixed(&cd, stack, sizeof(stack));
			auto *sh = obs_output_get_signal_handler(st->context);

			calldata_set_int(&cd, "index", index);
			calldata_set_int(&cd, "video_ts", (long long)it->video_ts);
			calldata_set_int(&cd, "audio_ts", (long long)it->audio_ts);
			calldata_set_float(&cd, "score", 0.0);
			signal_handler_signal(sh, "sync_found", &cd);

			st->sync_indices.erase(it);
			return;
		}

		/* Remove the old one. Later, insert the new one to the end */
		st->sync_indices.erase(it);
		break;
	}

	while (st->sync_indices.size() >= 128)
		st->sync_indices.erase(st->sync_indices.begin());

	auto &ref = st->sync_indices.emplace_back();
	ref.index = index;
	(is_video ? ref.video_ts : ref.audio_ts) = ts;
}

static void st_raw_video_search_marker(struct sync_test_output *st)
{
	if (st->video_buf_size != N_VIDEO_BUF)
		return;

	if (!st->qr_data.valid)
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
		uint8_t stack[64];
		struct calldata cd;
		calldata_init_fixed(&cd, stack, sizeof(stack));
		auto *sh = obs_output_get_signal_handler(st->context);

		struct video_marker_found_s data;
		data.timestamp = st->video_marker_finder.last_ts - st->start_ts;
		data.score = st->video_marker_finder.last_score;
		data.qr_data = st->qr_data;

		calldata_set_ptr(&cd, "data", &data);
		signal_handler_signal(sh, "video_marker_found", &cd);

		sync_index_found(st, st->qr_data.index, st->video_marker_finder.last_ts - st->start_ts, true);

		st->qr_data.valid = false;
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

static inline void st_raw_audio_decode_data(struct sync_test_output *st, std::complex<float> phase, uint64_t ts)
{
	uint32_t symbol_num = st->audio_sample_rate * st->c_last;
	uint32_t symbol_den = st->f_last;

	struct audio_marker_found_s data;
	data.timestamp = ts - st->start_ts;
	data.index = st->qr_data.index;
	data.score = 0.0f;

	float data_flt[8];
	uint8_t index = 0;
	for (int i = 0; i < 8; i++) {
		auto s0 = st->audio_buffer.sum(symbol_num * i / symbol_den);
		auto s1 = st->audio_buffer.sum(symbol_num * (i + 1) / symbol_den);
		auto x = int16_to_complex(s0 - s1);
		auto r = (x / phase).real();
		bool bit = r > 0.0f ? true : false;
		if (bit)
			index |= 1 << i;
		data.score += std::abs(r);
		data_flt[i] = r;
	}

	blog(LOG_INFO, "st_raw_audio_decode_data: Decoded %u rcv_ts=%.03f %f %f %f %f %f %f %f %f", index,
	     (ts - st->start_ts) * 1e-9, data_flt[7], data_flt[6], data_flt[5], data_flt[4], data_flt[3], data_flt[2],
	     data_flt[1], data_flt[0]);

	uint8_t stack[64];
	struct calldata cd;
	calldata_init_fixed(&cd, stack, sizeof(stack));
	auto *sh = obs_output_get_signal_handler(st->context);

	calldata_set_ptr(&cd, "data", &data);
	signal_handler_signal(sh, "audio_marker_found", &cd);

	sync_index_found(st, index, ts - st->start_ts, false);
}

static inline void st_raw_audio_test_preamble(struct sync_test_output *st, uint64_t ts, float v0)
{
	uint32_t c = st->c_last;
	uint32_t f = st->f_last;
	uint64_t symbol_ns = util_mul_div64(c, 1000000000ULL, f);
	size_t buffer_length = (size_t)(st->audio_sample_rate * c * N_SYMBOL_BUFFER / f);

	/* Test the preamble pattern 0xF0  */
	auto s0 = st->audio_buffer.sum(0);
	auto s4 = st->audio_buffer.sum(buffer_length * 4 / N_SYMBOL_BUFFER);
	auto s8 = st->audio_buffer.sum(buffer_length * 8 / N_SYMBOL_BUFFER);

	float det = std::abs(int16_to_complex(s4 - s0) - int16_to_complex(s8 - s4));

	UNUSED_PARAMETER(v0);
	// blog(LOG_INFO, "st_raw_audio-plot: %.05f %f %f", (ts - st->start_ts) * 1e-9, v0, det);

	if (st->audio_marker_finder.append(det, ts, symbol_ns * 8)) {
		auto s8 = st->audio_buffer.sum(buffer_length * 8 / N_SYMBOL_BUFFER);
		auto s12 = st->audio_buffer.sum(buffer_length * 12 / N_SYMBOL_BUFFER);
		auto s16 = st->audio_buffer.sum(buffer_length * 16 / N_SYMBOL_BUFFER);

		auto x = int16_to_complex(s12 - s16) - int16_to_complex(s8 - s12);

		if (st->qr_data.type_flags & TYPE_AUDIO_START_AT_SYNC)
			ts -= symbol_ns * N_AUDIO_SYMBOLS;
		else
			ts -= symbol_ns * (N_AUDIO_SYMBOLS / 2);

		st_raw_audio_decode_data(st, x / std::abs(x), ts);
	}
}

static void st_raw_audio(void *data, struct audio_data *frames)
{
	auto *st = (struct sync_test_output *)data;

	if (!st->start_ts)
		return;

	std::unique_lock<std::mutex> lock(st->mutex);
	uint32_t f = st->f;
	uint32_t c = st->c;
	lock.unlock();

	if (f <= 0 || c <= 0)
		return;

	if (f != st->f_last || c != st->c_last) {
		st->f_last = f;
		st->c_last = c;
		st->audio_buffer.buffer.clear();
	}

	float phase = (frames->timestamp % 1000000000) * (float)(1e-9 * 2 * M_PI * f);
	float phase_step = (float)(2 * M_PI * f) / st->audio_sample_rate;
	size_t buffer_length = (size_t)(st->audio_sample_rate * c * N_SYMBOL_BUFFER / f);

	for (uint32_t i = 0; i < frames->frames; i++) {
		float osc0 = sinf(phase + phase_step * i);
		float osc1 = cosf(phase + phase_step * i);
		uint64_t ts = frames->timestamp + util_mul_div64(i, 1000000000ULL, st->audio_sample_rate);

		float v0 = ((float *)frames->data[0])[i];
		float v1 = st->audio_channels >= 2 ? ((float *)frames->data[1])[i] : 0.0f;
		int16_t vr = (int16_t)((v0 * osc0 - v1 * osc1) * 16383.0f);
		int16_t vi = (int16_t)((v0 * osc1 + v1 * osc0) * 16383.0f);
		st->audio_buffer.push_back(vr, vi, buffer_length);

		if (st->audio_buffer.buffer.size() < buffer_length)
			continue;

		st_raw_audio_test_preamble(st, ts, v0);
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
