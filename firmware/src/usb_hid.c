/*
 * USB HID boot-mouse device for the g102-rebellion firmware.
 *
 * Enumerates as stock Logitech G102 (046d:c09d) so the OS applies the
 * same defaults it would to the original mouse. Report format is the
 * classic 4-byte boot protocol: buttons bitmap, dx, dy, wheel.
 */

#include <string.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include "board.h"
#include "hid.h"

#define HID_EP_IN       0x81
#define HID_REPORT_SIZE 4

static usbd_device *g_usbd;
static uint8_t usb_ctrl_buf[128];
static volatile uint8_t g_configured;

static const struct usb_device_descriptor dev_desc = {
    .bLength            = USB_DT_DEVICE_SIZE,
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x046d,
    .idProduct          = 0xc092,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

/* Boot-protocol mouse report descriptor — 3 buttons, X/Y (int8), wheel (int8). */
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,             // Usage Page (Generic Desktop)
    0x09, 0x02,             // Usage (Mouse)
    0xA1, 0x01,             // Collection (Application)
    0x09, 0x01,             //   Usage (Pointer)
    0xA1, 0x00,             //   Collection (Physical)
    0x05, 0x09,             //     Usage Page (Buttons)
    0x19, 0x01,             //     Usage Minimum (1)
    0x29, 0x03,             //     Usage Maximum (3)
    0x15, 0x00,             //     Logical Minimum (0)
    0x25, 0x01,             //     Logical Maximum (1)
    0x95, 0x03,             //     Report Count (3)
    0x75, 0x01,             //     Report Size (1)
    0x81, 0x02,             //     Input (Data, Var, Abs)
    0x95, 0x01,             //     Report Count (1) - 5 bit padding
    0x75, 0x05,             //     Report Size (5)
    0x81, 0x01,             //     Input (Const)
    0x05, 0x01,             //     Usage Page (Generic Desktop)
    0x09, 0x30,             //     Usage (X)
    0x09, 0x31,             //     Usage (Y)
    0x15, 0x81,             //     Logical Minimum (-127)
    0x25, 0x7F,             //     Logical Maximum (127)
    0x75, 0x08,             //     Report Size (8)
    0x95, 0x02,             //     Report Count (2)
    0x81, 0x06,             //     Input (Data, Var, Rel)
    0x09, 0x38,             //     Usage (Wheel)
    0x95, 0x01,             //     Report Count (1)
    0x81, 0x06,             //     Input (Data, Var, Rel)
    0xC0,                   //   End Collection
    0xC0,                   // End Collection
};

static const struct {
    struct usb_hid_descriptor hid;
    struct {
        uint8_t bReportDescriptorType;
        uint16_t wDescriptorLength;
    } __attribute__((packed)) report_ref;
} __attribute__((packed)) hid_function = {
    .hid = {
        .bLength            = sizeof(hid_function),
        .bDescriptorType    = USB_DT_HID,
        .bcdHID             = 0x0110,
        .bCountryCode       = 0,
        .bNumDescriptors    = 1,
    },
    .report_ref = {
        .bReportDescriptorType = USB_DT_REPORT,
        .wDescriptorLength     = sizeof(hid_report_descriptor),
    },
};

static const struct usb_endpoint_descriptor hid_ep = {
    .bLength          = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = HID_EP_IN,
    .bmAttributes     = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize   = HID_REPORT_SIZE,
    .bInterval        = 1,          // 1 ms — full-speed mouse
};

static const struct usb_interface_descriptor iface = {
    .bLength            = USB_DT_INTERFACE_SIZE,
    .bDescriptorType    = USB_DT_INTERFACE,
    .bInterfaceNumber   = 0,
    .bAlternateSetting  = 0,
    .bNumEndpoints      = 1,
    .bInterfaceClass    = USB_CLASS_HID,
    .bInterfaceSubClass = 1,        // Boot interface
    .bInterfaceProtocol = 2,        // Mouse
    .iInterface         = 0,
    .extra              = &hid_function,
    .extralen           = sizeof(hid_function),
    .endpoint           = &hid_ep,
};

static const struct usb_interface ifaces[] = {{
    .num_altsetting = 1,
    .altsetting     = &iface,
}};

static const struct usb_config_descriptor cfg_desc = {
    .bLength             = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType     = USB_DT_CONFIGURATION,
    .wTotalLength        = 0,
    .bNumInterfaces      = 1,
    .bConfigurationValue = 1,
    .iConfiguration      = 0,
    .bmAttributes        = 0xA0,    // bus-powered, remote wakeup
    .bMaxPower           = 50,      // 100 mA
    .interface           = ifaces,
};

static const char *strings[] = {
    "Logitech",
    "G102/G203 LIGHTSYNC Gaming Mouse",
    "G102REB0001",
};

static enum usbd_request_return_codes
hid_control_request(usbd_device *usbd_dev,
                    struct usb_setup_data *req,
                    uint8_t **buf, uint16_t *len,
                    void (**complete)(usbd_device *, struct usb_setup_data *))
{
    (void)usbd_dev;
    (void)complete;

    // Handle GET_DESCRIPTOR(Report) targeted at the HID interface.
    if ((req->bmRequestType != 0x81) ||
        (req->bRequest      != USB_REQ_GET_DESCRIPTOR) ||
        (req->wValue        != 0x2200)) {
        return USBD_REQ_NOTSUPP;
    }
    *buf = (uint8_t *)hid_report_descriptor;
    *len = sizeof(hid_report_descriptor);
    return USBD_REQ_HANDLED;
}

static void set_config(usbd_device *usbd_dev, uint16_t wValue)
{
    (void)wValue;
    usbd_ep_setup(usbd_dev, HID_EP_IN, USB_ENDPOINT_ATTR_INTERRUPT,
                  HID_REPORT_SIZE, NULL);
    usbd_register_control_callback(
        usbd_dev,
        USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE     | USB_REQ_TYPE_RECIPIENT,
        hid_control_request);
    g_configured = 1;
}

void usb_hid_init(void)
{
    g_usbd = usbd_init(
        &st_usbfs_v2_usb_driver,
        &dev_desc, &cfg_desc,
        strings, 3,
        usb_ctrl_buf, sizeof(usb_ctrl_buf));

    usbd_register_set_config_callback(g_usbd, set_config);
}

void usb_hid_poll(void)
{
    usbd_poll(g_usbd);
}

void usb_hid_send_report(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)
{
    if (!g_configured) return;

    uint8_t report[HID_REPORT_SIZE];
    report[0] = buttons & 0x07;
    report[1] = (uint8_t)dx;
    report[2] = (uint8_t)dy;
    report[3] = (uint8_t)wheel;
    usbd_ep_write_packet(g_usbd, HID_EP_IN, report, sizeof(report));
}
