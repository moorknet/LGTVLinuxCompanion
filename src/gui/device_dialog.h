// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#pragma once
#include <QDialog>
#include "device.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QRadioButton;
class QSpinBox;
class QPushButton;

// Per-device configuration, reproducing IDD_DEVICE: the four group boxes for
// device, network, connection and source input settings.
class DeviceDialog : public QDialog
{
	Q_OBJECT

public:
	explicit DeviceDialog(const Device& device, QWidget* parent = nullptr);

	// The edited device. Only meaningful after exec() returns Accepted.
	const Device& device(void) const { return device_; }

private slots:
	void onWakeMethodChanged(void);
	void validate(void);

private:
	QWidget* buildDeviceGroup(void);
	QWidget* buildNetworkGroup(void);
	QWidget* buildConnectionGroup(void);
	QWidget* buildSourceInputGroup(void);
	void load(void);
	void store(void);

	Device device_;

	QLineEdit* name_edit_ = nullptr;
	QLineEdit* ip_edit_ = nullptr;
	QPlainTextEdit* mac_edit_ = nullptr;

	QComboBox* wol_combo_ = nullptr;
	QRadioButton* wol_broadcast_ = nullptr;
	QRadioButton* wol_ip_ = nullptr;
	QRadioButton* wol_subnet_ = nullptr;
	QLineEdit* subnet_edit_ = nullptr;
	QComboBox* nic_combo_ = nullptr;

	QComboBox* ssl_combo_ = nullptr;
	QComboBox* persistence_combo_ = nullptr;

	QComboBox* source_combo_ = nullptr;
	QCheckBox* check_hdmi_check_ = nullptr;
	QCheckBox* set_hdmi_check_ = nullptr;
	QSpinBox* hdmi_delay_spin_ = nullptr;

	QPushButton* ok_button_ = nullptr;
};
