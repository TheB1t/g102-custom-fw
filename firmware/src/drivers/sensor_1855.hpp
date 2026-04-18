#pragma once

/*
 * "1855" optical sensor driver. Chip silk reads `1855 Rev 007`; Logitech
 * in-house, likely a revision of the Mercury family. Protocol is
 * PixArt-family in shape (SROM 2E/2F upload, 0x02 commit, CS-framed) but
 * the part is not a documented PixArt PMW/PAW. See docs/SENSOR_1855.md.
 *
 * Templated on a Board trait and an SPI bus — the driver owns neither,
 * it just needs to know which frame() to call and which CS pin it runs on
 * (the CS type is baked into the bus template parameter).
 *
 * Usage:
 *   Sensor1855<board::G102, platform::SoftCsSpiBus<G102::SensorCs>> sensor{spi};
 *   sensor.init();
 *   int16_t dx, dy;
 *   if (sensor.read_motion(dx, dy)) { ... }
 *   sensor.set_dpi(0x08);
 */

#include <cstdint>

namespace drivers {

template <typename Board, typename Spi>
class Sensor1855 {
public:
    explicit Sensor1855(Spi &spi) : spi_(spi) {}

    /* Bring up pins + clock the SPI bus. Non-blocking: the actual init
       sequence (Vcc settle, two replay passes, 166 ms gap, DPI clamp) is
       driven by init_tick(now_ms) from the main loop, so usbd_poll() can
       keep running during the ~280 ms bringup. */
    void init_start();

    /* Advance the bringup state machine. Safe to call every loop iteration
       even after the sensor is ready — returns immediately once done. */
    void init_tick(uint32_t now_ms);

    bool ready() const { return state_ == InitState::Ready; }

    /* Read motion burst. Returns true if new motion was latched since the
       last call (and writes dx/dy as signed 12-bit counts, sign-extended
       to int16); false otherwise (dx/dy set to 0).

       The G102's sensor chip is rotated 90° in the chassis, but this driver
       returns raw sensor-frame dx/dy. The axis remap to HID coordinates
       lives in main, so the driver stays board-agnostic. */
    bool read_motion(int16_t &dx, int16_t &dy);

    /* Mirror register 0x20 / 0x24 (X / Y resolution). Non-linear scale,
       see project memory / SENSOR_1855.md for the empirical mapping.
       Must replay FRAME8 tail + FRAME9 commit or Y axis stays runaway-fast. */
    void set_dpi(uint8_t code);

private:
    enum class InitState : uint8_t {
        Idle,        // init_start() not yet called
        WaitVcc,     // 100 ms Vcc settle
        Pass1,       // replay frames 0..9
        WaitGap,     // 166 ms between passes
        Pass2,       // replay frames 0..9 again
        SetDpi,      // clamp DPI to a sane startup value
        Ready,
    };

    static constexpr uint32_t kMotionLen = 5;
    static constexpr uint32_t kVccSettleMs = 100;
    static constexpr uint32_t kPassGapMs   = 166;

    Spi       &spi_;
    InitState  state_      = InitState::Idle;
    uint32_t   state_started_ms_ = 0;
};

} // namespace drivers
