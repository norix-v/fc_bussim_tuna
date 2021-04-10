//
// FC Cartridge bus simulator "tuna"
//
// Copyrighte (C) 2021 Norix (NX labs)
//
// License: GPL2 (see gpl-2.0.txt)
//
// USB functions
//
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "bsp/board.h"
#include "usb_drv.h"
#include "bus_sim.h"
#include "flash_memory.h"
#include "kazzo_request.h"

//
// kazzo
//
static const char	kazzo_version_string[KAZZO_VERSION_STRING_SIZE] = "kazzopp"" 0.0.1 / " __DATE__;

typedef struct
{
	KAZZO_REQUEST	request;
	uint16_t		address;
	uint16_t		length;
	uint16_t		offset;
} KAZZO_WRITE_COMMAND;

static KAZZO_WRITE_COMMAND	request_both_write;
static KAZZO_WRITE_COMMAND	request_cpu_program;
static KAZZO_WRITE_COMMAND	request_ppu_program;

//
// Device descriptor
//
tusb_desc_device_t const desc_device = {
	.bLength			= sizeof(tusb_desc_device_t),
	.bDescriptorType	= TUSB_DESC_DEVICE,
	.bcdUSB				= 0x0110,	// USB version

	.bDeviceClass		= TUSB_CLASS_VENDOR_SPECIFIC,
	.bDeviceSubClass	= 0,
	.bDeviceProtocol	= 0,
	.bMaxPacketSize0	= CFG_TUD_ENDPOINT0_SIZE,

	.idVendor			= 0x16C0,	// V-USB kazzo
	.idProduct			= 0x05DC,	// V-USB kazzo
	.bcdDevice			= 0x0100,

	.iManufacturer		= 0x01,		// index 1
	.iProduct			= 0x02,		// index 2
	.iSerialNumber		= 0x00,		// no string

	.bNumConfigurations	= 0x01		// 1 configuration
};

// callback
uint8_t const * tud_descriptor_device_cb(void)
{
	return (uint8_t const *)&desc_device;
}

// Configuration Descriptor
#define	CONFIG_TOTAL_LEN	(TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)

uint8_t const desc_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0, 500),

  // Interface number, string index, EP Out & IN address, EP size
  TUD_VENDOR_DESCRIPTOR(0, 0, 0x01, 0x81, TUD_OPT_HIGH_SPEED ? 512 : 64)
};

// callback
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index; // for multiple configurations
  return desc_configuration;
}

// String descriptor
const uint16_t string_desc_language[] = { // Index: 0
	4 | (TUSB_DESC_STRING << 8),		// bLength & bDescriptorType
	0x0409								// English - US
};
// Manufacturer
const uint16_t string_desc_manufacturer[] = { // Index: 1
	18 | (TUSB_DESC_STRING << 8),		// bLength & bDescriptorType
	'o', 'b', 'd', 'e', 'v', '.', 'a', 't'
};
// Product
const uint16_t string_desc_product[] = { // Index: 2
	12 | (TUSB_DESC_STRING << 8),		// bLength & bDescriptorType
	'k', 'a', 'z', 'z', 'o'
};


// String descriptor callback
uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
	(void)langid;

	uint16_t const *ret = NULL;
	switch (index) {
		case 0:
			ret = string_desc_language;
			break;
		case 1:
			ret = string_desc_manufacturer;
			break;
		case 2:
			ret = string_desc_product;
			break;
		default:
			break;
	}
	return ret;
}

static uint8_t	sendbuffer[CFG_TUD_VENDOR_TX_BUFSIZE];
static uint8_t	recvbuffer[CFG_TUD_VENDOR_RX_BUFSIZE];

