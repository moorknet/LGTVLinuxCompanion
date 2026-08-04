// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#pragma once
#include <functional>
#include <memory>
#include <string>

// System power events delivered by systemd-logind over D-Bus. Replaces the
// windows service control handler (SERVICE_CONTROL_POWEREVENT / PRESHUTDOWN)
// and the Event Log ID 1074 subscription used upstream.
//
// Upstream had to string-match a localised dictionary of "restart words" from
// the windows event log to tell a reboot from a shutdown. logind reports the
// operation directly, so that whole mechanism is unnecessary here.
enum class PowerEvent
{
	Suspend,        // about to sleep; delay inhibitor is held
	Resume,         // woke up
	Shutdown,       // about to power off; delay inhibitor is held
	Reboot,         // about to reboot; delay inhibitor is held
	ShutdownUnsure, // shutting down, but the operation type was not reported
	Lock,           // session locked
	Unlock,         // session unlocked
};

std::string toString(PowerEvent event);

// True when the session's screen lock / screensaver is active, via
// org.freedesktop.ScreenSaver on the session bus. Replaces upstream's scan of
// running processes for a ".scr" windows screensaver binary. False if the
// interface is unavailable, which is the safe answer: it only ever suppresses
// activity, never triggers it.
bool isScreensaverActive(void);

// The logind session id of the current session, e.g. "2". Replaces
// WTSGetActiveConsoleSessionId(). Falls back to XDG_SESSION_ID, then "1".
std::string currentSessionId(void);

class LogindMonitor
{
public:
	// The handler runs on the monitor's own thread. For the three "about to"
	// events it is called synchronously while a delay inhibitor is held, so the
	// TV can be told to power off before the system goes down. Keep it under
	// InhibitDelayMaxUSec (5s by default) or logind proceeds regardless.
	using Handler = std::function<void(PowerEvent)>;

	explicit LogindMonitor(Handler handler);
	~LogindMonitor();

	LogindMonitor(const LogindMonitor&) = delete;
	LogindMonitor& operator=(const LogindMonitor&) = delete;

	// Connects to the system bus and starts listening. False if logind is
	// unreachable, in which case the daemon still runs, just without power
	// events.
	bool start(void);
	void stop(void);
	bool isRunning(void) const;

	// logind's configured maximum delay, in milliseconds. Handlers must finish
	// within this budget. Reads InhibitDelayMaxUSec; 5000 if unavailable.
	unsigned inhibitDelayMs(void) const;

	// Human-readable reason for the last failure, for logging.
	std::string lastError(void) const;

private:
	class Impl;
	std::unique_ptr<Impl> pimpl;
};
