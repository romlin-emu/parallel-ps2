/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2020  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "common/Pcsx2Types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_NUM_PORTS 2

enum UsbPortDevice
{
	USB_DEV_NONE = 0,
	USB_DEV_KEYBOARD,
	USB_DEV_MOUSE
};

void USBSetPortDevice(unsigned port, int device, unsigned input_port);

enum UsbCamera
{
	USB_CAMERA_NONE = 0,
	USB_CAMERA_EYETOY,
	USB_CAMERA_CAPTURE_EYE
};

void USBSetCamera(int camera);

/* The accessory on the first USB port no other device claims (core option).
 * Buzz! handsets are read from the frontend's controller ports 1-4, and the
 * SingStar microphones are the frontend's microphones. */
enum UsbAccessory
{
	USB_ACCESSORY_NONE = 0,
	USB_ACCESSORY_BUZZ,
	USB_ACCESSORY_SINGSTAR
};

void USBSetAccessory(int accessory);
int USBGetAccessory(void);

/* Buzz! handset n (0-3): a five-bit mask of its buttons, bit 0 red, 1 yellow,
 * 2 green, 3 orange, 4 blue, latched like the pads. */
enum
{
	USB_BUZZ_HANDSETS = 4,
	USB_BUZZ_BUTTONS = 5
};
u32 PADGetBuzzButtons(unsigned handset);

/* The device on a port, or NULL; for the harnesses under tests/. */
struct USBDevice;
struct USBDevice* USBGetPortDevice(unsigned port);

size_t USBStateSize(void);
void USBStateWrite(void* dst);
size_t USBStateStrip(const void* state, size_t size);
void USBStateRead(const void* state, size_t size);

void USBinit(void);
void USBasync(u32 cycles);
void USBshutdown(void);
void USBclose(void);
bool USBopen(void);
void USBreset(void);

u8 USBread8(u32 addr);
u16 USBread16(u32 addr);
u32 USBread32(u32 addr);
void USBwrite8(u32 addr, u8 value);
void USBwrite16(u32 addr, u16 value);
void USBwrite32(u32 addr, u32 value);

#ifdef __cplusplus
}
#endif
