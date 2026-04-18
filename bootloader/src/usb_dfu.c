/*
 * USB DFU 1.1 device for the g102-custom-fw bootloader.
 *
 * Enumerates as ST DFU VID/PID (0483:df11) so stock `dfu-util` recognises it
 * without extra flags. The single DFU interface writes directly into the
 * firmware region starting at 0x08004000.
 *
 * Host side:
 *     dfu-util -d 0483:df11 -a 0 -s 0x08004000 -D firmware.bin
 */

#include <string.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dfu.h>
#include "board.h"
#include "flash.h"

#define DFU_TRANSFER_SIZE   2048   // one F072 flash page

/* Must be >= wTransferSize so DFU_DNLOAD data stages fit in the control buffer. */
static uint8_t usb_ctrl_buf[DFU_TRANSFER_SIZE];

static const struct usb_device_descriptor dev_desc = {
    .bLength            = USB_DT_DEVICE_SIZE,
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x0483,
    .idProduct          = 0xdf11,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

static const struct usb_dfu_descriptor dfu_function = {
    .bLength            = sizeof(struct usb_dfu_descriptor),
    .bDescriptorType    = DFU_FUNCTIONAL,
    .bmAttributes       = USB_DFU_CAN_DOWNLOAD | USB_DFU_WILL_DETACH,
    .wDetachTimeout     = 255,
    .wTransferSize      = DFU_TRANSFER_SIZE,
    .bcdDFUVersion      = 0x0110,   // plain DFU 1.1 (no ST extensions)
};

static const struct usb_interface_descriptor iface = {
    .bLength            = USB_DT_INTERFACE_SIZE,
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 0,
    .bInterfaceClass    = 0xFE,     // application-specific
    .bInterfaceSubClass = 1,        // DFU
    .bInterfaceProtocol = 2,        // DFU mode
    .iInterface         = 4,
    .extra              = &dfu_function,
    .extralen           = sizeof(dfu_function),
};

static const struct usb_interface ifaces[] = {{
    .num_altsetting = 1,
    .altsetting     = &iface,
}};

static const struct usb_config_descriptor cfg_desc = {
    .bLength              = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType      = USB_DT_CONFIGURATION,
    .wTotalLength         = 0,
    .bNumInterfaces       = 1,
    .bConfigurationValue  = 1,
    .iConfiguration       = 0,
    .bmAttributes         = 0xC0,   // self-powered
    .bMaxPower            = 50,
    .interface            = ifaces,
};

static const char *strings[] = {
    "g102-custom-fw",
    "G102/G203 LIGHTSYNC DFU Bootloader (CF)",
    "0001",
    "@Internal Flash   /0x08004000/56*002Kg",
};

/* ---- DFU state machine ---------------------------------------------------- */

static enum dfu_state dfu_state = STATE_DFU_IDLE;
static enum dfu_status dfu_status = DFU_STATUS_OK;

/* Pending download buffer: accumulated in the DATA OUT stage, then flashed in
   the following GET_STATUS so the host sees the correct status. */
static uint8_t  dnload_buf[DFU_TRANSFER_SIZE];
static uint16_t dnload_len;
static uint16_t dnload_block;

static void fail(enum dfu_status s)
{
    dfu_status = s;
    dfu_state  = STATE_DFU_ERROR;
}

/* ------- control request handling ----------------------------------------- */

static enum usbd_request_return_codes
dfu_control_request(usbd_device *usbd_dev,
                    struct usb_setup_data *req,
                    uint8_t **buf, uint16_t *len,
                    void (**complete)(usbd_device *, struct usb_setup_data *))
{
    (void)usbd_dev;
    (void)complete;

    if ((req->bmRequestType & 0x7F) != 0x21) return USBD_REQ_NOTSUPP;

    switch (req->bRequest) {
    case DFU_DNLOAD: {
        if (req->wLength == 0) {
            // Zero-length DNLOAD = end of transfer → manifest.
            dfu_state = STATE_DFU_MANIFEST_SYNC;
            return USBD_REQ_HANDLED;
        }
        if (req->wLength > DFU_TRANSFER_SIZE) { fail(DFU_STATUS_ERR_UNKNOWN); return USBD_REQ_NOTSUPP; }
        memcpy(dnload_buf, *buf, req->wLength);
        dnload_len   = req->wLength;
        dnload_block = req->wValue;
        dfu_state    = STATE_DFU_DNLOAD_SYNC;
        return USBD_REQ_HANDLED;
    }

    case DFU_GETSTATUS: {
        // Actually program flash here so the response reports the true result.
        if (dfu_state == STATE_DFU_DNLOAD_SYNC) {
            // Block 0 sets the base address (ST extension) — we lock it to firmware base.
            uint32_t addr = FLASH_FIRMWARE_BASE + (uint32_t)dnload_block * DFU_TRANSFER_SIZE;
            if (addr < FLASH_FIRMWARE_BASE) addr = FLASH_FIRMWARE_BASE;
            int rc = flash_program(addr, dnload_buf, dnload_len);
            if (rc != 0) { fail(DFU_STATUS_ERR_WRITE); }
            else         { dfu_state = STATE_DFU_DNLOAD_IDLE; }
        } else if (dfu_state == STATE_DFU_MANIFEST_SYNC) {
            // All blocks applied; no separate manifest phase needed.
            dfu_state = STATE_DFU_MANIFEST;
        } else if (dfu_state == STATE_DFU_MANIFEST) {
            dfu_state = STATE_DFU_IDLE;
            // WILL_DETACH is set, so host expects us to reset ourselves.
            scb_reset_system();
        }

        static uint8_t status[6];
        status[0] = dfu_status;
        status[1] = 0; status[2] = 0; status[3] = 0;  // bwPollTimeout = 0
        status[4] = dfu_state;
        status[5] = 0;
        *buf = status;
        *len = 6;
        return USBD_REQ_HANDLED;
    }

    case DFU_CLRSTATUS:
        dfu_state  = STATE_DFU_IDLE;
        dfu_status = DFU_STATUS_OK;
        return USBD_REQ_HANDLED;

    case DFU_ABORT:
        dfu_state = STATE_DFU_IDLE;
        return USBD_REQ_HANDLED;

    case DFU_GETSTATE: {
        static uint8_t st;
        st   = dfu_state;
        *buf = &st;
        *len = 1;
        return USBD_REQ_HANDLED;
    }

    case DFU_DETACH:
        scb_reset_system();
        return USBD_REQ_HANDLED;

    default:
        return USBD_REQ_NOTSUPP;
    }
}

static void set_config(usbd_device *usbd_dev, uint16_t wValue)
{
    (void)wValue;
    usbd_register_control_callback(
        usbd_dev,
        USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE  | USB_REQ_TYPE_RECIPIENT,
        dfu_control_request);
}

void usb_dfu_run(void)
{
    usbd_device *usbd_dev = usbd_init(
        &st_usbfs_v2_usb_driver,
        &dev_desc, &cfg_desc,
        strings, 4,
        usb_ctrl_buf, sizeof(usb_ctrl_buf));

    usbd_register_set_config_callback(usbd_dev, set_config);

    while (1) {
        usbd_poll(usbd_dev);
    }
}
