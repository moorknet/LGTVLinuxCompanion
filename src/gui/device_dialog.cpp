// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#include "device_dialog.h"
#include "tools.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{
	// The windows dialog used SysIPAddress32, which has no Qt equivalent. A
	// validated line edit gives the same guarantee.
	QRegularExpressionValidator* ipValidator(QObject* parent)
	{
		static const QString octet =
			"(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])";
		return new QRegularExpressionValidator(
			QRegularExpression("^" + octet + "\\." + octet + "\\." + octet + "\\." + octet + "$"),
			parent);
	}
}

DeviceDialog::DeviceDialog(const Device& device, QWidget* parent)
	: QDialog(parent)
	, device_(device)
{
	setWindowTitle(device.name.empty() ? tr("Add device") : tr("Configure device"));
	setModal(true);

	auto* outer = new QVBoxLayout(this);

	auto* top = new QHBoxLayout;
	top->addWidget(buildDeviceGroup(), 1);
	top->addWidget(buildNetworkGroup(), 1);
	outer->addLayout(top);

	outer->addWidget(buildConnectionGroup());
	outer->addWidget(buildSourceInputGroup());
	outer->addStretch(1);

	auto* buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	ok_button_ = buttons->button(QDialogButtonBox::Ok);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] { store(); accept(); });
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	outer->addWidget(buttons);

	load();
	onWakeMethodChanged();
	validate();
}

