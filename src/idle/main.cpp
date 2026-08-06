// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

// User idle helper. The counterpart to the system service, and the reason
// upstream also shipped a per-user daemon alongside its SYSTEM service.
//
// Idle detection needs a Wayland connection, which only exists inside a
// graphical session, so it cannot live in the system service. This process runs
// in the session, watches ext-idle-notify-v1, and forwards the transitions to
// the daemon over its control socket using the same wire format upstream's
// desktop daemon used: "-daemon <session> useridle" / "userbusy".
#include "app_define.h"
#include "idle_monitor.h"
#include "ipc.h"
#include "logind.h"
#include "paths.h"
#include "preferences.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <mutex>
#include <string>
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
			"LGTV Linux Companion idle helper " APP_VERSION "\n"
			"A Linux port of LGTV Companion by Jorgen Persson.\n"
			"\n"
			"Watches the Wayland session for user idle and tells the daemon.\n"
			"Runs in your graphical session; the daemon itself is a system service.\n"
			"\n"
			"Usage: lgtv-companion-idle [options]\n"
			"\n"
			"  -f, --foreground   Report transitions on stderr\n"
			"  -h, --help         Show this help\n";
	}
}

int main(int argc, char* argv[])
{
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
			foreground = true;
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

	Preferences prefs;
	if (!prefs.isInitialised())
	{
		std::cerr << "Failed to read " << paths::configFile() << ". Terminating.\n";
		return 1;
	}
	if (!prefs.user_idle_mode_)
	{
		// Nothing to do. Exit cleanly so the unit does not look failed; it will
		// be started again when the service is restarted after a settings change.
		note("user idle mode is disabled in the configuration; nothing to do");
		return 0;
	}

	const std::string socket = paths::ipcSocket();
	const std::string session = currentSessionId();

	auto tell = [&socket, &session, &note](const char* state) {
		// "-daemon <session> useridle|userbusy", matching the daemon's parser.
		std::string message = "-daemon ";
		message += session.empty() ? "1" : session;
		message += " ";
		message += state;
		if (!IpcClient::sendOneShot(socket, message, nullptr, 2000))
			note(std::string("could not reach the daemon at ") + socket);
		};

	IdleMonitor idle(prefs.user_idle_mode_delay_ * 60,
		[&tell, &note](bool is_idle) {
			note(is_idle ? "user idle" : "user active");
			tell(is_idle ? "useridle" : "userbusy");
		});

	if (!idle.start())
	{
		std::cerr << "Idle detection unavailable: " << idle.lastError() << "\n";
		return 1;
	}
	note("watching, idle timeout "
		+ std::to_string(prefs.user_idle_mode_delay_) + " minutes");

	std::signal(SIGINT, onSignal);
	std::signal(SIGTERM, onSignal);

	{
		std::unique_lock<std::mutex> lock(quit_mutex);
		quit_signal.wait(lock, [] { return quit.load(); });
	}

	// Leaving the user marked idle would strand the display off.
	if (idle.isIdle())
		tell("userbusy");

	idle.stop();
	return 0;
}
