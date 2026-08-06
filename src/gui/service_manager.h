// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#pragma once
#include <QString>

// Manages the systemd --user unit that does the actual work. The windows build
// installed a SYSTEM service from its MSI; here the UI owns the unit, so that
// ticking "Automatically manage this device" genuinely takes effect.
namespace service
{
	// Unit name and the per-user path the UI writes to.
	QString unitName(void);
	QString unitPath(void);

	// Absolute path of the daemon binary. An installed copy is preferred; a
	// sibling of this executable is only a last resort. Empty if not found.
	QString daemonPath(void);

	// True when the path is inside a build tree or the user's home, i.e. not a
	// stable location for a system unit to reference.
	bool isDevelopmentPath(const QString& path);

	// ExecStart of the currently installed unit, or empty. Used to warn when a
	// previously installed unit still points somewhere unstable.
	QString installedUnitExecStart(void);

	bool isInstalled(void);
	bool isEnabled(void);
	bool isActive(void);

	// Write the unit and `systemctl --user enable --now` it. Returns false and
	// fills error on failure.
	bool install(QString& error);

	// `disable --now`. The unit file is left in place.
	bool disable(QString& error);

	// Restart only if already running, so applying settings never starts a
	// service the user did not ask for.
	void tryRestart(void);
}
