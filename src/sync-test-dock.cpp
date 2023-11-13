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

	startButton = new QPushButton(obs_module_text("Button.Start"), this);
	mainLayout->addWidget(startButton);
	connect(startButton, &QPushButton::clicked, this, &SyncTestDock::on_start_stop);

	QLabel *latencyLabel = new QLabel(obs_module_text("Label.Latency"), this);
	topLayout->addWidget(latencyLabel, 0, 0);

	latencyDisplay = new QLabel("-", this);
	topLayout->addWidget(latencyDisplay, 0, 1);

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

static void cb_video_marker_found(void *param, calldata_t *data)
{
	auto *dock = (SyncTestDock *)param;

	long long timestamp;
	if (!calldata_get_int(data, "timestamp", &timestamp))
		return;
	double score;
	if (!calldata_get_float(data, "score", &score))
		return;

	QMetaObject::invokeMethod(dock, "on_video_marker_found", Q_ARG(uint64_t, timestamp), Q_ARG(double, score));
};

static void cb_audio_marker_found(void *param, calldata_t *data)
{
	auto *dock = (SyncTestDock *)param;

	long long channel;
	if (!calldata_get_int(data, "channel", &channel))
		return;
	long long timestamp;
	if (!calldata_get_int(data, "timestamp", &timestamp))
		return;
	double score;
	if (!calldata_get_float(data, "score", &score))
		return;

	QMetaObject::invokeMethod(dock, "on_audio_marker_found", Q_ARG(size_t, channel), Q_ARG(uint64_t, timestamp),
				  Q_ARG(double, score));
};

void SyncTestDock::on_start_stop()
{
	if (!started) /* request to start */ {
		OBSOutputAutoRelease o = obs_output_create(ID_PREFIX "output", "sync-test-output", nullptr, nullptr);
		if (!o) {
			blog(LOG_ERROR, "Failed to create sync-test-output.");
			return;
		}

		auto *sh = obs_output_get_signal_handler(o);
		signal_handler_connect(sh, "video_marker_found", cb_video_marker_found, this);
		signal_handler_connect(sh, "audio_marker_found", cb_audio_marker_found, this);

		obs_output_start(o);

		if (startButton)
			startButton->setText(obs_module_text("Button.Stop"));

		sync_test = o;
		started = true;
	}
	else /* request to stop */ {
		if (sync_test) {
			obs_output_stop(sync_test);
			sync_test = nullptr;
		}
		if (startButton)
			startButton->setText(obs_module_text("Button.Start"));
		started = false;
	}
}

void SyncTestDock::on_video_marker_found(uint64_t timestamp, double)
{
	blog(LOG_INFO, "%s: %.05f", __func__, timestamp * 1e-9);
	ASSERT_THREAD(OBS_TASK_UI);
	last_video_ts = timestamp;
}

void SyncTestDock::on_audio_marker_found(size_t channel, uint64_t timestamp, double)
{
	blog(LOG_INFO, "%s: [%zu] %.05f", __func__, channel, timestamp * 1e-9);
	ASSERT_THREAD(OBS_TASK_UI);
	if (MAX_AV_PLANES <= channel)
		return;
	last_audio_ts[channel] = timestamp;

	if (channel == 0) {
		int64_t ts = (int64_t)(timestamp - last_video_ts);
		if (ts > 500000000)
			ts -= 1000000000;
		latencyDisplay->setText(QString("%1 ms").arg(ts * 1e-6, 2, 'f', 1));
		blog(LOG_INFO, "diff=%05f", ts * 1e-9);
		// TODO: also display other channels
	}
}
