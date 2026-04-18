#include <cstring>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>

#include "services/usb_hid.hpp"

namespace services {

namespace {

constexpr uint8_t  HID_EP_IN       = 0x81;
constexpr uint16_t HID_REPORT_SIZE = 6;  // buttons + int16 dx + int16 dy + int8 wheel

const struct usb_device_descriptor dev_desc = {
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

/* Mouse report descriptor — 5 buttons, X/Y (int16), wheel (int8). */
const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x02,                    // Usage (Mouse)
    0xA1, 0x01,                    // Collection (Application)
    0x09, 0x01,                    //   Usage (Pointer)
    0xA1, 0x00,                    //   Collection (Physical)
    0x05, 0x09,                    //     Usage Page (Button)
    0x19, 0x01, 0x29, 0x05,        //     Usage Min 1, Usage Max 5
    0x15, 0x00, 0x25, 0x01,        //     Logical Min 0, Max 1
    0x95, 0x05, 0x75, 0x01,        //     Report Count 5, Size 1
    0x81, 0x02,                    //     Input (Data,Var,Abs)
    0x95, 0x01, 0x75, 0x03,        //     Report Count 1, Size 3
    0x81, 0x01,                    //     Input (Const) — 3-bit padding
    0x05, 0x01,                    //     Usage Page (Generic Desktop)
    0x09, 0x30, 0x09, 0x31,        //     Usage X, Usage Y
    0x16, 0x01, 0x80,              //     Logical Min -32767
    0x26, 0xFF, 0x7F,              //     Logical Max +32767
    0x75, 0x10, 0x95, 0x02,        //     Report Size 16, Count 2
    0x81, 0x06,                    //     Input (Data,Var,Rel)
    0x09, 0x38,                    //     Usage (Wheel)
    0x15, 0x81, 0x25, 0x7F,        //     Logical Min -127, Max +127
    0x75, 0x08, 0x95, 0x01,        //     Report Size 8, Count 1
    0x81, 0x06,                    //     Input (Data,Var,Rel)
    0xC0,                          //   End Collection (Physical)
    0xC0,                          // End Collection (Application)
};

const struct {
    struct usb_hid_descriptor hid;
    struct {
        uint8_t  bReportDescriptorType;
        uint16_t wDescriptorLength;
    } __attribute__((packed)) report_ref;
} __attribute__((packed)) hid_function = {
    .hid = {
        .bLength         = sizeof(hid_function),
        .bDescriptorType = USB_DT_HID,
        .bcdHID          = 0x0110,
        .bCountryCode    = 0,
        .bNumDescriptors = 1,
    },
    .report_ref = {
        .bReportDescriptorType = USB_DT_REPORT,
        .wDescriptorLength     = sizeof(hid_report_descriptor),
    },
};

const struct usb_endpoint_descriptor hid_ep = {
    .bLength          = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType  = USB_DT_ENDPOINT,
    .bEndpointAddress = HID_EP_IN,
    .bmAttributes     = USB_ENDPOINT_ATTR_INTERRUPT,
    .wMaxPacketSize   = HID_REPORT_SIZE,
    .bInterval        = 1,
};

const struct usb_interface_descriptor iface = {
    USB_DT_INTERFACE_SIZE,  // bLength
    USB_DT_INTERFACE,       // bDescriptorType
    0,                      // bInterfaceNumber
    0,                      // bAlternateSetting
    1,                      // bNumEndpoints
    USB_CLASS_HID,          // bInterfaceClass
    0,                      // bInterfaceSubClass (no boot — we use int16 X/Y)
    0,                      // bInterfaceProtocol (none)
    0,                      // iInterface
    &hid_ep,                // endpoint
    &hid_function,          // extra
    sizeof(hid_function),   // extralen
};

const struct usb_interface ifaces[] = {{
    nullptr,  // cur_altsetting
    1,        // num_altsetting
    nullptr,  // iface_assoc
    &iface,   // altsetting
}};

const struct usb_config_descriptor cfg_desc = {
    .bLength             = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType     = USB_DT_CONFIGURATION,
    .wTotalLength        = 0,
    .bNumInterfaces      = 1,
    .bConfigurationValue = 1,
    .iConfiguration      = 0,
    .bmAttributes        = 0xA0,   // bus-powered, remote-wakeup capable
    .bMaxPower           = 50,     // 100 mA
    .interface           = ifaces,
};

const char *strings[] = {
    "Logitech",
    "G102/G203 LIGHTSYNC Gaming Mouse (CF)",
    "G102CFW0001",
};

} // namespace

UsbHidMouse *UsbHidMouse::instance_ = nullptr;

enum usbd_request_return_codes
UsbHidMouse::control_request_thunk(usbd_device *,
                                   struct usb_setup_data *req,
                                   uint8_t **buf, uint16_t *len,
                                   void (**)(usbd_device *, struct usb_setup_data *))
{
    if (req->bmRequestType != 0x81 ||
        req->bRequest      != USB_REQ_GET_DESCRIPTOR ||
        req->wValue        != 0x2200) {
        return USBD_REQ_NOTSUPP;
    }
    *buf = const_cast<uint8_t *>(hid_report_descriptor);
    *len = sizeof(hid_report_descriptor);
    return USBD_REQ_HANDLED;
}

void UsbHidMouse::set_config_thunk(usbd_device *dev, uint16_t)
{
    usbd_ep_setup(dev, HID_EP_IN, USB_ENDPOINT_ATTR_INTERRUPT,
                  HID_REPORT_SIZE, nullptr);
    usbd_register_control_callback(
        dev,
        USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE     | USB_REQ_TYPE_RECIPIENT,
        &UsbHidMouse::control_request_thunk);
    if (instance_) instance_->configured_ = true;
}

void UsbHidMouse::init()
{
    instance_ = this;
    usbd_ = usbd_init(
        &st_usbfs_v2_usb_driver,
        &dev_desc, &cfg_desc,
        strings, 3,
        ctrl_buf_, sizeof(ctrl_buf_));

    usbd_register_set_config_callback(usbd_, &UsbHidMouse::set_config_thunk);
}

void UsbHidMouse::poll()
{
    usbd_poll(usbd_);
}

void UsbHidMouse::send_report(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel)
{
    if (!configured_) return;

    uint16_t udx = static_cast<uint16_t>(dx);
    uint16_t udy = static_cast<uint16_t>(dy);

    uint8_t report[HID_REPORT_SIZE];
    report[0] = buttons & 0x1F;
    report[1] = static_cast<uint8_t>(udx & 0xFF);
    report[2] = static_cast<uint8_t>(udx >> 8);
    report[3] = static_cast<uint8_t>(udy & 0xFF);
    report[4] = static_cast<uint8_t>(udy >> 8);
    report[5] = static_cast<uint8_t>(wheel);
    usbd_ep_write_packet(usbd_, HID_EP_IN, report, sizeof(report));
}

} // namespace services
