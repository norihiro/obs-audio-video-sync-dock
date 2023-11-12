#pragma once
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <obs.hpp>

class SyncTestDock : public QFrame {
	Q_OBJECT

public:
	SyncTestDock(QWidget *parent = nullptr);
	~SyncTestDock();

private:
	QPushButton *startButton = nullptr;
	bool started = 0;

	QLabel *latencyDisplay = nullptr;

private:
	OBSOutput sync_test;

private:
	uint64_t last_video_ts;
	uint64_t last_audio_ts[MAX_AV_PLANES];

private:
	void on_start_stop();

private slots:
	void on_video_marker_found(uint64_t timestamp, double score);
	void on_audio_marker_found(size_t channel, uint64_t timestamp, double score);
};
