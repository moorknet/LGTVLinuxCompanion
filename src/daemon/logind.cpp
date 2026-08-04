// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#include "logind.h"
#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <systemd/sd-bus.h>
#include <thread>
#include <unistd.h>

namespace
{
	constexpr const char* LOGIND_SERVICE = "org.freedesktop.login1";
	constexpr const char* LOGIND_PATH = "/org/freedesktop/login1";
	constexpr const char* LOGIND_MANAGER = "org.freedesktop.login1.Manager";

	// What we ask logind to delay. "shutdown:sleep" means we get woken before
	// either happens and the system waits for us to release the lock.
	constexpr const char* INHIBIT_WHAT = "shutdown:sleep";
	constexpr unsigned DEFAULT_INHIBIT_DELAY_MS = 5000;
}

std::string toString(PowerEvent event)
{
	switch (event)
	{
	case PowerEvent::Suspend:			return "suspend";
	case PowerEvent::Resume:			return "resume";
	case PowerEvent::Shutdown:			return "shutdown";
	case PowerEvent::Reboot:			return "reboot";
	case PowerEvent::ShutdownUnsure:	return "shutdown (type unreported)";
	case PowerEvent::Lock:				return "session lock";
	case PowerEvent::Unlock:			return "session unlock";
	}
	return "unknown";
}

bool isScreensaverActive(void)
{
	sd_bus* bus = nullptr;
	if (sd_bus_open_user(&bus) < 0)
		return false;

	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = nullptr;
	int active = 0;
	// KDE, GNOME and most others implement this; a missing service just means
	// "not locked" as far as we are concerned.
	int r = sd_bus_call_method(bus,
		"org.freedesktop.ScreenSaver",
		"/org/freedesktop/ScreenSaver",
		"org.freedesktop.ScreenSaver",
		"GetActive", &error, &reply, "");
	if (r >= 0 && reply)
	{
		sd_bus_message_read(reply, "b", &active);
		sd_bus_message_unref(reply);
	}
	sd_bus_error_free(&error);
	sd_bus_flush_close_unref(bus);
	return active != 0;
}

std::string currentSessionId(void)
{
	if (const char* id = std::getenv("XDG_SESSION_ID"))
		if (id[0] != '\0')
			return id;
	return "1";
}

class LogindMonitor::Impl
{
public:
	explicit Impl(Handler handler) : handler_(std::move(handler)) {}
	~Impl() { stop(); }

	bool start(void);
	void stop(void);
	void run(void);

	// Takes a delay inhibitor so we are notified before sleep/shutdown and the
	// system waits for us. Returns the fd, or -1. Releasing the fd releases the
	// lock and lets the system proceed.
	int takeInhibitor(void);
	void releaseInhibitor(void);
	void readInhibitDelay(void);

	static int onPrepareForSleep(sd_bus_message*, void*, sd_bus_error*);
	static int onPrepareForShutdown(sd_bus_message*, void*, sd_bus_error*);
	static int onPrepareForShutdownWithMetadata(sd_bus_message*, void*, sd_bus_error*);

	void dispatchGoingDown(PowerEvent event);

	Handler handler_;
	sd_bus* bus_ = nullptr;
	int inhibitor_fd_ = -1;
	std::thread thread_;
	std::atomic<bool> running_{ false };
	std::atomic<unsigned> inhibit_delay_ms_{ DEFAULT_INHIBIT_DELAY_MS };
	std::mutex error_mutex_;
	std::string last_error_;
	// PrepareForShutdownWithMetadata fires alongside the plain
	// PrepareForShutdown; take the richer one and suppress the duplicate.
	std::atomic<bool> shutdown_handled_{ false };

	void setError(const std::string& message)
	{
		std::lock_guard<std::mutex> lock(error_mutex_);
		last_error_ = message;
	}
};

