// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#include "service_manager.h"
#include "app_define.h"
#include "paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <unistd.h>

namespace
{
	constexpr const char* UNIT_NAME = "lgtv-companion.service";
	constexpr const char* DAEMON_NAME = "lgtv-companion-daemon";
	constexpr const char* SYSTEM_UNIT_DIR = "/etc/systemd/system";

	// systemctl in system scope. Read-only queries need no privileges.
	int systemctl(const QStringList& arguments, QString* output = nullptr)
	{
		QProcess process;
		process.start("systemctl", arguments);
		if (!process.waitForFinished(10000))
			return -1;
		if (output)
			*output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
		return process.exitCode();
	}

	// Privileged step, via polkit. The user authenticates in pkexec's own
	// dialog; nothing here ever handles their password.
	int pkexec(const QStringList& arguments, QString* error = nullptr)
	{
		QProcess process;
		process.start("pkexec", arguments);
		if (!process.waitForFinished(120000))
		{
			if (error)
				*error = QObject::tr("Timed out waiting for authorisation.");
			return -1;
		}
		if (error && process.exitCode() != 0)
		{
			QString text = QString::fromUtf8(process.readAllStandardError()).trimmed();
			// 126/127 are pkexec's own "dismissed" and "not authorised".
			*error = text.isEmpty()
				? QObject::tr("Authorisation was declined or failed.")
				: text;
		}
		return process.exitCode();
	}
}

QString service::unitName(void)
{
	return UNIT_NAME;
}
QString service::unitPath(void)
{
	return QString("%1/%2").arg(SYSTEM_UNIT_DIR, UNIT_NAME);
}
QString service::daemonPath(void)
{
	// An installed copy always wins. A system unit must not point into a build
	// tree: that path can be moved, cleaned or rebuilt, and the service then
	// fails to start with no obvious cause.
	QString installed = QStandardPaths::findExecutable(DAEMON_NAME);
	if (!installed.isEmpty() && !isDevelopmentPath(installed))
		return installed;

	for (const QString& prefix : { "/usr/local/bin", "/usr/bin", "/opt/lgtv-companion/bin" })
	{
		QString candidate = prefix + "/" + DAEMON_NAME;
		if (QFileInfo(candidate).isExecutable())
			return candidate;
	}

	// Last resort: a sibling of this binary, which is what running from a build
	// tree gives. Callers should check isDevelopmentPath() before installing.
	QString sibling = QCoreApplication::applicationDirPath() + "/" + DAEMON_NAME;
	if (QFileInfo(sibling).isExecutable())
		return QFileInfo(sibling).absoluteFilePath();

	return installed;
}
bool service::isDevelopmentPath(const QString& path)
{
	if (path.isEmpty())
		return false;
	// Anything under a build directory, or inside the user's home, is not a
	// stable location for a system service to reference.
	static const QStringList markers = { "/build/", "/cmake-build", "/_build/" };
	for (const QString& marker : markers)
		if (path.contains(marker))
			return true;

	QString home = QDir::homePath();
	return !home.isEmpty() && path.startsWith(home + "/");
}
QString service::installedUnitExecStart(void)
{
	QString output;
	if (systemctl({ "show", "-p", "ExecStart", "--value", UNIT_NAME }, &output) != 0)
		return QString();
	// "{ path=/usr/bin/foo ; argv[]=... }"
	int start = output.indexOf("path=");
	if (start < 0)
		return QString();
	start += 5;
	int end = output.indexOf(' ', start);
	return output.mid(start, end < 0 ? -1 : end - start);
}
bool service::isInstalled(void)
{
	if (QFile::exists(unitPath()))
		return true;
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

	QString user = qEnvironmentVariable("USER");
	if (user.isEmpty())
		user = QString::fromLocal8Bit(qgetenv("LOGNAME"));
	if (user.isEmpty())
	{
		error = QObject::tr("Could not determine the current user name.");
		return false;
	}

	// Compose the unit in a temporary file, then move it into place as root.
	QTemporaryFile staged;
	staged.setAutoRemove(true);
	if (!staged.open())
	{
		error = QObject::tr("Could not create a temporary file.");
		return false;
	}

	QTextStream unit(&staged);
	unit << "# Written by LGTV Linux Companion. Edits will be overwritten.\n"
		<< "[Unit]\n"
		<< "Description=LGTV Linux Companion\n"
		<< "Documentation=https://github.com/moorknet/LGTVLinuxCompanion\n"
		// System scope on purpose: a --user unit does not exist until login, so
		// the display would still be off at the login screen, and it is torn
		// down with the graphical session before logind broadcasts
		// PrepareForShutdown, so the display would never be switched off.
		<< "After=network-online.target\n"
		<< "Wants=network-online.target\n"
		<< "\n"
		<< "[Service]\n"
		<< "Type=notify\n"
		<< "User=" << user << "\n"
		<< "ExecStart=" << daemon << "\n"
		<< "Restart=on-failure\n"
		<< "RestartSec=5\n"
		<< "TimeoutStopSec=20\n"
		// No XDG_RUNTIME_DIR in system scope; the control socket goes here.
		<< "RuntimeDirectory=" << APP_ID << "\n"
		<< "RuntimeDirectoryMode=0755\n"
		<< "NoNewPrivileges=true\n"
		<< "ProtectSystem=strict\n"
		<< "ProtectHome=read-only\n"
		<< "RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6 AF_NETLINK\n"
		// The pairing key is written back to the config file, and the log lives
		// under the user's state directory.
		<< "ReadWritePaths=" << QString::fromStdString(paths::configDir())
		<< " " << QString::fromStdString(paths::stateDir()) << "\n"
		<< "\n"
		<< "[Install]\n"
		<< "WantedBy=multi-user.target\n";
	unit.flush();
	staged.close();

	// One privileged invocation for the whole job, so the user is prompted once.
	QString script = QString(
		"install -m 0644 '%1' '%2' && "
		"systemctl daemon-reload && "
		"systemctl enable --now %3")
		.arg(staged.fileName(), unitPath(), UNIT_NAME);

	if (pkexec({ "/bin/sh", "-c", script }, &error) != 0)
		return false;

	return true;
}
bool service::disable(QString& error)
{
	if (pkexec({ "/bin/sh", "-c",
		QString("systemctl disable --now %1").arg(UNIT_NAME) }, &error) != 0)
		return false;
	return true;
}
void service::tryRestart(void)
{
	pkexec({ "/bin/sh", "-c",
		QString("systemctl try-restart %1").arg(UNIT_NAME) });
}
