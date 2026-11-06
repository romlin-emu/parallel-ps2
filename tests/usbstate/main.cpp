// The USB block of a savestate (libretro/USB.cpp): write, find, strip, read.
//
// A keyboard on port 0 and an EyeToy on the free port, through the real
// OHCI controller and device models. A register is written into the EyeToy
// through its control endpoint, the state is written, the device is reset,
// and the state is read back: the register must be back, and the devices
// must be the ones that were there, not replugged. Then the ways a state
// can disagree with the machine, each of which must replug rather than half
// load: no block at all, a port whose device changed, a slot naming a
// configuration or interface the device does not have.
//
// The frontend's camera and microphone are stubs, so the EyeToy opens
// neither V4L2 nor ALSA here. Buzz! handsets and the SingStar microphones
// are attached as the accessory option selects, and are checked the same
// way: a report on each change, the last one kept across a state.

#include "USB/USB.h"
#include "PAD/USBSelection.h"
#include "USB/libretro-usb/USBinternal.h"
#include "USB/libretro-usb/qusb.h"
#include "USB/libretro-usb/desc.h"
#include "USB/usb-eyetoy/eyetoy-frontend.h"
#include "USB/usb-mic/mic-frontend.h"
#include "MemoryTypes.h"

#include "libretro.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- what the core provides around USB.cpp ---- */

static void stub_log(enum retro_log_level level, const char* fmt, ...)
{
	va_list ap;
	if (level < RETRO_LOG_WARN)
		return;
	va_start(ap, fmt);
	printf("    [log] ");
	vprintf(fmt, ap);
	va_end(ap);
}
retro_log_printf_t log_cb = stub_log;

IopVM_MemoryAllocMess* iopMem;

void usbIrq(int) {}

static int16_t RETRO_CALLCONV stub_input(unsigned, unsigned, unsigned, unsigned)
{
	return 0;
}
retro_input_state_t PADGetInputStateCallback(void)
{
	return stub_input;
}

/* Buzz! handsets: the buttons the frontend's controller ports report,
 * per handset, as PADGetBuzzButtons latches them. */
static u32 s_buzz[USB_BUZZ_HANDSETS];
u32 PADGetBuzzButtons(unsigned handset)
{
	return handset < USB_BUZZ_HANDSETS ? s_buzz[handset] : 0;
}

/* A frontend with a camera that delivers nothing, and a microphone whose
 * handles 1 and 2 (the first and second opened) give loud samples of
 * opposite sign, so the channel each one lands in is visible. */
static unsigned s_mic_opens;
static bool RETRO_CALLCONV cam_start(void) { return true; }
static void RETRO_CALLCONV cam_stop(void) {}
static retro_microphone_t* RETRO_CALLCONV mic_open(const retro_microphone_params_t*)
{
	return (retro_microphone_t*)(uintptr_t)(++s_mic_opens);
}
static void RETRO_CALLCONV mic_close(retro_microphone_t*) {}
static bool RETRO_CALLCONV mic_get_params(const retro_microphone_t*, retro_microphone_params_t* p) { p->rate = 48000; return true; }
static bool RETRO_CALLCONV mic_set_state(retro_microphone_t*, bool) { return true; }
static bool RETRO_CALLCONV mic_get_state(const retro_microphone_t*) { return true; }
static int RETRO_CALLCONV mic_read(retro_microphone_t* mic, int16_t* samples, size_t)
{
	const int16_t level = (uintptr_t)mic == 1 ? 12000 : -12000;
	for (int i = 0; i < 480; i++)
		samples[i] = level;
	return 480;
}

static bool RETRO_CALLCONV stub_environ(unsigned cmd, void* data);

static bool RETRO_CALLCONV stub_environ_nomic(unsigned cmd, void* data)
{
	if (cmd == RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE)
		return false;
	return stub_environ(cmd, data);
}