QWidget* DeviceDialog::buildDeviceGroup(void)
{
	auto* group = new QGroupBox(tr("Device settings"), this);
	auto* form = new QFormLayout(group);

	name_edit_ = new QLineEdit(group);
	form->addRow(tr("Friendly name"), name_edit_);

	ip_edit_ = new QLineEdit(group);
	ip_edit_->setValidator(ipValidator(ip_edit_));
	ip_edit_->setPlaceholderText("192.168.1.100");
	form->addRow(tr("IP Address"), ip_edit_);

	mac_edit_ = new QPlainTextEdit(group);
	mac_edit_->setPlaceholderText("aa:bb:cc:dd:ee:ff");
	mac_edit_->setMinimumHeight(70);
	form->addRow(tr("MAC (one per line)"), mac_edit_);

	connect(name_edit_, &QLineEdit::textChanged, this, &DeviceDialog::validate);
	connect(ip_edit_, &QLineEdit::textChanged, this, &DeviceDialog::validate);
	return group;
}
QWidget* DeviceDialog::buildNetworkGroup(void)
{
	auto* group = new QGroupBox(tr("Network settings"), this);
	auto* layout = new QVBoxLayout(group);

	auto* wol_row = new QHBoxLayout;
	wol_row->addWidget(new QLabel(tr("Wake-on-Lan:"), group));
	wol_combo_ = new QComboBox(group);
	wol_combo_->addItem(tr("Enabled"), true);
	wol_combo_->addItem(tr("Disabled"), false);
	wol_row->addWidget(wol_combo_, 1);
	layout->addLayout(wol_row);

	wol_broadcast_ = new QRadioButton(
		tr("Network broadcast address 255.255.255.255"), group);
	wol_ip_ = new QRadioButton(tr("Device IP-address"), group);
	wol_subnet_ = new QRadioButton(
		tr("Broadcast address according to device IP-address and subnet mask"), group);
	wol_broadcast_->setToolTip(
		tr("Only reaches devices on the same subnet as this machine."));
	layout->addWidget(wol_broadcast_);
	layout->addWidget(wol_ip_);
	layout->addWidget(wol_subnet_);

	auto* subnet_row = new QHBoxLayout;
	subnet_row->addWidget(new QLabel(tr("Subnet mask"), group));
	subnet_edit_ = new QLineEdit(group);
	subnet_edit_->setValidator(ipValidator(subnet_edit_));
	subnet_row->addWidget(subnet_edit_, 1);
	layout->addLayout(subnet_row);

	layout->addWidget(new QLabel(tr("Network Adapter for WOL"), group));
	nic_combo_ = new QComboBox(group);
	// Upstream listed adapters by NET_LUID; here they are interface names.
	nic_combo_->addItem(tr("Automatic"), QString());
	for (const auto& entry : tools::getLocalIP())
	{
		std::string bare = entry.substr(0, entry.find('/'));
		std::string name = tools::getInterfaceForIP(bare);
		if (name.empty())
			continue;
		nic_combo_->addItem(
			QString("%1 (%2)").arg(QString::fromStdString(name), QString::fromStdString(bare)),
			QString::fromStdString(name));
	}
	layout->addWidget(nic_combo_);
	layout->addStretch(1);

	connect(wol_broadcast_, &QRadioButton::toggled, this, &DeviceDialog::onWakeMethodChanged);
	connect(wol_ip_, &QRadioButton::toggled, this, &DeviceDialog::onWakeMethodChanged);
	connect(wol_subnet_, &QRadioButton::toggled, this, &DeviceDialog::onWakeMethodChanged);
	return group;
}
QWidget* DeviceDialog::buildConnectionGroup(void)
{
	auto* group = new QGroupBox(tr("Connection settings"), this);
	auto* layout = new QHBoxLayout(group);

	layout->addWidget(new QLabel(tr("Firmware:"), group));
	ssl_combo_ = new QComboBox(group);
	ssl_combo_->addItem(tr("Newer (secure, port 3001)"), true);
	ssl_combo_->addItem(tr("Older (insecure, port 3000)"), false);
	layout->addWidget(ssl_combo_, 1);

	layout->addSpacing(20);
	layout->addWidget(new QLabel(tr("Persistence:"), group));
	persistence_combo_ = new QComboBox(group);
	persistence_combo_->addItem(tr("Off"), PERSISTENT_CONNECTION_OFF);
	persistence_combo_->addItem(tr("On"), PERSISTENT_CONNECTION_ON);
	persistence_combo_->addItem(tr("On with keep-alive"), PERSISTENT_CONNECTION_KEEPALIVE);
	layout->addWidget(persistence_combo_, 1);
	return group;
}
QWidget* DeviceDialog::buildSourceInputGroup(void)
{
	auto* group = new QGroupBox(tr("Source input settings"), this);
	auto* layout = new QVBoxLayout(group);

	auto* source_row = new QHBoxLayout;
	source_row->addWidget(new QLabel(tr("HDMI-input for PC:"), group));
	source_combo_ = new QComboBox(group);
	for (int i = 1; i <= 4; i++)
		source_combo_->addItem(QString("HDMI %1").arg(i), i);
	source_row->addWidget(source_combo_);
	source_row->addStretch(1);
	layout->addLayout(source_row);

	check_hdmi_check_ = new QCheckBox(
		tr("Do not automatically power off device when using it for other "
			"activites (e.g. Netflix, Cable TV, ...)"), group);
	layout->addWidget(check_hdmi_check_);

	auto* delay_row = new QHBoxLayout;
	set_hdmi_check_ = new QCheckBox(
		tr("Switch to PC HDMI-input when powering on, with a delay of (seconds):"), group);
	delay_row->addWidget(set_hdmi_check_, 1);

	hdmi_delay_spin_ = new QSpinBox(group);
	hdmi_delay_spin_->setRange(0, 30);
	hdmi_delay_spin_->setEnabled(false);
	delay_row->addWidget(hdmi_delay_spin_);
	layout->addLayout(delay_row);

	connect(set_hdmi_check_, &QCheckBox::toggled,
		hdmi_delay_spin_, &QSpinBox::setEnabled);
	return group;
}
void DeviceDialog::load(void)
{
	name_edit_->setText(QString::fromStdString(device_.name));
	ip_edit_->setText(QString::fromStdString(device_.ip));

	QString macs;
	for (const auto& mac : device_.mac_addresses)
		macs += QString::fromStdString(mac) + "\n";
	mac_edit_->setPlainText(macs.trimmed());

	switch (device_.wake_method)
	{
	case WOL_TYPE_IP:				wol_ip_->setChecked(true); break;
	case WOL_TYPE_SUBNETBROADCAST:	wol_subnet_->setChecked(true); break;
	default:						wol_broadcast_->setChecked(true); break;
	}
	subnet_edit_->setText(QString::fromStdString(device_.subnet));

	int nic_index = nic_combo_->findData(QString::fromStdString(device_.network_interface));
	nic_combo_->setCurrentIndex(nic_index >= 0 ? nic_index : 0);

	ssl_combo_->setCurrentIndex(device_.ssl ? 0 : 1);
	persistence_combo_->setCurrentIndex(
		persistence_combo_->findData(device_.persistent_connection_level));
	source_combo_->setCurrentIndex(
		source_combo_->findData(device_.sourceHdmiInput));

	check_hdmi_check_->setChecked(device_.check_hdmi_input_when_power_off);
	set_hdmi_check_->setChecked(device_.set_hdmi_input_on_power_on);
	hdmi_delay_spin_->setValue(device_.set_hdmi_input_on_power_on_delay);
}
void DeviceDialog::store(void)
{
	device_.name = name_edit_->text().trimmed().toStdString();
	device_.ip = ip_edit_->text().trimmed().toStdString();

	device_.mac_addresses.clear();
	const auto lines = mac_edit_->toPlainText().split('\n', Qt::SkipEmptyParts);
	for (const auto& line : lines)
	{
		QString mac = line.trimmed();
		if (!mac.isEmpty())
			device_.mac_addresses.push_back(mac.toStdString());
	}

	if (wol_ip_->isChecked())
		device_.wake_method = WOL_TYPE_IP;
	else if (wol_subnet_->isChecked())
		device_.wake_method = WOL_TYPE_SUBNETBROADCAST;
	else
		device_.wake_method = WOL_TYPE_NETWORKBROADCAST;

	device_.subnet = subnet_edit_->text().trimmed().toStdString();
	device_.network_interface = nic_combo_->currentData().toString().toStdString();
	device_.ssl = ssl_combo_->currentData().toBool();
	device_.persistent_connection_level = persistence_combo_->currentData().toInt();
	device_.sourceHdmiInput = source_combo_->currentData().toInt();
	device_.check_hdmi_input_when_power_off = check_hdmi_check_->isChecked();
	device_.set_hdmi_input_on_power_on = set_hdmi_check_->isChecked();
	device_.set_hdmi_input_on_power_on_delay = hdmi_delay_spin_->value();
}
void DeviceDialog::onWakeMethodChanged(void)
{
	// The subnet mask only matters for the subnet-broadcast method.
	const bool needs_subnet = wol_subnet_->isChecked();
	subnet_edit_->setEnabled(needs_subnet);
	if (needs_subnet && subnet_edit_->text().isEmpty() && !ip_edit_->text().isEmpty())
		subnet_edit_->setText(QString::fromStdString(
			tools::getSubnetMask(ip_edit_->text().toStdString())));
}
void DeviceDialog::validate(void)
{
	if (!ok_button_)
		return;
	// The windows dialog kept OK disabled until name and IP were present.
	const bool ok = !name_edit_->text().trimmed().isEmpty()
		&& ip_edit_->hasAcceptableInput();
	ok_button_->setEnabled(ok);
}
