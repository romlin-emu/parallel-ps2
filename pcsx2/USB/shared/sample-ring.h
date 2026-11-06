// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Types.h"

#include "retro_atomic.h"

// Shared by the USB devices that carry audio across threads.
namespace usb_shared
{
	// 16-bit samples from the thread that captures to the thread that runs
	// the device, without a lock: a single-producer single-consumer ring.
	// Full, the producer drops what it has; the consumer trims a backlog
	// itself.
	struct SampleRing
	{
		static constexpr u32 SIZE = 2048;
		static constexpr u32 MASK = SIZE - 1;

		s16 data[SIZE];
		retro_atomic_int_t head; // consumer's, next to read
		retro_atomic_int_t tail; // producer's, next to write

		void Reset();
		// Producer: room for how many more.
		u32 Space();
		// Producer: adds one; false and nothing added when full.
		bool Push(s16 sample);
		// Consumer: how many are waiting.
		u32 Count();
		// Consumer: drops all but the newest keep.
		void TrimTo(u32 keep);
		// Consumer: takes up to max; returns how many.
		u32 Pop(s16* samples, u32 max);
	};
} // namespace usb_shared
