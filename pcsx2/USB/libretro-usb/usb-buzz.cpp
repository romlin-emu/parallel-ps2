// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

/* Buzz! receiver: four handsets of five buttons in one interrupt report. The
 * descriptors and report layout follow PCSX2's usb-pad/usb-buzz.cpp. A report
 * goes out when a button changes and is NAKed until then. */

#include "USB/libretro-usb/USBinternal.h"
#include "USB/libretro-usb/desc.h"
#include "USB/libretro-usb/usb-buzz.h"

#include "USB/USB.h"

#include "libretro.h"

#include <cstring>

namespace usb_buzz
{
	static const USBDescStrings desc_strings = {
		"",
		"Logitech Buzz(tm) Controller V1",
		"",
		"Logitech"};

	static uint8_t dev_descriptor[] = {
		0x12,       // bLength
		0x01,       // bDescriptorType (Device)
		0x00, 0x02, // bcdUSB 2.00
		0x00,       // bDeviceClass (Use class information in the Interface Descriptors)
		0x00,       // bDeviceSubClass
		0x00,       // bDeviceProtocol
		0x08,       // bMaxPacketSize0 8
		0x4C, 0x05, // idVendor 0x054C
		0x02, 0x00, // idProduct 0x0002
		0xA1, 0x05, // bcdDevice 11.01
		0x03,       // iManufacturer (String Index)
		0x01,       // iProduct (String Index)
		0x00,       // iSerialNumber (String Index)
		0x01,       // bNumConfigurations 1
	};

	static const uint8_t config_descriptor[] = {
		0x09,       // bLength
		0x02,       // bDescriptorType (Configuration)
		0x22, 0x00, // wTotalLength 34
		0x01,       // bNumInterfaces 1
		0x01,       // bConfigurationValue
		0x00,       // iConfiguration (String Index)
		0x80,       // bmAttributes
		0x32,       // bMaxPower 100mA

		0x09,       // bLength
		0x04,       // bDescriptorType (Interface)
		0x00,       // bInterfaceNumber 0
		0x00,       // bAlternateSetting
		0x01,       // bNumEndpoints 1
		0x03,       // bInterfaceClass
		0x00,       // bInterfaceSubClass
		0x00,       // bInterfaceProtocol
		0x00,       // iInterface (String Index)

		0x09,       // bLength
		0x21,       // bDescriptorType (HID)
		0x11, 0x01, // bcdHID 1.11
		0x33,       // bCountryCode
		0x01,       // bNumDescriptors
		0x22,       // bDescriptorType[0] (HID)
		0x4E, 0x00, // wDescriptorLength[0] 78

		0x07,       // bLength
		0x05,       // bDescriptorType (Endpoint)
		0x81,       // bEndpointAddress (IN/D2H)
		0x03,       // bmAttributes (Interrupt)
		0x08, 0x00, // wMaxPacketSize 8
		0x0A,       // bInterval 10 (unit depends on device speed)
	};

	static const uint8_t hid_report_descriptor[] = {
		0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
		0x09, 0x04,       // Usage (Joystick)
		0xA1, 0x01,       // Collection (Application)
		0xA1, 0x02,       //   Collection (Logical)
		0x75, 0x08,       //     Report Size (8)
		0x95, 0x02,       //     Report Count (2)
		0x15, 0x00,       //     Logical Minimum (0)
		0x26, 0xFF, 0x00, //     Logical Maximum (255)
		0x35, 0x00,       //     Physical Minimum (0)
		0x46, 0xFF, 0x00, //     Physical Maximum (255)
		0x09, 0x30,       //     Usage (X)
		0x09, 0x31,       //     Usage (Y)
		0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
		0x75, 0x01,       //     Report Size (1)
		0x95, 0x14,       //     Report Count (20)
		0x25, 0x01,       //     Logical Maximum (1)
		0x45, 0x01,       //     Physical Maximum (1)
		0x05, 0x09,       //     Usage Page (Button)
		0x19, 0x01,       //     Usage Minimum (0x01)
		0x29, 0x14,       //     Usage Maximum (0x14)
		0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
		0x06, 0x00, 0xFF, //     Usage Page (Vendor Defined 0xFF00)
		0x75, 0x01,       //     Report Size (1)
		0x95, 0x04,       //     Report Count (4)
		0x25, 0x01,       //     Logical Maximum (1)
		0x45, 0x01,       //     Physical Maximum (1)
		0x09, 0x01,       //     Usage (0x01)
		0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
		0xC0,             //   End Collection
		0xA1, 0x02,       //   Collection (Logical)
		0x75, 0x08,       //     Report Size (8)
		0x95, 0x07,       //     Report Count (7)
		0x26, 0xFF, 0x00, //     Logical Maximum (255)
		0x46, 0xFF, 0x00, //     Physical Maximum (255)
		0x09, 0x02,       //     Usage (0x02)
		0x91, 0x02,       //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
		0xC0,             //   End Collection
		0xC0,             // End Collection
	};

