// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Types.h"
#include "USB/libretro-usb/usb-state.h"

struct USBDevice;

namespace usb_mic
{
	/* A SingStar microphone adapter on the given OHCI port. Player 1 and player
	 * 2 read the frontend's first and second microphones; see mic-frontend.h. */
	USBDevice* usb_mic_create_singstar(u32 port);

	/* Saves or loads the adapter's volume, mute, rates and interface. The audio
	 * in flight is not part of the state. */
	void usb_mic_freeze(USBDevice* dev, UsbState& st);
} // namespace usb_mic
