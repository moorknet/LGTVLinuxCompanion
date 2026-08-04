// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

// Daemon entry point. Replaces the windows service host (SvcMain / the SCM
// dispatch table): systemd supervises the process, so all that remains is to
// wire the two platform event sources into the Companion engine and wait.
#include "companion.h"
#include "event.h"
#include "idle_monitor.h"
#include "logind.h"
#include "app_define.h"
#include "paths.h"
#include "preferences.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <systemd/sd-daemon.h>
#include <thread>

namespace
{
	std::mutex quit_mutex;
	std::condition_variable quit_signal;
	std::atomic<bool> quit{ false };

	void onSignal(int)
	{
		quit = true;
		quit_signal.notify_all();
	}

	void usage(void)
	{
		std::cout <<
			"LGTV Linux Companion daemon " APP_VERSION "\n"
			"A Linux port of LGTV Companion by Jorgen Persson.\n"
			"\n"
			"Usage: lgtv-companion-daemon [options]\n"
			"\n"
			"  -f, --foreground   Log to stderr as well as the log file\n"
			"  -c, --config PATH  Use an alternate configuration file\n"
			"  -h, --help         Show this help\n"
			"\n"
			"Configuration: " << paths::configFile() << "\n"
			"Log:           " << paths::logFile() << "\n"
			"Control socket:" << paths::ipcSocket() << "\n";
	}
}

int main(int argc, char* argv[])
{
	std::string config_file;
	bool foreground = false;

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "-h" || arg == "--help")
		{
			usage();
			return 0;
		}
		if (arg == "-f" || arg == "--foreground")
		{
			foreground = true;
		}
		else if ((arg == "-c" || arg == "--config") && i + 1 < argc)
		{
			config_file = argv[++i];
		}
		else
		{
			std::cerr << "Unrecognised argument: " << arg << "\n\n";
			usage();
			return 2;
		}
	}

	auto note = [foreground](const std::string& message) {
		if (foreground)
			std::cerr << message << "\n";
		};

	Preferences prefs(config_file);
	if (!prefs.isInitialised())
	{
		std::cerr << "Failed to read the configuration file ("
			<< (config_file.empty() ? paths::configFile() : config_file)
			<< "). Terminating.\n";
		sd_notifyf(0, "STATUS=Failed to read the configuration file\nERRNO=%i", EIO);
		return 1;
	}

	Companion companion(prefs);

	// Power events. The handler runs while logind holds our delay inhibitor, so
	// the TV is told to power off before the system actually goes down.
	LogindMonitor power([&companion, &note](PowerEvent event) {
		note("power event: " + toString(event));
		switch (event)
		{
		case PowerEvent::Suspend:
			companion.systemEvent(EVENT_SYSTEM_SUSPEND);
			break;
		case PowerEvent::Resume:
			companion.systemEvent(EVENT_SHUTDOWN_TYPE_UNDEFINED);
			companion.systemEvent(EVENT_SYSTEM_RESUME);
			// On windows the console display-state notification fires on resume
			// and is what actually powers the TV back on. logind has no
			// equivalent signal, so synthesise it: without this the display is
			// switched off on suspend and never comes back.
			companion.systemEvent(EVENT_SYSTEM_DISPLAYON);
			break;
		case PowerEvent::Shutdown:
			// Tell the engine which kind of shutdown first; upstream derived
			// this from the windows event log, logind reports it outright.
			companion.systemEvent(EVENT_SHUTDOWN_TYPE_SHUTDOWN);
			companion.systemEvent(EVENT_SYSTEM_SHUTDOWN);
			break;
		case PowerEvent::Reboot:
			companion.systemEvent(EVENT_SHUTDOWN_TYPE_REBOOT);
			companion.systemEvent(EVENT_SYSTEM_REBOOT);
			break;
		case PowerEvent::ShutdownUnsure:
			companion.systemEvent(EVENT_SHUTDOWN_TYPE_UNSURE);
			companion.systemEvent(EVENT_SYSTEM_SHUTDOWN);
			break;
		case PowerEvent::Lock:
		case PowerEvent::Unlock:
			break;
		}
		});

	if (!power.start())
		std::cerr << "Warning: logind unavailable (" << power.lastError()
		<< "). Power events will not be handled.\n";
	else
		note("logind connected; inhibit budget "
			+ std::to_string(power.inhibitDelayMs()) + "ms");

	// Idle detection is optional and only armed when the user enabled it.
	std::unique_ptr<IdleMonitor> idle;
	if (prefs.user_idle_mode_)
	{
		idle = std::make_unique<IdleMonitor>(
			prefs.user_idle_mode_delay_ * 60,
			[&companion, &note](bool is_idle) {
				note(is_idle ? "user idle" : "user active");
				companion.systemEvent(is_idle ? EVENT_SYSTEM_USERIDLE : EVENT_SYSTEM_USERBUSY);
			});
		if (!idle->start())
			std::cerr << "Warning: idle detection unavailable ("
			<< idle->lastError() << ").\n";
		else
			note("idle detection armed at "
				+ std::to_string(prefs.user_idle_mode_delay_) + " minutes");
	}

	std::signal(SIGINT, onSignal);
	std::signal(SIGTERM, onSignal);

	companion.systemEvent(EVENT_SYSTEM_BOOT);

	// EVENT_SYSTEM_BOOT only records a timestamp; the engine tracks whether the
	// displays are on via the display-state events, and that flag starts false.
	// Left alone, the first suspend after login would decide the display was
	// already off and skip powering the TV down. Declaring the display on at
	// startup fixes that, and powers the TV on at login.
	if (prefs.power_on_at_login_)
		companion.systemEvent(EVENT_SYSTEM_DISPLAYON);

	sd_notify(0, "READY=1\nSTATUS=Watching for power events");
	note("daemon ready");

	{
		std::unique_lock<std::mutex> lock(quit_mutex);
		quit_signal.wait(lock, [] { return quit.load(); });
	}

	note("shutting down");
	sd_notify(0, "STOPPING=1\nSTATUS=Shutting down");

	if (idle)
		idle->stop();
	power.stop();

	// Let in-flight webOS traffic drain, bounded so a wedged connection cannot
	// hold up the stop.
	companion.shutdown(true);
	for (int i = 0; i < 100 && companion.isBusy(); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	companion.shutdown(false);

	return 0;
}
