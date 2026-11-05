// SPDX-FileCopyrightText: 2002-2021 PCSX2 Dev Team
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <libretro.h>
#include "../USB/USB.h"

#define RETRO_DEVICE_KEYBOARD_AND_MOUSE RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_KEYBOARD, 0)

struct USBSelection
{
	int device[2];
	unsigned input[2];
};

inline USBSelection SelectUSBDevices(const int* selected)
{
	USBSelection result = {{USB_DEV_NONE, USB_DEV_NONE}, {0, 1}};
	// A combined selection reserves both ports; port 0 wins ties.
	for (unsigned port = 0; port < 2; port++)
		if (selected[port] == RETRO_DEVICE_KEYBOARD_AND_MOUSE)
			return {{USB_DEV_KEYBOARD, USB_DEV_MOUSE}, {port, port}};
	for (unsigned port = 0; port < 2; port++)
	{
		if (selected[port] == RETRO_DEVICE_KEYBOARD)
			result.device[port] = USB_DEV_KEYBOARD;
		else if (selected[port] == RETRO_DEVICE_MOUSE)
			result.device[port] = USB_DEV_MOUSE;
	}
	return result;
}
