// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#include "service_manager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

namespace
{
	constexpr const char* UNIT_NAME = "lgtv-companion.service";
	constexpr const char* DAEMON_NAME = "lgtv-companion-daemon";

	// Run systemctl --user and return its exit code, or -1 if it never ran.
	int systemctl(const QStringList& arguments, QString* output = nullptr)
	{
		QProcess process;
		process.start("systemctl", QStringList{ "--user" } + arguments);
		if (!process.waitForFinished(10000))
			return -1;
		if (output)
			*output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
		return process.exitCode();
	}
}

QString service::unitName(void)
{
	return UNIT_NAME;
}
QString service::unitPath(void)
{
	QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
	return base + "/systemd/user/" + UNIT_NAME;
}
QString service::daemonPath(void)
{
	// Prefer a sibling of this binary, so a build tree is self-consistent.
	QString sibling = QCoreApplication::applicationDirPath() + "/" + DAEMON_NAME;
	if (QFileInfo(sibling).isExecutable())
		return QFileInfo(sibling).absoluteFilePath();

	QString found = QStandardPaths::findExecutable(DAEMON_NAME);
	return found;
}
bool service::isInstalled(void)
{
	if (QFile::exists(unitPath()))
		return true;
	// A distribution package may have put it in a system unit directory.
	QString state;
	return systemctl({ "cat", UNIT_NAME }, &state) == 0;
}
bool service::isEnabled(void)
{
	QString state;
	systemctl({ "is-enabled", UNIT_NAME }, &state);
	return state == "enabled" || state == "enabled-runtime" || state == "static";
}
bool service::isActive(void)
{
	QString state;
	systemctl({ "is-active", UNIT_NAME }, &state);
	return state == "active";
}
bool service::install(QString& error)
{
	QString daemon = daemonPath();
	if (daemon.isEmpty())
	{
		error = QObject::tr(
			"Could not find the %1 executable. Install the application, or run "
			"the interface from the same directory as the daemon.")
			.arg(DAEMON_NAME);
		return false;
	}

	QString path = unitPath();
	if (!QDir().mkpath(QFileInfo(path).absolutePath()))
	{
		error = QObject::tr("Could not create %1").arg(QFileInfo(path).absolutePath());
		return false;
	}

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
	{
		error = QObject::tr("Could not write %1: %2").arg(path, file.errorString());
		return false;
	}

	QTextStream unit(&file);
	unit << "# Written by LGTV Linux Companion. Edits will be overwritten.\n"
		<< "[Unit]\n"
		<< "Description=LGTV Linux Companion\n"
		<< "Documentation=https://github.com/moorknet/LGTVLinuxCompanion\n"
		<< "After=graphical-session.target network-online.target\n"
		<< "PartOf=graphical-session.target\n"
		<< "\n"
		<< "[Service]\n"
		<< "Type=notify\n"
		<< "ExecStart=" << daemon << "\n"
		<< "Restart=on-failure\n"
		<< "RestartSec=5\n"
		// The display must be told to power off before the system goes down;
		// logind grants us InhibitDelayMaxSec to do it.
		<< "TimeoutStopSec=20\n"
		<< "NoNewPrivileges=true\n"
		<< "RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6 AF_NETLINK\n"
		<< "\n"
		<< "[Install]\n"
		<< "WantedBy=graphical-session.target\n";
	file.close();

	if (systemctl({ "daemon-reload" }) != 0)
	{
		error = QObject::tr("systemctl --user daemon-reload failed.");
		return false;
	}
	if (systemctl({ "enable", "--now", UNIT_NAME }) != 0)
	{
		error = QObject::tr("systemctl --user enable --now %1 failed.").arg(UNIT_NAME);
		return false;
	}
	return true;
}
bool service::disable(QString& error)
{
	if (systemctl({ "disable", "--now", UNIT_NAME }) != 0)
	{
		error = QObject::tr("systemctl --user disable --now %1 failed.").arg(UNIT_NAME);
		return false;
	}
	return true;
}
void service::tryRestart(void)
{
	systemctl({ "try-restart", UNIT_NAME });
}
