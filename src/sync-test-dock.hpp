#pragma once
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <obs.hpp>
#include "sync-test-output.hpp"

class SyncTestDock : public QFrame {
	Q_OBJECT

public:
	SyncTestDock(QWidget *parent = nullptr);
	~SyncTestDock();

private:
	QPushButton *startButton = nullptr;

	QLabel *latencyDisplay = nullptr;
	QLabel *latencyPolarity = nullptr;
	QLabel *indexDisplay = nullptr;
	QLabel *frequencyDisplay = nullptr;
	QLabel *videoIndexDisplay = nullptr;
	QLabel *audioIndexDisplay = nullptr;

private:
	OBSOutput sync_test;

private:
	int last_video_ix;
	int last_audio_ix;
	int missed_video_ix;
	int missed_audio_ix;
	int received_video_ix;
	int received_audio_ix;
	int received_video_index_max = 0;
	int received_audio_index_max = 0;
	int audio_index_max = 0;

private:
	void on_start_stop();

private slots:
	void on_video_marker_found(video_marker_found_s data);
	void on_audio_marker_found(audio_marker_found_s data);
	void on_sync_found(uint64_t video_ts, uint64_t audio_ts, int index);
};
