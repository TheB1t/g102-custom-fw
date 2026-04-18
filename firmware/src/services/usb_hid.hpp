#pragma once

/*
 * USB HID boot-mouse service. Enumerates as stock Logitech G102
 * (046d:c092) so the OS applies its usual defaults. 4-byte boot-protocol
 * report: buttons bitmap, dx, dy, wheel.
 *
 * Singleton in practice — libopencm3's usbd callbacks aren't instance-aware,
 * so the class routes them through a file-static pointer. Construct once.
 */

#include <cstdint>
#include <libopencm3/usb/usbd.h>

namespace services {

class UsbHidMouse {
public:
    void init();
    void poll();

    /* No-op until the host has SET_CONFIGURATION'd us. */
    void send_report(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);

    bool configured() const { return configured_; }

private:
    static UsbHidMouse *instance_;

    static void set_config_thunk(usbd_device *dev, uint16_t wValue);
    static enum usbd_request_return_codes control_request_thunk(
        usbd_device *dev, struct usb_setup_data *req,
        uint8_t **buf, uint16_t *len,
        void (**complete)(usbd_device *, struct usb_setup_data *));

    usbd_device *usbd_ = nullptr;
    uint8_t      ctrl_buf_[128]{};
    volatile bool configured_ = false;
};

} // namespace services
