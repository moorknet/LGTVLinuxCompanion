// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#include "options_dialog.h"
#include "log.h"
#include "paths.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

OptionsDialog::OptionsDialog(const Preferences& prefs, QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Settings"));
	setModal(true);

	auto* outer = new QVBoxLayout(this);
	outer->addWidget(buildGlobalGroup());
	outer->addWidget(buildPowerGroup());
	outer->addWidget(buildApiGroup());
	outer->addStretch(1);

	auto* footer = new QHBoxLayout;
	// Upstream showed this in the dialog itself; keep it visible.
	auto* copyright = new QLabel(QString::fromUtf8("© 2021-2026 Jörgen Persson"), this);
	copyright->setEnabled(false);
	footer->addWidget(copyright);
	footer->addStretch(1);

	auto* buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	footer->addWidget(buttons);
	outer->addLayout(footer);

	load(prefs);
	onIdleToggled(idle_check_->isChecked());
}

QWidget* OptionsDialog::buildGlobalGroup(void)
{
	auto* group = new QGroupBox(tr("Global settings"), this);
	auto* layout = new QVBoxLayout(group);

	auto* timeout_row = new QHBoxLayout;
	timeout_row->addWidget(new QLabel(tr("Power On timeout (s)"), group));
	timeout_spin_ = new QSpinBox(group);
	timeout_spin_->setRange(5, 100);
	timeout_row->addWidget(timeout_spin_);
	timeout_row->addStretch(1);
	layout->addLayout(timeout_row);

	auto* log_row = new QHBoxLayout;
	log_row->addWidget(new QLabel(tr("Log level:"), group));
	log_combo_ = new QComboBox(group);
	log_combo_->addItem(tr("Off"), LOG_LEVEL_OFF);
	log_combo_->addItem(tr("Info"), LOG_LEVEL_INFO);
	log_combo_->addItem(tr("Warning"), LOG_LEVEL_WARNING);
	log_combo_->addItem(tr("Error"), LOG_LEVEL_ERROR);
	log_combo_->addItem(tr("Debug"), LOG_LEVEL_DEBUG);
	log_row->addWidget(log_combo_);

	auto* show_link = new QPushButton(tr("Show"), group);
	auto* clear_link = new QPushButton(tr("Clear"), group);
	show_link->setFlat(true);
	clear_link->setFlat(true);
	log_row->addWidget(show_link);
	log_row->addWidget(clear_link);
	log_row->addStretch(1);
	layout->addLayout(log_row);

	connect(show_link, &QPushButton::clicked, this, &OptionsDialog::onShowLog);
	connect(clear_link, &QPushButton::clicked, this, &OptionsDialog::onClearLog);
	return group;
}
QWidget* OptionsDialog::buildPowerGroup(void)
{
	auto* group = new QGroupBox(
		tr("Advanced power options and enhanced burn-in protection"), this);
	auto* layout = new QVBoxLayout(group);

	idle_check_ = new QCheckBox(tr("Automatic user idle mode management"), group);
	layout->addWidget(idle_check_);

	auto* delay_row = new QHBoxLayout;
	delay_row->addSpacing(20);
	delay_row->addWidget(new QLabel(tr("Blank the screen after (minutes):"), group));
	idle_delay_spin_ = new QSpinBox(group);
	idle_delay_spin_->setRange(1, 240);
	delay_row->addWidget(idle_delay_spin_);
	delay_row->addStretch(1);
	layout->addLayout(delay_row);

	auto* mute_row = new QHBoxLayout;
	mute_row->addSpacing(20);
	idle_mute_check_ = new QCheckBox(tr("Also mute the speakers while idle"), group);
	mute_row->addWidget(idle_mute_check_);
	mute_row->addStretch(1);
	layout->addLayout(mute_row);

	remote_check_ = new QCheckBox(
		tr("Support remote streaming (e.g. Sunshine, Apollo, Steam, RDP, ...)"), group);
	layout->addWidget(remote_check_);

	auto* mode_row = new QHBoxLayout;
	mode_row->addSpacing(20);
	mode_row->addWidget(new QLabel(tr("Remote streaming power off mode"), group));
	remote_mode_combo_ = new QComboBox(group);
	remote_mode_combo_->addItem(tr("Power off the device"), true);
	remote_mode_combo_->addItem(tr("Blank the screen only"), false);
	mode_row->addWidget(remote_mode_combo_);
	mode_row->addStretch(1);
	layout->addLayout(mode_row);

	connect(idle_check_, &QCheckBox::toggled, this, &OptionsDialog::onIdleToggled);
	connect(remote_check_, &QCheckBox::toggled,
		remote_mode_combo_, &QComboBox::setEnabled);
	return group;
}
QWidget* OptionsDialog::buildApiGroup(void)
{
	auto* group = new QGroupBox(tr("External API"), this);
	auto* layout = new QVBoxLayout(group);

	api_check_ = new QCheckBox(
		tr("Send events to external scripts or applications via IPC"), group);
	layout->addWidget(api_check_);

	auto* path = new QLabel(
		tr("Socket: %1").arg(QString::fromStdString(paths::ipcSocket())), group);
	path->setEnabled(false);
	path->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(path);
	return group;
}
void OptionsDialog::load(const Preferences& prefs)
{
	timeout_spin_->setValue(prefs.power_on_timeout_);
	int log_index = log_combo_->findData(prefs.log_level_);
	log_combo_->setCurrentIndex(log_index >= 0 ? log_index : 0);

	idle_check_->setChecked(prefs.user_idle_mode_);
	idle_delay_spin_->setValue(prefs.user_idle_mode_delay_);
	idle_mute_check_->setChecked(prefs.user_idle_mode_mute_speakers_);

	remote_check_->setChecked(prefs.remote_streaming_host_support_);
	remote_mode_combo_->setCurrentIndex(
		remote_mode_combo_->findData(prefs.remote_streaming_host_prefer_power_off_));
	remote_mode_combo_->setEnabled(prefs.remote_streaming_host_support_);

	api_check_->setChecked(prefs.external_api_support_);
}
void OptionsDialog::applyTo(Preferences& prefs) const
{
	prefs.power_on_timeout_ = timeout_spin_->value();
	prefs.log_level_ = log_combo_->currentData().toInt();

	prefs.user_idle_mode_ = idle_check_->isChecked();
	prefs.user_idle_mode_delay_ = idle_delay_spin_->value();
	prefs.user_idle_mode_mute_speakers_ = idle_mute_check_->isChecked();

	prefs.remote_streaming_host_support_ = remote_check_->isChecked();
	prefs.remote_streaming_host_prefer_power_off_ = remote_mode_combo_->currentData().toBool();

	prefs.external_api_support_ = api_check_->isChecked();
}
void OptionsDialog::onIdleToggled(bool checked)
{
	idle_delay_spin_->setEnabled(checked);
	idle_mute_check_->setEnabled(checked);
}
void OptionsDialog::onShowLog(void)
{
	QString path = QString::fromStdString(paths::logFile());
	if (!QFile::exists(path))
	{
		QMessageBox::information(this, tr("Log"),
			tr("No log file yet. Set a log level above and click Apply."));
		return;
	}
	QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
void OptionsDialog::onClearLog(void)
{
	QString path = QString::fromStdString(paths::logFile());
	if (QFile::exists(path) && !QFile::remove(path))
		QMessageBox::warning(this, tr("Log"),
			tr("Could not remove %1").arg(path));
}
