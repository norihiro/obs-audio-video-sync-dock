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

	QLabel *label;
	label = new QLabel(obs_module_text("Label.Latency"), this);
	topLayout->addWidget(label, 0, 0);

	latencyDisplay = new QLabel("-", this);
	topLayout->addWidget(latencyDisplay, 0, 1);

	label = new QLabel(obs_module_text("Label.Index"), this);
	topLayout->addWidget(label, 1, 0);

	indexDisplay = new QLabel("-", this);
	topLayout->addWidget(indexDisplay, 1, 1);

	label = new QLabel(obs_module_text("Label.Frequency"), this);
	topLayout->addWidget(label, 2, 0);

	frequencyDisplay = new QLabel("-", this);
	topLayout->addWidget(frequencyDisplay, 2, 1);

	label = new QLabel(obs_module_text("Label.VideoIndex"), this);
	topLayout->addWidget(label, 3, 0);

	videoIndexDisplay = new QLabel("-", this);
	topLayout->addWidget(videoIndexDisplay, 3, 1);

	label = new QLabel(obs_module_text("Label.AudioIndex"), this);
	topLayout->addWidget(label, 4, 0);

	audioIndexDisplay = new QLabel("-", this);
	topLayout->addWidget(audioIndexDisplay, 4, 1);

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

Q_DECLARE_METATYPE(uint64_t);
Q_DECLARE_METATYPE(video_marker_found_s);
Q_DECLARE_METATYPE(audio_marker_found_s);

extern "C" QWidget *create_sync_test_dock()
{
	qRegisterMetaType<uint64_t>("uint64_t");
	qRegisterMetaType<video_marker_found_s>("video_marker_found_s");
	qRegisterMetaType<audio_marker_found_s>("audio_marker_found_s");

	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	return static_cast<QWidget *>(new SyncTestDock(main_window));
}

#define CD_TO_LOCAL(type, name, get_func)  \
	type name;                         \
	if (!get_func(cd, #name, &name)) \
		return;

static void cb_video_marker_found(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(video_marker_found_s *, data, calldata_get_ptr);

	QMetaObject::invokeMethod(dock, "on_video_marker_found", Q_ARG(video_marker_found_s, *data));
};

static void cb_audio_marker_found(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(audio_marker_found_s *, data, calldata_get_ptr);

	QMetaObject::invokeMethod(dock, "on_audio_marker_found", Q_ARG(audio_marker_found_s, *data));
};

static void cb_sync_found(void *param, calldata_t *cd)
{
	auto *dock = (SyncTestDock *)param;

	CD_TO_LOCAL(long long, video_ts, calldata_get_int);
	CD_TO_LOCAL(long long, audio_ts, calldata_get_int);
	CD_TO_LOCAL(long long, index, calldata_get_int);

	QMetaObject::invokeMethod(dock, "on_sync_found", Q_ARG(uint64_t, video_ts), Q_ARG(uint64_t, audio_ts),
				  Q_ARG(int, index));
}

void SyncTestDock::on_start_stop()
{
	if (!sync_test) /* request to start */ {
		OBSOutputAutoRelease o = obs_output_create(ID_PREFIX "output", "sync-test-output", nullptr, nullptr);
		if (!o) {
			blog(LOG_ERROR, "Failed to create sync-test-output.");
			return;
		}

		last_video_ix = last_audio_ix = -1;
		missed_video_ix = missed_audio_ix = 0;
		received_video_ix = received_audio_ix = 0;

		auto *sh = obs_output_get_signal_handler(o);
		signal_handler_connect(sh, "video_marker_found", cb_video_marker_found, this);
		signal_handler_connect(sh, "audio_marker_found", cb_audio_marker_found, this);
		signal_handler_connect(sh, "sync_found", cb_sync_found, this);

		obs_output_start(o);

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

void SyncTestDock::on_video_marker_found(struct video_marker_found_s data)
{
	const int index = data.qr_data.index;
	if (last_video_ix >= 0) {
		int m = (index - last_video_ix - 1) & 0xFF;
		if (m < 0x80)
			missed_video_ix += m;
	}
	blog(LOG_INFO, "index=%d last_video_ix=%d missed_video_ix=%d received_video_ix=%d", index, last_video_ix, missed_video_ix, received_video_ix);
	last_video_ix = index;
	received_video_ix ++;
	frequencyDisplay->setText(QString("%1 Hz").arg(data.qr_data.f));
	int missed = missed_video_ix * 100 / (received_video_ix + missed_video_ix);
	videoIndexDisplay->setText(QString("%1 (%2% missed)").arg(index).arg(missed));
}

void SyncTestDock::on_audio_marker_found(struct audio_marker_found_s data)
{
	const int index = data.index;
	if (last_audio_ix >= 0) {
		int m = (index - last_audio_ix - 1) & 0xFF;
		if (m < 0x80)
			missed_audio_ix += m;
	}
	last_audio_ix = index;
	received_audio_ix ++;
	int missed = missed_audio_ix * 100 / (received_audio_ix + missed_audio_ix);
	audioIndexDisplay->setText(QString("%1 (%2% missed)").arg(index).arg(missed));
}

void SyncTestDock::on_sync_found(uint64_t video_ts, uint64_t audio_ts, int index)
{
	int64_t ts = (int64_t)audio_ts - (int64_t)video_ts;
	latencyDisplay->setText(QString("%1 ms").arg(ts * 1e-6, 2, 'f', 1));
	indexDisplay->setText(QString("%1").arg(index));
}