static bool RETRO_CALLCONV stub_environ(unsigned cmd, void* data)
{
	if (cmd == RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE)
	{
		retro_camera_callback* cb = (retro_camera_callback*)data;
		cb->start = cam_start;
		cb->stop = cam_stop;
		return true;
	}
	if (cmd == RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE)
	{
		retro_microphone_interface* mi = (retro_microphone_interface*)data;
		mi->open_mic = mic_open;
		mi->close_mic = mic_close;
		mi->get_params = mic_get_params;
		mi->set_mic_state = mic_set_state;
		mi->get_mic_state = mic_get_state;
		mi->read_mic = mic_read;
		return true;
	}
	return false;
}

/* ---- the harness ---- */

static int s_bad;

static void check(bool ok, const char* what)
{
	printf("  %-60s %s\n", what, ok ? "ok" : "WRONG");
	if (!ok)
		s_bad++;
}

/* One control transfer into a device, the way the OHCI core would make it. */
static int control(USBDevice* dev, int request, int value, int index, u8* data, int length)
{
	USBPacket p;
	memset(&p, 0, sizeof(p));
	p.ep = &dev->ep_ctl;
	p.pid = USB_TOKEN_OUT;
	p.actual_length = 0;
	dev->klass.handle_control(dev, &p, request, value, index, length, data);
	return p.status;
}

static u8 eyetoy_read_reg(USBDevice* dev, int reg)
{
	u8 data[8] = {0};
	control(dev, VendorDeviceRequest | 0x1, 0, reg, data, 1);
	return data[0];
}

/* The Capture Eye (OV511+) reads its registers through request 3. */
static u8 ov511_read_reg(USBDevice* dev, int reg)
{
	u8 data[8] = {0};
	control(dev, VendorDeviceRequest | 0x3, 0, reg, data, 1);
	return data[0];
}

static void eyetoy_write_reg(USBDevice* dev, int reg, u8 value)
{
	u8 data[8] = {value};
	control(dev, VendorDeviceOutRequest | 0x1, 0, reg, data, 1);
}

/* One IN transfer on endpoint 1, the way the OHCI core would make it. Returns
 * the status; the bytes received are in buf, and actual gets their count. */
static int in_transfer(USBDevice* dev, u8* buf, unsigned size, int* actual)
{
	USBPacket p;
	memset(&p, 0, sizeof(p));
	p.ep = usb_ep_get(dev, USB_TOKEN_IN, 1);
	p.pid = USB_TOKEN_IN;
	p.buffer_ptr = buf;
	p.buffer_size = size;
	dev->klass.handle_data(dev, &p);
	*actual = p.actual_length;
	return p.status;
}

/* The buttons a Buzz! report carries: 20 bits from byte 2, bit 0 first. */
static u32 buzz_report_buttons(const u8* report)
{
	return report[2] | (report[3] << 8) | ((report[4] & 0x0F) << 16);
}

/* Where the altsetting array sits in a port's slot: the kind, then the
 * fields USBFreezeDevice writes in order. Mirrors libretro/USB.cpp. */
static size_t slot_altsetting_offset(void)
{
	USBDevice* d = nullptr;
	return 4 + sizeof(d->speed) + sizeof(d->addr) + sizeof(d->state) + sizeof(d->setup_buf) + sizeof(d->data_buf) +
		   sizeof(d->remote_wakeup) + sizeof(d->setup_state) + sizeof(d->setup_len) + sizeof(d->setup_index) +
		   sizeof(d->configuration);
}

#define USB_STATE_OHCI 10240
#define USB_STATE_SLOT 8192

