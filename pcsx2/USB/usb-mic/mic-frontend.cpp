// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "USB/usb-mic/mic-frontend.h"
#include "USB/shared/sample-ring.h"

#include "common/Pcsx2Defs.h"

#include <retro_atomic.h>

#include <string.h>

namespace usb_mic
{
	namespace
	{
		// The adapter's rate until the game picks one of those it offers.
		constexpr u32 DEFAULT_RATE = 48000;

		// One player: the frontend thread resamples into the ring at the rate
		// the game set, which the emulator thread publishes in game_rate.
		struct Player
		{
			usb_shared::SampleRing ring;
			retro_atomic_int_t game_rate;
			retro_microphone_t* handle;
			u32 rate;
			u32 out_rate;
			u64 phase;
		};

		retro_atomic_int_t s_wanted;
		// Set while both handles are open, so the device streams what was
		// captured; otherwise it streams silence, as with no capture device.
		retro_atomic_int_t s_live;
		Player s_players[2];

		// Main thread only.
		bool s_available;
		bool s_failed;
		retro_microphone_interface s_if;

		void CloseHandles()
		{
			retro_atomic_store_release_int(&s_live, 0);
			for (Player& p : s_players)
			{
				if (p.handle)
					s_if.close_mic(p.handle);
				p.handle = nullptr;
			}
		}

		// Opens player 0 first, then player 1; any failure closes both.
		bool OpenHandles()
		{
			for (Player& p : s_players)
			{
				retro_microphone_params_t params = {DEFAULT_RATE};
				p.handle = s_if.open_mic(&params);
				if (!p.handle || !s_if.get_params(p.handle, &params) || !params.rate || !s_if.set_mic_state(p.handle, true))
				{
					if (p.handle)
						s_if.close_mic(p.handle);
					p.handle = nullptr;
					CloseHandles();
					log_cb(RETRO_LOG_WARN, "SingStar: the frontend could not open its microphones\n");
					return false;
				}
				p.rate = params.rate;
				p.phase = 0;
			}
			retro_atomic_store_release_int(&s_live, 1);
			return true;
		}

		void ReadHandle(Player& p)
		{
			s16 in[1024];
			int n;
			const u32 out_rate = static_cast<u32>(retro_atomic_load_acquire_int(&p.game_rate));
			if (out_rate != p.out_rate)
			{
				p.out_rate = out_rate;
				p.phase = 0;
			}
			while ((n = s_if.read_mic(p.handle, in, C89_ARRAY_SIZE(in))) > 0)
			{
				// Nearest-sample resampling: each output sample steps the input
				// by the frontend's rate. A full ring drops the rest of the chunk.
				const u64 end = static_cast<u64>(n) * out_rate;
				for (; p.phase < end; p.phase += p.rate)
				{
					if (!p.ring.Push(in[p.phase / out_rate]))
					{
						p.phase = end;
						break;
					}
				}
				p.phase -= end;
				if (n < static_cast<int>(C89_ARRAY_SIZE(in)))
					break;
			}
		}

		class FrontendSource final : public AudioDevice
		{
		public:
			explicit FrontendSource(unsigned player)
				: AudioDevice(1)
				, m_player(player)
			{
			}

			bool Start() override
			{
				retro_atomic_store_release_int(&s_wanted, 1);
				return true;
			}

			void Stop() override
			{
				retro_atomic_store_release_int(&s_wanted, 0);
			}

			// Like upstream's capture device: the frames captured so far, or,
			// with nothing capturing, *size untouched so a whole packet of
			// silence goes out.
			bool GetFrames(u32* size) override
			{
				if (retro_atomic_load_acquire_int(&s_live))
					*size = s_players[m_player].ring.Count();
				return true;
			}

			u32 GetBuffer(s16* buff, u32 len) override
			{
				if (!retro_atomic_load_acquire_int(&s_live))
				{
					memset(buff, 0, len * sizeof(buff[0]));
					return len;
				}
				// A game that stops listening fills the ring; when it listens
				// again a backlog past 32 ms is cut to the newest 20 ms.
				Player& p = s_players[m_player];
				const u32 rate = static_cast<u32>(retro_atomic_load_relaxed_int(&p.game_rate));
				if (p.ring.Count() > rate * 32 / 1000)
					p.ring.TrimTo(rate * 20 / 1000);
				return p.ring.Pop(buff, len);
			}

			void SetResampling(int samplerate) override
			{
				retro_atomic_store_release_int(&s_players[m_player].game_rate, samplerate);
			}

		private:
			unsigned m_player;
		};
	} // namespace

	void FrontendLoad(retro_environment_t environ_cb)
	{
		retro_atomic_int_init(&s_wanted, 0);
		retro_atomic_int_init(&s_live, 0);
		for (Player& p : s_players)
		{
			p.ring.Reset();
			retro_atomic_int_init(&p.game_rate, DEFAULT_RATE);
			p.handle = nullptr;
			p.out_rate = DEFAULT_RATE;
		}
		memset(&s_if, 0, sizeof(s_if));
		s_if.interface_version = RETRO_MICROPHONE_INTERFACE_VERSION;
		s_available = environ_cb(RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE, &s_if) &&
			s_if.interface_version && s_if.open_mic && s_if.close_mic && s_if.get_params &&
			s_if.set_mic_state && s_if.read_mic;
		s_failed = false;
		if (!s_available)
			log_cb(RETRO_LOG_WARN, "SingStar: the frontend has no microphone interface; the players hear silence\n");
	}

	void FrontendRun()
	{
		if (!s_available)
			return;
		if (retro_atomic_load_acquire_int(&s_wanted) == 0)
		{
			CloseHandles();
			s_failed = false;
			return;
		}
		if (!s_players[0].handle && !s_failed)
			s_failed = !OpenHandles();
		if (!s_players[0].handle)
			return;
		for (Player& p : s_players)
			ReadHandle(p);
	}

	void FrontendUnload()
	{
		CloseHandles();
		s_available = false;
		s_failed = false;
	}

	std::unique_ptr<AudioDevice> CreateFrontendSource(unsigned player)
	{
		return std::make_unique<FrontendSource>(player);
	}
} // namespace usb_mic
