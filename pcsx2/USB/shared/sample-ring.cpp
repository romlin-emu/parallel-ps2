// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "USB/shared/sample-ring.h"

namespace usb_shared
{
	void SampleRing::Reset()
	{
		retro_atomic_int_init(&head, 0);
		retro_atomic_int_init(&tail, 0);
	}

	u32 SampleRing::Space()
	{
		const u32 h = static_cast<u32>(retro_atomic_load_acquire_int(&head));
		const u32 t = static_cast<u32>(retro_atomic_load_relaxed_int(&tail));
		return MASK - ((t - h) & MASK);
	}

	bool SampleRing::Push(s16 sample)
	{
		const u32 t = static_cast<u32>(retro_atomic_load_relaxed_int(&tail));
		if (((t + 1) & MASK) == (static_cast<u32>(retro_atomic_load_acquire_int(&head)) & MASK))
			return false;
		data[t & MASK] = sample;
		retro_atomic_store_release_int(&tail, static_cast<int>((t + 1) & MASK));
		return true;
	}

	u32 SampleRing::Count()
	{
		const u32 h = static_cast<u32>(retro_atomic_load_relaxed_int(&head));
		const u32 t = static_cast<u32>(retro_atomic_load_acquire_int(&tail));
		return (t - h) & MASK;
	}

	void SampleRing::TrimTo(u32 keep)
	{
		const u32 n = Count();
		if (n > keep)
		{
			const u32 h = static_cast<u32>(retro_atomic_load_relaxed_int(&head));
			retro_atomic_store_release_int(&head, static_cast<int>((h + n - keep) & MASK));
		}
	}

	u32 SampleRing::Pop(s16* samples, u32 max)
	{
		// One read of the count: the producer may add between two.
		const u32 count = Count();
		const u32 n = count < max ? count : max;
		const u32 h = static_cast<u32>(retro_atomic_load_relaxed_int(&head));
		for (u32 i = 0; i < n; i++)
			samples[i] = data[(h + i) & MASK];
		retro_atomic_store_release_int(&head, static_cast<int>((h + n) & MASK));
		return n;
	}
} // namespace usb_shared
