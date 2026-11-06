// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "USB/libretro-usb/qusb.h"
#include "USB/libretro-usb/usb-state.h"

namespace usb_buzz
{
	/* A Buzz! receiver with four handsets, on the given OHCI port. */
	USBDevice* usb_buzz_create(u32 port);

	/* Takes the handsets' latched buttons, once per USB frame. */
	void usb_buzz_update(USBDevice* dev);

	/* Saves or loads the last report sent, beyond what every USB device has. */
	void usb_buzz_freeze(USBDevice* dev, UsbState& st);
} // namespace usb_buzz
