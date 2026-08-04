// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#include "idle_monitor.h"
#include "ext-idle-notify-v1-client-protocol.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <thread>
#include <wayland-client.h>

class IdleMonitor::Impl
{
public:
	Impl(unsigned timeout_seconds, Handler handler)
		: timeout_seconds_(timeout_seconds)
		, handler_(std::move(handler))
	{
	}
	~Impl() { stop(); }

	bool start(void);
	void stop(void);
	void run(void);
	bool arm(void);
	void disarm(void);

	// wl_registry
	static void onGlobal(void*, struct wl_registry*, uint32_t, const char*, uint32_t);
	static void onGlobalRemove(void*, struct wl_registry*, uint32_t);
	// ext_idle_notification_v1
	static void onIdled(void*, struct ext_idle_notification_v1*);
	static void onResumed(void*, struct ext_idle_notification_v1*);

	// Vtables handed to libwayland. Members, because they name the callbacks
	// above and Impl itself is private to IdleMonitor.
	static inline const struct wl_registry_listener REGISTRY_LISTENER = {
		&Impl::onGlobal,
		&Impl::onGlobalRemove,
	};
	static inline const struct ext_idle_notification_v1_listener NOTIFICATION_LISTENER = {
		&Impl::onIdled,
		&Impl::onResumed,
	};

	void setError(const std::string& message)
	{
		std::lock_guard<std::mutex> lock(error_mutex_);
		last_error_ = message;
	}

	unsigned timeout_seconds_;
	Handler handler_;

	struct wl_display* display_ = nullptr;
	struct wl_registry* registry_ = nullptr;
	struct wl_seat* seat_ = nullptr;
	struct ext_idle_notifier_v1* notifier_ = nullptr;
	struct ext_idle_notification_v1* notification_ = nullptr;

	std::thread thread_;
	std::atomic<bool> running_{ false };
	std::atomic<bool> idle_{ false };
	std::atomic<bool> rearm_requested_{ false };
	std::mutex error_mutex_;
	std::string last_error_;
};

void IdleMonitor::Impl::onGlobal(void* data, struct wl_registry* registry,
	uint32_t name, const char* interface, uint32_t version)
{
	auto* self = static_cast<Impl*>(data);
	if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0)
	{
		self->notifier_ = static_cast<ext_idle_notifier_v1*>(
			wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1));
	}
	else if (strcmp(interface, wl_seat_interface.name) == 0 && !self->seat_)
	{
		// Idle is tracked per seat; the first one is the user's.
		uint32_t bind_version = version < 7 ? version : 7;
		self->seat_ = static_cast<wl_seat*>(
			wl_registry_bind(registry, name, &wl_seat_interface, bind_version));
	}
}
void IdleMonitor::Impl::onGlobalRemove(void*, struct wl_registry*, uint32_t)
{
}
void IdleMonitor::Impl::onIdled(void* data, struct ext_idle_notification_v1*)
{
	auto* self = static_cast<Impl*>(data);
	self->idle_ = true;
	if (self->handler_)
		self->handler_(true);
}
void IdleMonitor::Impl::onResumed(void* data, struct ext_idle_notification_v1*)
{
	auto* self = static_cast<Impl*>(data);
	self->idle_ = false;
	if (self->handler_)
		self->handler_(false);
}
bool IdleMonitor::Impl::arm(void)
{
	disarm();
	if (!notifier_ || !seat_)
		return false;

	notification_ = ext_idle_notifier_v1_get_idle_notification(
		notifier_, timeout_seconds_ * 1000, seat_);
	if (!notification_)
		return false;

	ext_idle_notification_v1_add_listener(notification_, &NOTIFICATION_LISTENER, this);
	wl_display_flush(display_);
	return true;
}
void IdleMonitor::Impl::disarm(void)
{
	if (notification_)
	{
		ext_idle_notification_v1_destroy(notification_);
		notification_ = nullptr;
	}
}
bool IdleMonitor::Impl::start(void)
{
	display_ = wl_display_connect(nullptr);
	if (!display_)
	{
		setError("no Wayland display (is WAYLAND_DISPLAY set?)");
		return false;
	}

	registry_ = wl_display_get_registry(display_);
	wl_registry_add_listener(registry_, &REGISTRY_LISTENER, this);
	// Two roundtrips: the first delivers the globals, the second any events
	// produced while binding them.
	wl_display_roundtrip(display_);
	wl_display_roundtrip(display_);

	if (!notifier_)
	{
		setError("compositor does not implement ext-idle-notify-v1");
		stop();
		return false;
	}
	if (!seat_)
	{
		setError("no wl_seat advertised by the compositor");
		stop();
		return false;
	}
	if (!arm())
	{
		setError("failed to create the idle notification");
		stop();
		return false;
	}

	running_ = true;
	thread_ = std::thread([this] { run(); });
	return true;
}
void IdleMonitor::Impl::run(void)
{
	const int fd = wl_display_get_fd(display_);

	while (running_)
	{
		if (rearm_requested_.exchange(false))
			arm();

		// Standard wayland prepare_read dance, so the timeout below cannot race
		// with events arriving on another thread.
		while (wl_display_prepare_read(display_) != 0)
			wl_display_dispatch_pending(display_);

		if (wl_display_flush(display_) < 0 && errno != EAGAIN)
		{
			wl_display_cancel_read(display_);
			setError("failed to flush the Wayland connection");
			break;
		}

		struct pollfd pfd { fd, POLLIN, 0 };
		int r = poll(&pfd, 1, 200); // 200ms, so stop() is noticed promptly
		if (r > 0 && (pfd.revents & POLLIN))
		{
			if (wl_display_read_events(display_) < 0)
			{
				setError("failed to read Wayland events");
				break;
			}
			wl_display_dispatch_pending(display_);
		}
		else
		{
			wl_display_cancel_read(display_);
			if (r < 0 && errno != EINTR)
			{
				setError("poll on the Wayland fd failed");
				break;
			}
		}
	}
}
void IdleMonitor::Impl::stop(void)
{
	bool was_running = running_.exchange(false);
	if (was_running && thread_.joinable())
		thread_.join();

	disarm();
	if (notifier_)
	{
		ext_idle_notifier_v1_destroy(notifier_);
		notifier_ = nullptr;
	}
	if (seat_)
	{
		wl_seat_release(seat_);
		seat_ = nullptr;
	}
	if (registry_)
	{
		wl_registry_destroy(registry_);
		registry_ = nullptr;
	}
	if (display_)
	{
		wl_display_disconnect(display_);
		display_ = nullptr;
	}
}

IdleMonitor::IdleMonitor(unsigned timeout_seconds, Handler handler)
	: pimpl(std::make_unique<Impl>(timeout_seconds, std::move(handler)))
{
}
IdleMonitor::~IdleMonitor() = default;
bool IdleMonitor::start(void) { return pimpl->start(); }
void IdleMonitor::stop(void) { pimpl->stop(); }
bool IdleMonitor::isRunning(void) const { return pimpl->running_; }
bool IdleMonitor::isIdle(void) const { return pimpl->idle_; }
bool IdleMonitor::setTimeout(unsigned timeout_seconds)
{
	pimpl->timeout_seconds_ = timeout_seconds;
	if (!pimpl->running_)
		return true;
	// Re-arm on the monitor thread; wayland objects are not thread safe.
	pimpl->rearm_requested_ = true;
	return true;
}
std::string IdleMonitor::lastError(void) const
{
	std::lock_guard<std::mutex> lock(pimpl->error_mutex_);
	return pimpl->last_error_;
}