bool tud_vendor_control_request_cb(uint8_t rhport, tusb_control_request_t const * request)
{
// response with data stage
// bool tud_control_xfer(uint8_t rhport, tusb_control_request_t const * request, void* buffer, uint16_t len)
// response with status OK
// bool tud_control_status(uint8_t rhport, tusb_control_request_t const * request);

	KAZZO_WRITE_COMMAND	*write_command;

	switch (request->bRequest) {
		case	KAZZO_REQUEST_ECHO:
			sendbuffer[0] = (uint8_t)(request->wValue);
			sendbuffer[1] = (uint8_t)(request->wValue >> 8);
			sendbuffer[2] = (uint8_t)(request->wIndex);
			sendbuffer[3] = (uint8_t)(request->wIndex >> 8);
			return	tud_control_xfer (rhport, request, sendbuffer, 4);

		case	KAZZO_REQUEST_PHI2_INIT:
			// nothing to do
			return tud_control_status(rhport, request);

		case	KAZZO_REQUEST_CPU_READ_6502:
		case	KAZZO_REQUEST_CPU_READ:
			kazzo_cpu_read (request->wValue, request->wLength, sendbuffer);
			return	tud_control_xfer (rhport, request, sendbuffer, request->wLength);

		case	KAZZO_REQUEST_PPU_READ:
			kazzo_ppu_read (request->wValue, request->wLength, sendbuffer);
			return	tud_control_xfer (rhport, request, sendbuffer, request->wLength);

		case	KAZZO_REQUEST_CPU_WRITE_6502:
		case	KAZZO_REQUEST_CPU_WRITE_FLASH:
		case	KAZZO_REQUEST_PPU_WRITE:
			write_command = &request_both_write;
			goto	KAZZO_WRITE;

		case	KAZZO_REQUEST_FLASH_PROGRAM:
		case	KAZZO_REQUEST_FLASH_CONFIG_SET:
			if (request->wIndex == KAZZO_INDEX_CPU) {
				write_command = &request_cpu_program;
			} else {
				write_command = &request_ppu_program;
			}
			goto	KAZZO_WRITE;
		KAZZO_WRITE:
			write_command->request = (KAZZO_REQUEST)request->bRequest;
			write_command->length  = request->wLength;
			write_command->address = request->wValue;
			write_command->offset  = 0;
			return	tud_control_xfer (rhport, request, recvbuffer, request->wLength);

		case	KAZZO_REQUEST_FLASH_STATUS:
			switch ((KAZZO_INDEX)request->wIndex) {
				case	KAZZO_INDEX_CPU:
					sendbuffer[0] = kazzo_flash_cpu_status();
					return	tud_control_xfer (rhport, request, sendbuffer, 1);
				case	KAZZO_INDEX_PPU:
					sendbuffer[0] = kazzo_flash_ppu_status();
					return	tud_control_xfer (rhport, request, sendbuffer, 1);
				default:
					sendbuffer[0] = kazzo_flash_cpu_status();
					sendbuffer[1] = kazzo_flash_ppu_status();
					return	tud_control_xfer (rhport, request, sendbuffer, 2);
			}
			break;

		case	KAZZO_REQUEST_FLASH_DEVICE:
			if (request->wIndex == KAZZO_INDEX_CPU) {
				kazzo_flash_cpu_device_get(sendbuffer);
			} else {
				kazzo_flash_ppu_device_get(sendbuffer);
			}
			return	tud_control_xfer (rhport, request, sendbuffer, 2);

		case	KAZZO_REQUEST_FLASH_ERASE:
			if (request->wIndex == KAZZO_INDEX_CPU) {
				kazzo_flash_cpu_erase(request->wValue);
			} else {
				kazzo_flash_ppu_erase(request->wValue);
			}
			return	tud_control_xfer (rhport, request, recvbuffer, request->wLength);

		case	KAZZO_REQUEST_VRAM_CONNECTION:
			sendbuffer[0] = kazzo_vram_connection_get();
			return	tud_control_xfer (rhport, request, sendbuffer, 1);

		case	KAZZO_REQUEST_FIRMWARE_VERSION:
			return	tud_control_xfer (rhport, request, (void*)kazzo_version_string, KAZZO_VERSION_STRING_SIZE);

		// nothing to do following requests.
		case	KAZZO_REQUEST_DISK_STATUS_GET:
		case	KAZZO_REQUEST_DISK_READ:
		case	KAZZO_REQUEST_DISK_WRITE:
		case	KAZZO_REQUEST_FIRMWARE_PROGRAM:
		case	KAZZO_REQUEST_FIRMWARE_DOWNLOAD:
			// no response
			return	false;

		default:
			// stall unknown request
#if	0
			printf("\ntud_vendor_control_request_cb\n");
			printf("bmRequestType: %02X\n", request->bmRequestType);
			printf("bRequest     : %02X\n", request->bRequest);
			printf("wValue       : %04X\n", request->wValue);
			printf("wIndex       : %04X\n", request->wIndex);
			printf("wLength      : %04X\n", request->wLength);
#endif
			return	false;
	}

	return	true;
}

