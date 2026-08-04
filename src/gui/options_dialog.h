// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#pragma once
#include <QDialog>
#include "preferences.h"

class QCheckBox;
class QComboBox;
class QSpinBox;
class QPushButton;

// Global settings, reproducing IDD_OPTIONS.
//
// Two of the upstream group boxes do not survive the port and are absent:
//   - "Shutdown settings", whose list of localised restart strings existed only
//     to tell a reboot from a shutdown in the windows event log. logind reports
//     the operation directly.
//   - "Multi-monitor support", which drove the windows display topology API.
// The Updates control is gone too; distributions handle packaging.
class OptionsDialog : public QDialog
{
	Q_OBJECT

public:
	explicit OptionsDialog(const Preferences& prefs, QWidget* parent = nullptr);

	// Applies the edited values onto the given preferences object.
	void applyTo(Preferences& prefs) const;

private slots:
	void onShowLog(void);
	void onClearLog(void);
	void onIdleToggled(bool checked);

private:
	QWidget* buildGlobalGroup(void);
	QWidget* buildPowerGroup(void);
	QWidget* buildApiGroup(void);
	void load(const Preferences& prefs);

	QSpinBox* timeout_spin_ = nullptr;
	QComboBox* log_combo_ = nullptr;

	QCheckBox* idle_check_ = nullptr;
	QSpinBox* idle_delay_spin_ = nullptr;
	QCheckBox* idle_mute_check_ = nullptr;
	QCheckBox* remote_check_ = nullptr;
	QComboBox* remote_mode_combo_ = nullptr;

	QCheckBox* api_check_ = nullptr;
};
