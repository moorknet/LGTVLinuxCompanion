// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

// Interactive harness for the two platform event sources. Prints logind power
// events and Wayland idle transitions as they arrive, so they can be verified
// against a real session before the daemon depends on them.
//
//   lgtv-platform-probe [idle_timeout_seconds]
#include "idle_monitor.h"
#include "logind.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <thread>

namespace
{
	std::atomic<bool> quit{ false };

	void onSignal(int) { quit = true; }

	std::string stamp(void)
	{
		auto now = std::time(nullptr);
		struct tm tm_buf;
		localtime_r(&now, &tm_buf);
		char buffer[32];
		strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm_buf);
		return buffer;
	}
	void report(const std::string& source, const std::string& message)
	{
		std::cout << "[" << stamp() << "] " << std::left << std::setw(6) << source
			<< " | " << message << std::endl;
	}
}

int main(int argc, char* argv[])
{
	unsigned idle_timeout = 10;
	if (argc > 1)
		idle_timeout = static_cast<unsigned>(std::atoi(argv[1]));
	if (idle_timeout < 1)
		idle_timeout = 10;

	std::signal(SIGINT, onSignal);
	std::signal(SIGTERM, onSignal);

	std::cout << "LGTV Linux Companion - platform probe\n"
		<< "Idle timeout: " << idle_timeout << "s. Ctrl-C to stop.\n\n";

	LogindMonitor power([](PowerEvent event) {
		report("logind", "power event: " + toString(event));
		// The daemon will send the TV its power-off command here, while the
		// delay inhibitor is still held.
		});

	if (power.start())
		report("logind", "connected; inhibit delay budget is "
			+ std::to_string(power.inhibitDelayMs()) + "ms");
	else
		report("logind", "UNAVAILABLE: " + power.lastError());

	IdleMonitor idle(idle_timeout, [](bool is_idle) {
		report("idle", is_idle ? "user went idle" : "user became active");
		});

	if (idle.start())
		report("idle", "armed via ext-idle-notify-v1");
	else
		report("idle", "UNAVAILABLE: " + idle.lastError());

	if (!power.isRunning() && !idle.isRunning())
	{
		std::cerr << "\nNeither event source is available; nothing to watch.\n";
		return 1;
	}

	std::cout << "\nWatching. Try: leaving the machine alone, or "
		<< "'systemctl suspend'.\n\n";

	while (!quit)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

	std::cout << "\nStopping...\n";
	idle.stop();
	power.stop();
	return 0;
}
