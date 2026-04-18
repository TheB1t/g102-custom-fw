#pragma once

/*
 * USB HID mouse service. Enumerates as stock Logitech G102 (046d:c092)
 * so the OS applies its usual defaults. Report is 6 bytes:
 *   [buttons:1 (5 bits used)] [dx:int16_le] [dy:int16_le] [wheel:int8]
 * Non-boot interface — OS reads the report descriptor and handles the
 * 16-bit X/Y natively, so fast swipes don't clip at ±127.
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
    void send_report(uint8_t buttons, int16_t dx, int16_t dy, int8_t wheel);

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
