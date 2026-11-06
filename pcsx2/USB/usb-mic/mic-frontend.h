// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "libretro.h"
#include "USB/usb-mic/audiodev.h"

#include <memory>

namespace usb_mic
{
	// Load asks the frontend for its microphone and must be called from
	// retro_load_game. Run opens the two handles while a SingStar adapter is
	// streaming and collects their samples, once per retro_run. Unload closes
	// them. Sources are only ever read on the emulator thread.
	void FrontendLoad(retro_environment_t environ_cb);
	void FrontendRun();
	void FrontendUnload();

	// Player 0 or 1. The first handle opened is the device's left channel, so
	// player 0 is opened first. Without a frontend microphone the source
	// delivers silence.
	std::unique_ptr<AudioDevice> CreateFrontendSource(unsigned player);
} // namespace usb_mic
