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
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QMainWindow>
#include <obs-frontend-api.h>
#include "plugin-macros.generated.h"
#include "sync-test-dock.hpp"

#define ASSERT_THREAD(type)                                                                     \
	do {                                                                                    \
		if (!obs_in_task_thread(type))                                                  \
			blog(LOG_ERROR, "%s: ASSERT_THREAD failed: Expected " #type, __func__); \
	} while (false)

SyncTestDock::SyncTestDock(QWidget *parent) : QFrame(parent)
{
	QVBoxLayout *mainLayout = new QVBoxLayout();
	QGridLayout *topLayout = new QGridLayout();

	int y = 0;

	startButton = new QPushButton(obs_module_text("Button.Start"), this);
	mainLayout->addWidget(startButton);
	connect(startButton, &QPushButton::clicked, this, &SyncTestDock::on_start_stop);

	QLabel *label;
	label = new QLabel(obs_module_text("Label.Latency"), this);
	label->setProperty("class", "text-large");
	topLayout->addWidget(label, y, 0);

	latencyDisplay = new QLabel("-", this);
	latencyDisplay->setObjectName("latencyDisplay");
	latencyDisplay->setProperty("class", "text-large");
	topLayout->addWidget(latencyDisplay, y++, 1);

	latencyPolarity = new QLabel("-", this);
	latencyPolarity->setObjectName("latencyPolarity");
	topLayout->addWidget(latencyPolarity, y++, 1);

	label = new QLabel(obs_module_text("Label.Index"), this);
	topLayout->addWidget(label, y, 0);

	indexDisplay = new QLabel("-", this);
	indexDisplay->setObjectName("indexDisplay");
	topLayout->addWidget(indexDisplay, y++, 1);

	label = new QLabel(obs_module_text("Label.Frequency"), this);
	topLayout->addWidget(label, y, 0);

	frequencyDisplay = new QLabel("-", this);
	frequencyDisplay->setObjectName("frequencyDisplay");
	topLayout->addWidget(frequencyDisplay, y++, 1);

	label = new QLabel(obs_module_text("Label.VideoIndex"), this);
	topLayout->addWidget(label, y, 0);

	videoIndexDisplay = new QLabel("-", this);
	videoIndexDisplay->setObjectName("videoIndexDisplay");
	topLayout->addWidget(videoIndexDisplay, y++, 1);

	label = new QLabel(obs_module_text("Label.AudioIndex"), this);
	topLayout->addWidget(label, y, 0);

	audioIndexDisplay = new QLabel("-", this);
	audioIndexDisplay->setObjectName("audioIndexDisplay");
	topLayout->addWidget(audioIndexDisplay, y++, 1);

	label = new QLabel(obs_module_text("Label.FrameDrops"), this);
	topLayout->addWidget(label, y, 0);

	frameDropDisplay = new QLabel("-", this);
	frameDropDisplay->setObjectName("frameDropDisplay");
	topLayout->addWidget(frameDropDisplay, y++, 1);

	mainLayout->addLayout(topLayout);
	setLayout(mainLayout);
}

SyncTestDock::~SyncTestDock()
{
	if (sync_test) {
		obs_output_stop(sync_test);
		sync_test = nullptr;
	}
}

extern "C" QWidget *create_sync_test_dock()
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	return static_cast<QWidget *>(new SyncTestDock(main_window));
}

