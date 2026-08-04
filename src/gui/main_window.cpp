// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#include "main_window.h"
#include "device_dialog.h"
#include "app_define.h"
#include "ipc.h"
#include "paths.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	setWindowTitle(APPNAME);
	buildUi();
	reloadDevices();

	if (!prefs_.isInitialised())
	{
		QMessageBox::warning(this, tr("Configuration"),
			tr("The configuration file could not be read:\n%1\n\n"
				"A new one will be written when you click Apply.")
			.arg(QString::fromStdString(paths::configFile())));
	}
}
MainWindow::~MainWindow() = default;

void MainWindow::buildUi(void)
{
	auto* central = new QWidget(this);
	auto* outer = new QVBoxLayout(central);
	outer->setContentsMargins(12, 12, 12, 12);
	outer->setSpacing(10);

	// Device row: selector plus the Scan split-button.
	auto* device_row = new QHBoxLayout;
	device_row->addWidget(new QLabel(tr("Device:"), central));

	device_combo_ = new QComboBox(central);
	device_combo_->setMinimumWidth(280);
	device_combo_->setEnabled(false);
	device_row->addWidget(device_combo_, 1);

	// The windows UI used a BS_SPLITBUTTON: the face scans, the arrow opens a
	// menu of per-device actions. QPushButton with a menu is the equivalent.
	scan_button_ = new QPushButton(tr("&Scan"), central);
	auto* scan_menu = new QMenu(scan_button_);
	scan_menu->addAction(tr("Add device manually..."), this, &MainWindow::onAddManually);
	scan_menu->addAction(tr("Configure device..."), this, &MainWindow::onEditDevice);
	scan_menu->addAction(tr("Remove device"), this, &MainWindow::onRemoveDevice);
	scan_menu->addSeparator();
	scan_menu->addAction(tr("Test"), this, &MainWindow::onTestDevice);
	scan_button_->setMenu(scan_menu);
	device_row->addWidget(scan_button_);
	outer->addLayout(device_row);

	enable_check_ = new QCheckBox(tr("Automatically manage this device"), central);
	enable_check_->setEnabled(false);
	outer->addWidget(enable_check_);

	outer->addStretch(1);

	auto* button_row = new QHBoxLayout;
	settings_button_ = new QPushButton(tr("Settings"), central);
	button_row->addWidget(settings_button_);

	donate_label_ = new QLabel(central);
	donate_label_->setText(QString("<a href=\"%1\">%2</a>")
		.arg(DONATELINK, tr("Support your local Software Developer?")));
	donate_label_->setOpenExternalLinks(true);
	donate_label_->setAlignment(Qt::AlignCenter);
	button_row->addWidget(donate_label_, 1);

	apply_button_ = new QPushButton(tr("&Apply"), central);
	apply_button_->setDefault(true);
	apply_button_->setEnabled(false);
	button_row->addWidget(apply_button_);
	outer->addLayout(button_row);

	setCentralWidget(central);
	resize(620, 230);

	connect(device_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &MainWindow::onDeviceChanged);
	connect(enable_check_, &QCheckBox::toggled, this, &MainWindow::onEnableToggled);
	connect(scan_button_, &QPushButton::clicked, this, &MainWindow::onScan);
	connect(settings_button_, &QPushButton::clicked, this, &MainWindow::onSettings);
	connect(apply_button_, &QPushButton::clicked, this, &MainWindow::onApply);
}
void MainWindow::reloadDevices(void)
{
	const QSignalBlocker blocker(device_combo_);
	device_combo_->clear();

	for (const auto& device : prefs_.devices_)
	{
		QString label = QString::fromStdString(device.name.empty() ? device.id : device.name);
		if (!device.ip.empty())
			label += QString(" (%1)").arg(QString::fromStdString(device.ip));
		device_combo_->addItem(label);
	}

	const bool has_devices = !prefs_.devices_.empty();
	device_combo_->setEnabled(has_devices);
	enable_check_->setEnabled(has_devices);

	if (has_devices)
	{
		device_combo_->setCurrentIndex(0);
		onDeviceChanged(0);
	}
	else
	{
		const QSignalBlocker check_blocker(enable_check_);
		enable_check_->setChecked(false);
	}
}
void MainWindow::onDeviceChanged(int index)
{
	if (index < 0 || index >= static_cast<int>(prefs_.devices_.size()))
		return;
	const QSignalBlocker blocker(enable_check_);
	enable_check_->setChecked(prefs_.devices_[index].enabled);
}
void MainWindow::onEnableToggled(bool checked)
{
	int index = device_combo_->currentIndex();
	if (index < 0 || index >= static_cast<int>(prefs_.devices_.size()))
		return;
	prefs_.devices_[index].enabled = checked;
	setDirty(true);
}
void MainWindow::setDirty(bool dirty)
{
	dirty_ = dirty;
	apply_button_->setEnabled(dirty);
}
void MainWindow::onScan(void)
{
	QMessageBox::information(this, tr("Scan"),
		tr("Network scanning is not implemented yet.\n\n"
			"Use the dropdown arrow next to Scan to add a device manually."));
}
void MainWindow::onAddManually(void)
{
	Device device;
	DeviceDialog dialog(device, this);
	if (dialog.exec() == QDialog::Accepted)
	{
		prefs_.devices_.push_back(dialog.device());
		reloadDevices();
		device_combo_->setCurrentIndex(static_cast<int>(prefs_.devices_.size()) - 1);
		setDirty(true);
	}
}
void MainWindow::onEditDevice(void)
{
	int index = device_combo_->currentIndex();
	if (index < 0 || index >= static_cast<int>(prefs_.devices_.size()))
		return;

	DeviceDialog dialog(prefs_.devices_[index], this);
	if (dialog.exec() == QDialog::Accepted)
	{
		prefs_.devices_[index] = dialog.device();
		int keep = index;
		reloadDevices();
		device_combo_->setCurrentIndex(keep);
		setDirty(true);
	}
}
void MainWindow::onRemoveDevice(void)
{
	int index = device_combo_->currentIndex();
	if (index < 0 || index >= static_cast<int>(prefs_.devices_.size()))
		return;

	QString name = device_combo_->currentText();
	if (QMessageBox::question(this, tr("Remove device"),
		tr("Remove %1 from the configuration?").arg(name))
		!= QMessageBox::Yes)
		return;

	prefs_.devices_.erase(prefs_.devices_.begin() + index);
	reloadDevices();
	setDirty(true);
}
void MainWindow::onTestDevice(void)
{
	int index = device_combo_->currentIndex();
	if (index < 0 || index >= static_cast<int>(prefs_.devices_.size()))
		return;

	if (dirty_)
	{
		QMessageBox::information(this, tr("Test"),
			tr("Please click Apply first, so the daemon picks up your changes."));
		return;
	}
	// Blank and unblank the screen: visible on the TV, and harmless if wake on
	// lan cannot reach it.
	notifyDaemon("-screenoff");
	QMessageBox::information(this, tr("Test"),
		tr("Sent a blank-screen command to %1.\n\nClick OK to unblank.")
		.arg(device_combo_->currentText()));
	notifyDaemon("-screenon");
}
void MainWindow::onSettings(void)
{
	QMessageBox::information(this, tr("Settings"),
		tr("The global settings dialog is not implemented yet."));
}
void MainWindow::onApply(void)
{
	if (!prefs_.writeToDisk())
	{
		QMessageBox::critical(this, tr("Apply"),
			tr("Failed to write the configuration to:\n%1")
			.arg(QString::fromStdString(paths::configFile())));
		return;
	}
	setDirty(false);
	restartDaemon();
}
void MainWindow::notifyDaemon(const std::string& command)
{
	// Best effort: the daemon may not be running, which is not an error.
	IpcClient::sendOneShot(paths::ipcSocket(), command, nullptr, 1000);
}
void MainWindow::restartDaemon(void)
{
	// The daemon reads its configuration once at startup, so applying changes
	// means restarting it. Upstream relaunched the windows service; here systemd
	// owns the lifecycle. Silently does nothing if the unit is not installed.
	QProcess::startDetached("systemctl",
		{ "--user", "try-restart", "lgtv-companion.service" });
}
void MainWindow::closeEvent(QCloseEvent* event)
{
	if (dirty_)
	{
		auto answer = QMessageBox::question(this, tr("Unsaved changes"),
			tr("You have unapplied changes. Apply them before closing?"),
			QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
		if (answer == QMessageBox::Cancel)
		{
			event->ignore();
			return;
		}
		if (answer == QMessageBox::Yes)
			onApply();
	}
	event->accept();
}