	// Two axis bytes, then the 20 button bits from bit 0 (handset 0's red),
	// then four tail bits.
	static constexpr u8 REPORT_AXIS = 0x7F;
	static constexpr u8 REPORT_TAIL = 0xF0;

	struct BuzzState
	{
		USBDevice dev{};
		USBDesc desc{};
		USBDescDevice desc_dev{};

		u32 buttons = 0; // bit 5 * handset + button, as the report orders them
		u32 sent = 0;    // the buttons in the last report sent
	};

	static void usb_buzz_handle_control(USBDevice* dev, USBPacket* p,
		int request, int value, int index, int length, uint8_t* data)
	{
		int ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
		if (ret >= 0)
			return;

		switch (request)
		{
			case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
				if ((value >> 8) != USB_DT_REPORT)
					goto fail;
				memcpy(data, hid_report_descriptor, sizeof(hid_report_descriptor));
				p->actual_length = sizeof(hid_report_descriptor);
				break;
			case SET_IDLE:
				break;
			default:
			fail:
				p->status = USB_RET_STALL;
				break;
		}
	}

	static void usb_buzz_handle_data(USBDevice* dev, USBPacket* p)
	{
		BuzzState* s = USB_CONTAINER_OF(dev, BuzzState, dev);

		if (p->ep->nr != 1)
		{
			p->status = USB_RET_STALL;
			return;
		}

		switch (p->pid)
		{
			case USB_TOKEN_IN:
			{
				if (s->buttons == s->sent)
				{
					p->status = USB_RET_NAK;
					return;
				}
				uint8_t report[5] = {
					REPORT_AXIS,
					REPORT_AXIS,
					static_cast<uint8_t>(s->buttons),
					static_cast<uint8_t>(s->buttons >> 8),
					static_cast<uint8_t>(REPORT_TAIL | (s->buttons >> 16)),
				};
				usb_packet_copy(p, report, sizeof(report));
				s->sent = s->buttons;
				break;
			}
			case USB_TOKEN_OUT:
				break;
			default:
				p->status = USB_RET_STALL;
				break;
		}
	}

	static void usb_buzz_unrealize(USBDevice* dev)
	{
		BuzzState* s = USB_CONTAINER_OF(dev, BuzzState, dev);
		usb_desc_device_free(&s->desc_dev);
		delete s;
	}

	static USBDevice* buzz_create(u32 port)
	{
		BuzzState* s = new BuzzState();
		s->desc.full = &s->desc_dev;
		s->desc.str = desc_strings;

		if (usb_desc_parse_dev(dev_descriptor, sizeof(dev_descriptor), &s->desc, &s->desc_dev) < 0)
			goto fail;
		if (usb_desc_parse_config(config_descriptor, sizeof(config_descriptor), &s->desc_dev) < 0)
			goto fail;

		s->dev.speed = USB_SPEED_FULL;
		s->dev.klass.handle_attach = usb_desc_attach;
		s->dev.klass.handle_reset = nullptr;
		s->dev.klass.handle_control = usb_buzz_handle_control;
		s->dev.klass.handle_data = usb_buzz_handle_data;
		s->dev.klass.unrealize = usb_buzz_unrealize;
		s->dev.klass.usb_desc = &s->desc;
		s->dev.klass.product_desc = nullptr;

		usb_desc_init(&s->dev);
		usb_ep_init(&s->dev);
		log_cb(RETRO_LOG_INFO, "USB: Buzz! receiver on port %u\n", port);
		return &s->dev;

	fail:
		usb_buzz_unrealize(&s->dev);
		return nullptr;
	}

	static void buzz_update(USBDevice* dev)
	{
		BuzzState* s = USB_CONTAINER_OF(dev, BuzzState, dev);
		s->buttons = 0;
		for (u32 handset = 0; handset < USB_BUZZ_HANDSETS; handset++)
			s->buttons |= PADGetBuzzButtons(handset) << (USB_BUZZ_BUTTONS * handset);
	}

	static void buzz_freeze(USBDevice* dev, UsbState* st)
	{
		BuzzState* s = USB_CONTAINER_OF(dev, BuzzState, dev);
		USB_STATE_DO(st, s->sent);
	}
} // namespace usb_buzz

USBDevice* usb_buzz_create(u32 port)
{
	return usb_buzz::buzz_create(port);
}

void usb_buzz_update(USBDevice* dev)
{
	usb_buzz::buzz_update(dev);
}

void usb_buzz_freeze(USBDevice* dev, UsbState* st)
{
	usb_buzz::buzz_freeze(dev, st);
}
