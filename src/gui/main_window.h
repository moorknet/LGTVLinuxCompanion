// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#pragma once
#include <QMainWindow>
#include <memory>
#include "preferences.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// The main window, reproducing IDD_MAIN from the windows UI: a device selector,
// a Scan split-button, the "automatically manage this device" checkbox, and the
// Settings / Apply buttons.
class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override;

protected:
	void closeEvent(QCloseEvent*) override;

private slots:
	void onDeviceChanged(int index);
	void onEnableToggled(bool checked);
	void onScan(void);
	void onAddManually(void);
	void onEditDevice(void);
	void onRemoveDevice(void);
	void onTestDevice(void);
	void onSettings(void);
	void onApply(void);

private:
	void buildUi(void);
	// Restart the systemd user service so it re-reads the configuration.
	void restartDaemon(void);
	void reloadDevices(void);
	void setDirty(bool dirty);
	// Ask the daemon to re-read its configuration after Apply. Replaces the
	// windows service restart the UI performed.
	void notifyDaemon(const std::string& command);

	Preferences prefs_;
	bool dirty_ = false;

	QComboBox* device_combo_ = nullptr;
	QPushButton* scan_button_ = nullptr;
	QCheckBox* enable_check_ = nullptr;
	QPushButton* settings_button_ = nullptr;
	QPushButton* apply_button_ = nullptr;
	QLabel* donate_label_ = nullptr;
};
