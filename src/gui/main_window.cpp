// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#include "main_window.h"
#include "device_dialog.h"
#include "options_dialog.h"
#include "service_manager.h"
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
	updateServiceStatus();

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

	// Spell out what ticking the box actually causes, rather than leaving the
	// user to infer it.
	manage_hint_ = new QLabel(central);
	manage_hint_->setText(tr(
		"Runs a system service that powers the TV on at boot, so the login "
		"screen is visible, and off again when this PC shuts down or suspends."));
	manage_hint_->setWordWrap(true);
	manage_hint_->setEnabled(false);
	manage_hint_->setContentsMargins(22, 0, 0, 0);
	outer->addWidget(manage_hint_);

	service_status_ = new QLabel(central);
	service_status_->setWordWrap(true);
	service_status_->setContentsMargins(22, 0, 0, 0);
	outer->addWidget(service_status_);

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
	OptionsDialog dialog(prefs_, this);
	if (dialog.exec() == QDialog::Accepted)
	{
		dialog.applyTo(prefs_);
		setDirty(true);
	}
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
	syncService();
	updateServiceStatus();
}
void MainWindow::syncService(void)
{
	// Does the user want anything managed automatically?
	bool wants_management = false;
	for (const auto& device : prefs_.devices_)
		if (device.enabled)
			wants_management = true;

	if (wants_management && !service::isEnabled())
	{
		// Enabling a background service that starts at every login is not
		// something to do silently. Say exactly what it will do first.
		auto answer = QMessageBox::question(this,
			tr("Enable automatic management?"),
			tr("<p>To manage your display automatically, a background service "
				"must run in your desktop session.</p>"
				"<p><b>This will:</b></p>"
				"<ul>"
				"<li>Install a system service (<code>%1</code>) that starts at "
				"boot, before anyone logs in</li>"
				"<li><b>Power the TV on</b> at boot, so the login screen is "
				"visible, and again when this PC resumes</li>"
				"<li><b>Power the TV off</b> when this PC shuts down or suspends</li>"
				"</ul>"
				"<p>The service runs as your own user account, not as root. "
				"Installing it needs administrator rights, so you will be asked "
				"to authenticate once. You can turn it off again at any time by "
				"unticking &quot;Automatically manage this device&quot;.</p>"
				"<p>Enable it now?</p>")
			.arg(service::unitName()),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

		if (answer != QMessageBox::Yes)
			return;

		if (service::isDevelopmentPath(service::daemonPath())
			&& !confirmUnstablePath(service::daemonPath()))
			return;

		QString error;
		if (!service::install(error))
		{
			QMessageBox::critical(this, tr("Could not enable the service"), error);
			return;
		}
		QMessageBox::information(this, tr("Automatic management enabled"),
			tr("The service is running and will start automatically at boot."));
		return;
	}

	if (!wants_management && service::isEnabled())
	{
		auto answer = QMessageBox::question(this,
			tr("Disable automatic management?"),
			tr("<p>No device is set to be managed automatically.</p>"
				"<p>Stop the background service and prevent it from starting at "
				"login? Your display will no longer be powered off when this PC "
				"shuts down or suspends.</p>"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

		if (answer != QMessageBox::Yes)
			return;

		QString error;
		if (!service::disable(error))
			QMessageBox::critical(this, tr("Could not disable the service"), error);
		return;
	}

	// Already in the desired state; just let a running service reload.
	service::tryRestart();
}
void MainWindow::updateServiceStatus(void)
{
	if (!service_status_)
		return;

	if (service::isEnabled())
	{
		QString text = service::isActive()
			? tr("✓ System service is running and starts at boot.")
			: tr("⚠ System service is enabled but not currently running.");

		// A unit installed earlier may still reference a build tree.
		QString exec = service::installedUnitExecStart();
		if (service::isDevelopmentPath(exec))
			text += tr("\n⚠ It runs %1, which is a build directory. Install the "
				"application and enable the service again to make this stable.")
			.arg(exec);

		service_status_->setText(text);
	}
	else if (service::daemonPath().isEmpty())
	{
		service_status_->setText(
			tr("⚠ The daemon executable was not found; automatic management "
				"cannot be enabled."));
	}
	else
	{
		service_status_->setText(
			tr("System service is not enabled. Tick the box above and click "
				"Apply to enable it."));
	}
	service_status_->setEnabled(false);
}
void MainWindow::notifyDaemon(const std::string& command)
{
	// Best effort: the daemon may not be running, which is not an error.
	IpcClient::sendOneShot(paths::ipcSocket(), command, nullptr, 1000);
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
bool MainWindow::confirmUnstablePath(const QString& path)
{
	// A system unit that points into a build tree breaks as soon as the tree is
	// cleaned, moved or rebuilt, and the failure is obscure. Say so plainly.
	return QMessageBox::warning(this, tr("Unstable location"),
		tr("<p>The daemon would be run from:</p><p><code>%1</code></p>"
			"<p>That is a build directory. The service will stop working if you "
			"move, clean or rebuild it.</p>"
			"<p>Install the application first, for example:</p>"
			"<p><code>sudo cmake --install build --prefix /usr/local</code></p>"
			"<p>Enable the service anyway?</p>").arg(path),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}