// Invoked when DATA Stage of VENDOR's request is complete
bool tud_vendor_control_complete_cb(uint8_t rhport, tusb_control_request_t const * request)
{
	(void) rhport;

	static uint8_t	cpu_buffer[KAZZO_FLASH_PACKET_SIZE];
	static uint8_t	ppu_buffer[KAZZO_FLASH_PACKET_SIZE];

	// request data receive
	if (request->bmRequestType_bit.direction == TUSB_DIR_OUT) {
		// decode masked data
		for (int i = 0; i < request->wLength; i++) {
			recvbuffer[i] ^= 0xA5;
		}

		switch (request_both_write.request) {
			case	KAZZO_REQUEST_CPU_WRITE_6502:
				kazzo_cpu_write_6502(request_both_write.address, request->wLength, recvbuffer);
				request_both_write.request = KAZZO_REQUEST_NOP;
				break;
			case	KAZZO_REQUEST_CPU_WRITE_FLASH:
				kazzo_cpu_write_flash(request_both_write.address, request->wLength, recvbuffer);
				request_both_write.request = KAZZO_REQUEST_NOP;
				break;
			case	KAZZO_REQUEST_PPU_WRITE:
				kazzo_ppu_write(request_both_write.address, request->wLength, recvbuffer);
				request_both_write.request = KAZZO_REQUEST_NOP;
				break;
			default:
				break;
		}

		switch (request_cpu_program.request) {
			case	KAZZO_REQUEST_FLASH_PROGRAM:
				memcpy(cpu_buffer, recvbuffer, request->wLength);
				kazzo_flash_cpu_program(request_cpu_program.address, request_cpu_program.length, cpu_buffer);
				request_cpu_program.request = KAZZO_REQUEST_NOP;
				break;
			case	KAZZO_REQUEST_FLASH_CONFIG_SET:
				kazzo_flash_cpu_config(recvbuffer, request_cpu_program.length);
				request_cpu_program.request = KAZZO_REQUEST_NOP;
				break;
			default:
				break;
		}

		switch (request_ppu_program.request) {
			case	KAZZO_REQUEST_FLASH_PROGRAM:
				memcpy(ppu_buffer, recvbuffer, request->wLength);
				kazzo_flash_ppu_program(request_ppu_program.address, request_ppu_program.length, ppu_buffer);
				request_ppu_program.request = KAZZO_REQUEST_NOP;
				break;
			case	KAZZO_REQUEST_FLASH_CONFIG_SET:
				kazzo_flash_ppu_config(recvbuffer, request_ppu_program.length);
				request_ppu_program.request = KAZZO_REQUEST_NOP;
				break;
			default:
				break;
		}
	}

#if	0
	printf("\ntud_vendor_control_complete_cb\n");
	printf("bmRequestType: %02X\n", request->bmRequestType);
	printf("bRequest     : %02X\n", request->bRequest);
	printf("wValue       : %04X\n", request->wValue);
	printf("wIndex       : %04X\n", request->wIndex);
	printf("wLength      : %04X\n", request->wLength);
#endif

	return true;
}

