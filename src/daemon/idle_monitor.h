// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#pragma once
#include <functional>
#include <memory>
#include <string>

// User idle detection on Wayland via ext-idle-notify-v1.
//
// Replaces the windows GetLastInputInfo() polling loop. The compositor tracks
// input and tells us when the timeout elapses, so there is nothing to poll and
// no need for the raw input hooks upstream used.
//
// Two upstream features are deliberately absent:
//   - fullscreen detection: Wayland exposes no portable way for one client to
//     learn that another is fullscreen.
//   - the "Video Wake Lock" NT power-request query: browsers and players inhibit
//     idle through the compositor itself (zwp_idle_inhibit_manager_v1), so the
//     compositor already accounts for it and simply will not report idle.
class IdleMonitor
{
public:
	// Called on the monitor's own thread when the user goes idle (true) or
	// becomes active again (false).
	using Handler = std::function<void(bool idle)>;

	IdleMonitor(unsigned timeout_seconds, Handler handler);
	~IdleMonitor();

	IdleMonitor(const IdleMonitor&) = delete;
	IdleMonitor& operator=(const IdleMonitor&) = delete;

	// Connects to the compositor and arms the notification. False if there is no
	// Wayland display or the compositor lacks ext-idle-notify-v1.
	bool start(void);
	void stop(void);
	bool isRunning(void) const;
	bool isIdle(void) const;

	// Re-arm with a new timeout. Safe to call while running.
	bool setTimeout(unsigned timeout_seconds);

	std::string lastError(void) const;

private:
	class Impl;
	std::unique_ptr<Impl> pimpl;
};