#define CD_TO_LOCAL(type, name, get_func) \
	type name;                        \
	if (!get_func(cd, #name, &name))  \
		return;

void SyncTestDock::cb_video_marker_found(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(video_marker_found_s *, data, calldata_get_ptr);
	video_marker_found_s found = *data;

	QMetaObject::invokeMethod(dock, [dock, found]() { dock->on_video_marker_found(found); });
};

void SyncTestDock::cb_audio_marker_found(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(audio_marker_found_s *, data, calldata_get_ptr);
	audio_marker_found_s found = *data;

	QMetaObject::invokeMethod(dock, [dock, found]() { dock->on_audio_marker_found(found); });
};

void SyncTestDock::cb_sync_found(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(sync_index *, data, calldata_get_ptr);
	sync_index found = *data;

	QMetaObject::invokeMethod(dock, [dock, found]() { dock->on_sync_found(found); });
}

void SyncTestDock::cb_frame_drop_detected(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(frame_drop_event_s *, data, calldata_get_ptr);
	frame_drop_event_s found = *data;

	QMetaObject::invokeMethod(dock, [dock, found]() { dock->on_frame_drop_detected(found); });
}

void SyncTestDock::on_start_stop()
{
	if (!sync_test) /* request to start */ {
		OBSOutputAutoRelease o = obs_output_create(OUTPUT_ID, "sync-test-output", nullptr, nullptr);
		if (!o) {
			blog(LOG_ERROR, "Failed to create sync-test-output.");
			return;
		}

		last_video_ix = last_audio_ix = -1;
		missed_video_ix = missed_audio_ix = 0;
		received_video_ix = received_audio_ix = 0;
		received_video_index_max = 256;
		received_audio_index_max = 256;
		audio_index_max = 256;
		total_frame_drops = 0;
		total_frames_seen = 0;
		last_summary_ts = 0;
		sync_count_since_summary = 0;
		latency_sum_since_summary = 0.0;

		auto *sh = obs_output_get_signal_handler(o);
		signal_handler_connect(sh, "video_marker_found", cb_video_marker_found, this);
		signal_handler_connect(sh, "audio_marker_found", cb_audio_marker_found, this);
		signal_handler_connect(sh, "sync_found", cb_sync_found, this);
		signal_handler_connect(sh, "frame_drop_detected", cb_frame_drop_detected, this);

		bool success = obs_output_start(o);

		if (!success)
			latencyPolarity->setText(obs_module_text("Display.Polarity.Failure"));

		if (startButton)
			startButton->setText(obs_module_text("Button.Stop"));

		sync_test = o;
	}
	else /* request to stop */ {
		obs_output_stop(sync_test);
		sync_test = nullptr;

		if (startButton)
			startButton->setText(obs_module_text("Button.Start"));
	}
}

static int missed_markers(int index, int last_index, int max_index)
{
	if (index == last_index + 1 || last_index < 0 || max_index <= 0)
		return 0;
	return (max_index + index - last_index - 1) % max_index;
}

void SyncTestDock::on_video_marker_found(struct video_marker_found_s data)
{
	const int index = data.qr_data.index;
	missed_video_ix += missed_markers(index, last_video_ix, received_video_index_max);
	last_video_ix = index;
	received_video_index_max = data.qr_data.index_max;
	received_video_ix++;
	total_frames_seen++;
	frequencyDisplay->setText(QStringLiteral("%1 Hz").arg(data.qr_data.f));
	int missed = missed_video_ix * 100 / (received_video_ix + missed_video_ix);
	videoIndexDisplay->setText(QStringLiteral("%1 (%2% missed)").arg(index).arg(missed));

	if (total_frame_drops == 0 && total_frames_seen > 0)
		frameDropDisplay->setText(QStringLiteral("0 dropped (0.0%)"));
}

void SyncTestDock::on_audio_marker_found(struct audio_marker_found_s data)
{
	const int index = data.index;
	missed_audio_ix += missed_markers(index, last_audio_ix, received_audio_index_max);
	last_audio_ix = index;
	received_audio_index_max = data.index_max;
	received_audio_ix++;
	int missed = missed_audio_ix * 100 / (received_audio_ix + missed_audio_ix);
	audioIndexDisplay->setText(QStringLiteral("%1 (%2% missed)").arg(index).arg(missed));
}

void SyncTestDock::on_sync_found(sync_index data)
{
	int64_t ts = (int64_t)data.audio_ts - (int64_t)data.video_ts;
	double latency_ms = ts * 1e-6;
	latencyDisplay->setText(QStringLiteral("%1 ms").arg(latency_ms, 2, 'f', 1));
	indexDisplay->setText(QStringLiteral("%1").arg(data.index));
	if (ts > 0)
		latencyPolarity->setText(obs_module_text("Display.Polarity.Positive"));
	else if (ts < 0)
		latencyPolarity->setText(obs_module_text("Display.Polarity.Negative"));

	blog(LOG_DEBUG, "[sync-dock] latency=%.1f ms  index=%d  video_ts=%llu  audio_ts=%llu",
	     latency_ms, data.index,
	     (unsigned long long)data.video_ts,
	     (unsigned long long)data.audio_ts);

	sync_count_since_summary++;
	latency_sum_since_summary += latency_ms;

	if (last_summary_ts == 0)
		last_summary_ts = data.video_ts;

	if (data.video_ts - last_summary_ts >= 10000000000ULL) {
		double avg_latency = latency_sum_since_summary / sync_count_since_summary;
		int64_t total = total_frames_seen + total_frame_drops;
		double drop_rate = total > 0 ? (double)total_frame_drops * 100.0 / (double)total : 0.0;
		blog(LOG_INFO, "[sync-dock] avg_latency=%.1f ms  measurements=%d  total_frames=%" PRId64 "  total_drops=%" PRId64 "  drop_rate=%.1f%%",
		     avg_latency, sync_count_since_summary, total_frames_seen, total_frame_drops, drop_rate);
		sync_count_since_summary = 0;
		latency_sum_since_summary = 0.0;
		last_summary_ts = data.video_ts;
	}
}

void SyncTestDock::on_frame_drop_detected(frame_drop_event_s data)
{
	total_frame_drops = (int64_t)data.total_dropped;
	total_frames_seen = (int64_t)data.total_received;
	double drop_rate = 0.0;
	int64_t total = total_frames_seen + total_frame_drops;
	if (total > 0)
		drop_rate = (double)total_frame_drops * 100.0 / (double)total;
	frameDropDisplay->setText(QStringLiteral("%1 dropped (%2%)").arg(total_frame_drops).arg(drop_rate, 0, 'f', 1));

	blog(LOG_DEBUG, "[sync-dock] frame_drop: dropped=%d expected_idx=%d received_idx=%d total_dropped=%" PRId64 " total_received=%" PRId64 " drop_rate=%.1f%%",
	     data.dropped_count, data.expected_index, data.received_index,
	     total_frame_drops, total_frames_seen, drop_rate);
}