int LogindMonitor::Impl::takeInhibitor(void)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = nullptr;
	int fd = -1;

	int r = sd_bus_call_method(bus_, LOGIND_SERVICE, LOGIND_PATH, LOGIND_MANAGER,
		"Inhibit", &error, &reply, "ssss",
		INHIBIT_WHAT,
		"LGTV Linux Companion",
		"Powering off the webOS display before the system goes down",
		"delay");
	if (r < 0)
	{
		setError(std::string("Inhibit failed: ") + (error.message ? error.message : strerror(-r)));
		sd_bus_error_free(&error);
		return -1;
	}

	int raw = -1;
	r = sd_bus_message_read(reply, "h", &raw);
	if (r >= 0 && raw >= 0)
	{
		// The fd belongs to the message; keep our own copy.
		fd = ::dup(raw);
	}
	sd_bus_message_unref(reply);
	sd_bus_error_free(&error);
	return fd;
}
void LogindMonitor::Impl::releaseInhibitor(void)
{
	if (inhibitor_fd_ >= 0)
	{
		::close(inhibitor_fd_);
		inhibitor_fd_ = -1;
	}
}
void LogindMonitor::Impl::readInhibitDelay(void)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	uint64_t usec = 0;
	int r = sd_bus_get_property_trivial(bus_, LOGIND_SERVICE, LOGIND_PATH,
		LOGIND_MANAGER, "InhibitDelayMaxUSec", &error, 't', &usec);
	if (r >= 0 && usec > 0)
		inhibit_delay_ms_ = static_cast<unsigned>(usec / 1000);
	sd_bus_error_free(&error);
}
void LogindMonitor::Impl::dispatchGoingDown(PowerEvent event)
{
	// Runs while the delay inhibitor is held: the system is waiting on us.
	if (handler_)
		handler_(event);

	// Dropping the lock tells logind it may proceed.
	releaseInhibitor();
}
int LogindMonitor::Impl::onPrepareForSleep(sd_bus_message* message, void* userdata, sd_bus_error*)
{
	auto* self = static_cast<Impl*>(userdata);
	int going_down = 0;
	if (sd_bus_message_read(message, "b", &going_down) < 0)
		return 0;

	if (going_down)
	{
		self->dispatchGoingDown(PowerEvent::Suspend);
	}
	else
	{
		// Back from sleep. Re-arm for the next cycle.
		self->inhibitor_fd_ = self->takeInhibitor();
		self->shutdown_handled_ = false;
		if (self->handler_)
			self->handler_(PowerEvent::Resume);
	}
	return 0;
}
int LogindMonitor::Impl::onPrepareForShutdown(sd_bus_message* message, void* userdata, sd_bus_error*)
{
	auto* self = static_cast<Impl*>(userdata);
	int going_down = 0;
	if (sd_bus_message_read(message, "b", &going_down) < 0)
		return 0;
	if (!going_down)
		return 0;

	// The WithMetadata variant carries the operation type; prefer it when it
	// arrives. Only act here if it did not.
	if (self->shutdown_handled_.exchange(true))
		return 0;

	self->dispatchGoingDown(PowerEvent::ShutdownUnsure);
	return 0;
}
int LogindMonitor::Impl::onPrepareForShutdownWithMetadata(sd_bus_message* message, void* userdata, sd_bus_error*)
{
	auto* self = static_cast<Impl*>(userdata);
	int going_down = 0;
	if (sd_bus_message_read(message, "b", &going_down) < 0)
		return 0;
	if (!going_down)
		return 0;

	// Metadata is a{sv}; we want the "type" string: poweroff, reboot, halt, kexec.
	PowerEvent event = PowerEvent::ShutdownUnsure;
	if (sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}") >= 0)
	{
		while (sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0)
		{
			const char* key = nullptr;
			if (sd_bus_message_read(message, "s", &key) >= 0 && key && strcmp(key, "type") == 0)
			{
				const char* type = nullptr;
				if (sd_bus_message_read(message, "v", "s", &type) >= 0 && type)
				{
					if (strcmp(type, "reboot") == 0 || strcmp(type, "kexec") == 0)
						event = PowerEvent::Reboot;
					else if (strcmp(type, "poweroff") == 0 || strcmp(type, "halt") == 0)
						event = PowerEvent::Shutdown;
				}
			}
			else
			{
				sd_bus_message_skip(message, "v");
			}
			sd_bus_message_exit_container(message);
		}
		sd_bus_message_exit_container(message);
	}

	if (self->shutdown_handled_.exchange(true))
		return 0;

	self->dispatchGoingDown(event);
	return 0;
}
bool LogindMonitor::Impl::start(void)
{
	int r = sd_bus_open_system(&bus_);
	if (r < 0)
	{
		setError(std::string("cannot connect to the system bus: ") + strerror(-r));
		return false;
	}

	r = sd_bus_match_signal(bus_, nullptr, LOGIND_SERVICE, LOGIND_PATH,
		LOGIND_MANAGER, "PrepareForSleep", onPrepareForSleep, this);
	if (r < 0)
	{
		setError(std::string("cannot subscribe to PrepareForSleep: ") + strerror(-r));
		sd_bus_unref(bus_);
		bus_ = nullptr;
		return false;
	}
	// Best effort: older systemd has no WithMetadata variant, in which case we
	// fall back to the plain signal and report ShutdownUnsure.
	sd_bus_match_signal(bus_, nullptr, LOGIND_SERVICE, LOGIND_PATH,
		LOGIND_MANAGER, "PrepareForShutdownWithMetadata",
		onPrepareForShutdownWithMetadata, this);
	sd_bus_match_signal(bus_, nullptr, LOGIND_SERVICE, LOGIND_PATH,
		LOGIND_MANAGER, "PrepareForShutdown", onPrepareForShutdown, this);

	readInhibitDelay();
	inhibitor_fd_ = takeInhibitor();

	running_ = true;
	thread_ = std::thread([this] { run(); });
	return true;
}
void LogindMonitor::Impl::run(void)
{
	while (running_)
	{
		int r = sd_bus_process(bus_, nullptr);
		if (r < 0)
		{
			setError(std::string("sd_bus_process failed: ") + strerror(-r));
			break;
		}
		if (r > 0)
			continue; // more messages queued

		// Bounded wait so stop() is observed promptly.
		r = sd_bus_wait(bus_, 200000); // 200ms
		if (r < 0 && -r != EINTR)
		{
			setError(std::string("sd_bus_wait failed: ") + strerror(-r));
			break;
		}
	}
}
void LogindMonitor::Impl::stop(void)
{
	if (!running_.exchange(false))
		return;
	if (thread_.joinable())
		thread_.join();
	releaseInhibitor();
	if (bus_)
	{
		sd_bus_flush_close_unref(bus_);
		bus_ = nullptr;
	}
}

LogindMonitor::LogindMonitor(Handler handler)
	: pimpl(std::make_unique<Impl>(std::move(handler)))
{
}
LogindMonitor::~LogindMonitor() = default;
bool LogindMonitor::start(void) { return pimpl->start(); }
void LogindMonitor::stop(void) { pimpl->stop(); }
bool LogindMonitor::isRunning(void) const { return pimpl->running_; }
unsigned LogindMonitor::inhibitDelayMs(void) const { return pimpl->inhibit_delay_ms_; }
std::string LogindMonitor::lastError(void) const
{
	std::lock_guard<std::mutex> lock(pimpl->error_mutex_);
	return pimpl->last_error_;
}
