// LGTV Linux Companion — a Linux port of LGTV Companion.
//
// Copyright © 2021-2026 Jörgen Persson
// Licensed under the MIT License. See the LICENSE file at the repository root
// for the full license text, which must accompany all copies.

#pragma once
#include <string>
#include <vector>

// Strings are UTF-8 std::string throughout. The upstream widen()/narrow()
// helpers existed only to cross the windows UTF-16 boundary and are gone.
namespace tools
{
	std::string								tolower(std::string input);
	std::vector<std::string>				stringsplit(std::string, std::string);
	void									replaceAllInPlace(std::string& str, const std::string& from, const std::string& to);
	std::string								validateArgument(std::string argument, std::string validation_list);
	bool									compareUsingWildcard(const std::string& text, const std::string& pattern);

	// Network helpers, backed by getifaddrs(3).
	// Each entry is "a.b.c.d/cidr" for every non-loopback IPv4 interface.
	std::vector <std::string>				getLocalIP(void);
	std::string								getSubnetMask(std::string ip);
	bool									isSameSubnet(const char* ip1, const char* ip2, const char* subnetMask);

	// Source address the kernel would use to reach the given destination, or ""
	// on failure. Replaces GetBestRoute2(); a connect() on a UDP socket performs
	// the route lookup without sending anything.
	std::string								getSourceIPforDestination(const std::string& destination_ip);
	// Name of the interface owning the given local address, or "".
	std::string								getInterfaceForIP(const std::string& ip);
	// IPv4 address of the named interface, e.g. "enp5s0" -> "192.168.1.9", or "".
	std::string								getIPforInterface(const std::string& interface_name);
	// Names of interfaces that currently have carrier, i.e. a live cable or
	// association. True long before DHCP has finished.
	std::vector<std::string>				getInterfacesWithCarrier(void);
	// Block until some interface has carrier, or the timeout elapses.
	bool									waitForCarrier(int timeout_ms);

	// Send a wake-on-lan magic packet as a raw ethernet frame (EtherType 0x0842)
	// out of the named interface.
	//
	// Unlike the UDP path this needs no IP address, no route and no DHCP lease,
	// so it works during the window after resume when the interface has carrier
	// but NetworkManager has not finished reconfiguring it. That window is
	// routinely ten seconds or more, which is dead time the display could have
	// spent waking up.
	//
	// Requires CAP_NET_RAW. Returns false (setting error) when unavailable, and
	// callers should fall back to the UDP path rather than treat it as fatal.
	bool									sendMagicPacketRaw(const std::string& interface_name,
												const std::string& mac,
												std::string& error);

	// Block until a route to the given address exists, or the timeout elapses.
	// Returns true if the network became usable. Needed on resume: logind
	// reports the wakeup before the interface is back, and every packet sent
	// before that fails with ENETUNREACH.
	bool									waitForNetwork(const std::string& destination_ip, int timeout_ms);
	// Broadcast address of the interface owning the given local address, or "".
	std::string								getBroadcastForIP(const std::string& ip);
}