int main()
{
	int selected[2] = {RETRO_DEVICE_KEYBOARD_AND_MOUSE, RETRO_DEVICE_MOUSE};
	USBSelection allocation = SelectUSBDevices(selected);
	check(allocation.device[0] == USB_DEV_KEYBOARD && allocation.device[1] == USB_DEV_MOUSE && allocation.input[1] == 0, "combined reserves both ports");
	selected[0] = RETRO_DEVICE_JOYPAD;
	allocation = SelectUSBDevices(selected);
	check(allocation.device[0] == USB_DEV_NONE && allocation.device[1] == USB_DEV_MOUSE && allocation.input[1] == 1, "releasing combined restores the other selection");
	selected[0] = selected[1] = RETRO_DEVICE_KEYBOARD_AND_MOUSE;
	allocation = SelectUSBDevices(selected);
	check(allocation.input[0] == 0 && allocation.input[1] == 0, "port zero wins combined conflict");
	static const int REG = 0x10; /* OV519_R10_H_SIZE: harmless, logged, kept */
	iopMem = (IopVM_MemoryAllocMess*)calloc(1, sizeof(IopVM_MemoryAllocMess));

	usb_eyetoy::FrontendLoad(stub_environ);
	USBSetPortDevice(0, USB_DEV_KEYBOARD, 0);
	USBSetCamera(USB_CAMERA_EYETOY);
	check(USBopen(), "USBopen with a keyboard and an EyeToy");
	USBDevice* kbd = USBGetPortDevice(0);
	USBDevice* cam = USBGetPortDevice(1);
	check(kbd && cam, "keyboard on port 0, EyeToy on port 1");
	USBSetPortDevice(0, 999, 0);
	USBSetPortDevice(USB::NUM_PORTS, USB_DEV_MOUSE, 0);
	USBreset();
	kbd = USBGetPortDevice(0);
	cam = USBGetPortDevice(1);
	check(kbd && cam, "invalid device and port leave the selection intact");

	const size_t size = USBStateSize();
	u8* state = (u8*)malloc(size);
	u8* other = (u8*)malloc(size);
	check(size == USB_STATE_OHCI + 2 * USB_STATE_SLOT + 16, "block is the fixed size");

	/* A register the state must carry: a value that is not its default,
	 * so a reset is told from a restore. Device identity is read through
	 * it too: a replugged device is back at the default, whatever address
	 * its object landed at. */
	const u8 def = eyetoy_read_reg(cam, REG);
	const u8 val = def ^ 0x55;
	eyetoy_write_reg(cam, REG, val);
	check(eyetoy_read_reg(cam, REG) == val, "EyeToy register written through the control endpoint");
	/* And a configuration, as the guest would set. */
	usb_desc_set_config(cam, 1);

	USBStateWrite(state);
	check(!memcmp(state + size - 8, "LRPS2USB", 8), "block ends on its magic");
	check(USBStateStrip(state, size) == 0, "strip finds the block");
	memset(other, 0xA5, size);
	check(USBStateStrip(other, size) == size, "strip leaves a state without the block alone");

	/* The state puts the register back on the same device. */
	cam->klass.handle_reset(cam);
	check(eyetoy_read_reg(cam, REG) == def, "device reset forgets the register");
	USBStateRead(state, size);
	check(USBGetPortDevice(1) == cam && USBGetPortDevice(0) == kbd, "a matching state keeps the devices");
	check(eyetoy_read_reg(cam, REG) == val, "a matching state restores the register");
	check(cam->configuration == 1, "a matching state restores the configuration");

	/* No block: everything replugged, nothing read. */
	eyetoy_write_reg(cam, REG, val);
	USBStateRead(other, size);
	cam = USBGetPortDevice(1);
	kbd = USBGetPortDevice(0);
	check(cam && kbd && eyetoy_read_reg(cam, REG) == def, "a state without the block plugs the devices in afresh");

	/* A slot naming an interface the device does not have: that port is
	 * replugged, the other port still loads. */
	memcpy(other, state, size);
	{
		int bad = 7;
		memcpy(other + USB_STATE_OHCI + 1 * USB_STATE_SLOT + slot_altsetting_offset(), &bad, sizeof(bad));
	}
	eyetoy_write_reg(cam, REG, val ^ 0x0F);
	USBStateRead(other, size);
	check(eyetoy_read_reg(cam, REG) == def, "a slot with an unknown interface replugs rather than half loads");
	check(USBGetPortDevice(0) == kbd, "the other port is untouched by it");

	/* A slot naming a configuration the device does not have. */
	memcpy(other, state, size);
	{
		int bad = 9;
		memcpy(other + USB_STATE_OHCI + 1 * USB_STATE_SLOT + slot_altsetting_offset() - sizeof(int), &bad, sizeof(bad));
	}
	eyetoy_write_reg(cam, REG, val ^ 0x0F);
	USBStateRead(other, size);
	check(eyetoy_read_reg(cam, REG) == def, "a slot with an unknown configuration replugs rather than half loads");

	/* The port holds another camera now: its slot is skipped, not loaded. */
	USBSetCamera(USB_CAMERA_CAPTURE_EYE);
	USBreset();
	USBDevice* eye = USBGetPortDevice(1);
	check(eye != nullptr, "USBreset plugs the new camera in");
	const u8 eye_def = ov511_read_reg(eye, REG);
	USBStateRead(state, size);
	check(USBGetPortDevice(1) == eye && ov511_read_reg(eye, REG) == eye_def,
		"a state for another device on the port replugs rather than loads");

	/* A state written with the camera unplugged has an empty slot; reading
	 * it with a camera plugged in replugs the camera. */
	USBSetCamera(USB_CAMERA_NONE);
	USBreset();
	check(USBGetPortDevice(1) == nullptr, "no camera, port 1 empty");
	USBStateWrite(other);
	USBSetCamera(USB_CAMERA_EYETOY);
	USBreset();
	cam = USBGetPortDevice(1);
	eyetoy_write_reg(cam, REG, val);
	USBStateRead(other, size);
	check(USBGetPortDevice(1) == cam && eyetoy_read_reg(cam, REG) == def, "an empty slot replugs the device, so it starts afresh");

	/* Buzz!: attached on the free port, one report per change of the
	 * handsets, each of the 20 buttons its own bit, and the last report
	 * kept across a state. The handsets are read through USBasync, as the
	 * core runs them. */
	USBSetCamera(USB_CAMERA_NONE);
	USBSetAccessory(USB_ACCESSORY_BUZZ);
	USBreset();
	USBDevice* buzz = USBGetPortDevice(1);
	check(buzz && USBGetPortDevice(0), "Buzz! receiver on the free port, keyboard kept");
	usb_desc_set_config(buzz, 1);
	check(buzz->configuration == 1, "Buzz! receiver enumerates");

	u8 report[8];
	int len = 0;
	check(in_transfer(buzz, report, sizeof(report), &len) == USB_RET_NAK, "Buzz! idle: NAK");

	bool buttons_ok = true;
	for (u32 i = 0; i < USB_BUZZ_HANDSETS * USB_BUZZ_BUTTONS; i++)
	{
		s_buzz[i / USB_BUZZ_BUTTONS] = 1u << (i % USB_BUZZ_BUTTONS);
		USBasync(0);
		const int pressed = in_transfer(buzz, report, sizeof(report), &len);
		buttons_ok = buttons_ok && pressed == USB_RET_SUCCESS && len == 5 && buzz_report_buttons(report) == 1u << i;
		buttons_ok = buttons_ok && in_transfer(buzz, report, sizeof(report), &len) == USB_RET_NAK;
		s_buzz[i / USB_BUZZ_BUTTONS] = 0;
		USBasync(0);
		buttons_ok = buttons_ok && in_transfer(buzz, report, sizeof(report), &len) == USB_RET_SUCCESS && buzz_report_buttons(report) == 0;
	}
	check(buttons_ok, "Buzz! each of 20 buttons: its report bit, NAK until changed");

	for (int h = 0; h < USB_BUZZ_HANDSETS; h++)
		s_buzz[h] = 0x1F;
	USBasync(0);
	check(in_transfer(buzz, report, sizeof(report), &len) == USB_RET_SUCCESS && buzz_report_buttons(report) == 0xFFFFF,
		"Buzz! all handsets at once in one report");
	for (int h = 0; h < USB_BUZZ_HANDSETS; h++)
		s_buzz[h] = 0;

	s_buzz[0] = 1;
	USBasync(0);
	in_transfer(buzz, report, sizeof(report), &len);
	USBStateWrite(state);
	check(!memcmp(state + size - 8, "LRPS2USB", 8), "Buzz! state written with its block");
	s_buzz[0] = 0;
	USBasync(0);
	in_transfer(buzz, report, sizeof(report), &len);
	USBStateRead(state, size);
	check(USBGetPortDevice(1) == buzz, "Buzz! state keeps the receiver");
	s_buzz[0] = 1;
	USBasync(0);
	check(in_transfer(buzz, report, sizeof(report), &len) == USB_RET_NAK, "Buzz! state restores the last report sent");
	s_buzz[0] = 0;

	/* SingStar with no frontend microphone: the adapter enumerates and
	 * streams silence at full length. */
	USBSetAccessory(USB_ACCESSORY_SINGSTAR);
	usb_mic::FrontendLoad(stub_environ_nomic);
	USBreset();
	USBDevice* mic = USBGetPortDevice(1);
	check(mic && USBGetPortDevice(0), "SingStar adapter on the free port, keyboard kept");
	usb_desc_set_config(mic, 1);
	check(mic->configuration == 1, "SingStar adapter enumerates");
	usb_desc_set_interface(mic, 1, 2);
	usb_mic::FrontendRun();
	u8 pcm[192];
	memset(pcm, 0x55, sizeof(pcm));
	const int mic_status = in_transfer(mic, pcm, sizeof(pcm), &len);
	bool silent = true;
	for (unsigned i = 0; i < sizeof(pcm); i++)
		silent = silent && pcm[i] == 0;
	check(mic_status == USB_RET_SUCCESS && len == (int)sizeof(pcm) && silent, "SingStar without a microphone streams silence");

	/* SingStar with the frontend's microphones: player 1 is the left
	 * channel, player 2 the right. */
	usb_mic::FrontendUnload();
	usb_mic::FrontendLoad(stub_environ);
	USBreset();
	mic = USBGetPortDevice(1);
	usb_desc_set_config(mic, 1);
	usb_desc_set_interface(mic, 1, 2);
	s_mic_opens = 0;
	usb_mic::FrontendRun();
	check(s_mic_opens == 2, "SingStar opens both microphones, player 1 first");
	in_transfer(mic, pcm, sizeof(pcm), &len);
	bool left_positive = true, right_negative = true;
	for (unsigned i = 0; i + 1 < sizeof(pcm); i += 4)
	{
		const int16_t left = (int16_t)(pcm[i] | (pcm[i + 1] << 8));
		const int16_t right = (int16_t)(pcm[i + 2] | (pcm[i + 3] << 8));
		left_positive = left_positive && left > 0;
		right_negative = right_negative && right < 0;
	}
	check(left_positive && right_negative, "SingStar left channel is player 1, right is player 2");

	/* The game picks 8 kHz: one read of the microphone's 480 samples at
	 * 48 kHz comes out as 80 frames, 8 per 1 ms packet. */
	usb_mic::FrontendUnload();
	usb_mic::FrontendLoad(stub_environ);
	USBreset();
	mic = USBGetPortDevice(1);
	usb_desc_set_config(mic, 1);
	u8 rate[3] = { 8000 & 0xff, 8000 >> 8, 0 };
	control(mic, ClassEndpointOutRequest | 0x01 /* SET_CUR */, 0x01 << 8 /* sampling frequency */, 0x81, rate, sizeof(rate));
	usb_desc_set_interface(mic, 1, 2);
	usb_mic::FrontendRun();
	unsigned frames = 0, packets = 0;
	while (packets < 32 && in_transfer(mic, pcm, sizeof(pcm), &len) == USB_RET_SUCCESS)
	{
		frames += len / 4;
		packets++;
	}
	check(frames == 80 && packets == 10, "SingStar follows the rate the game sets");
	usb_mic::FrontendRun();

	/* The alternate setting is kept across a state. */
	USBStateWrite(state);
	check(!memcmp(state + size - 8, "LRPS2USB", 8), "SingStar state written with its block");
	usb_desc_set_interface(mic, 1, 0);
	USBStateRead(state, size);
	check(USBGetPortDevice(1) == mic && mic->altsetting[1] == 2, "SingStar state keeps the stream setting");

	USBSetAccessory(USB_ACCESSORY_NONE);
	USBclose();
	usb_mic::FrontendUnload();
	usb_eyetoy::FrontendUnload();
	free(state);
	free(other);
	free(iopMem);
	printf(s_bad ? "FAIL\n" : "PASS\n");
	return s_bad ? 1 : 0;
}
