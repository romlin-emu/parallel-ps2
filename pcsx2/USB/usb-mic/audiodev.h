// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Types.h"

enum MicMode
{
	MIC_MODE_NONE,
	MIC_MODE_SINGLE,
	MIC_MODE_SEPARATE,
	// Use same source for both player or
	// left channel for P1 and right for P2 if stereo.
	MIC_MODE_SHARED
};

// A microphone the USB device reads mono 16-bit frames from.
class AudioDevice
{
public:
	explicit AudioDevice(u32 channels)
		: mChannels(channels)
	{
	}
	virtual ~AudioDevice() = default;

	// Fills up to len frames and returns how many it wrote.
	virtual u32 GetBuffer(s16* buff, u32 len) = 0;
	// False when nothing can be read; otherwise *size is the most GetBuffer
	// will be asked for, and may be lowered to what is recorded.
	virtual bool GetFrames(u32* size) = 0;
	virtual void SetResampling(int samplerate) = 0;
	u32 GetChannels() { return mChannels; }

	virtual bool Start() = 0;
	virtual void Stop() = 0;

protected:
	u32 mChannels;
};
