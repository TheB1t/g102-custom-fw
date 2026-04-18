#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>

#include "board.hpp"
#include "platform/gpio.hpp"
#include "platform/spi.hpp"
#include "platform/systick.hpp"
#include "drivers/sensor_1855.hpp"
#include "sensor_frames.h"

namespace drivers {

namespace {

/* Init-sequence replay tables. Two passes of 10 CS-framed transactions with
   a 166 ms gap between them — lifted directly from a scope capture of the
   stock firmware. Sizes are compile-time from sizeof(). */
constexpr const uint8_t * const kFrames[10] = {
    FRAME0, FRAME1, FRAME2, FRAME3, FRAME4,
    FRAME5, FRAME6, FRAME7, FRAME8, FRAME9,
};
constexpr uint32_t kFrameSizes[10] = {
    sizeof(FRAME0), sizeof(FRAME1), sizeof(FRAME2), sizeof(FRAME3), sizeof(FRAME4),
    sizeof(FRAME5), sizeof(FRAME6), sizeof(FRAME7), sizeof(FRAME8), sizeof(FRAME9),
};

} // namespace

template <typename Board, typename Spi>
void Sensor1855<Board, Spi>::init_start()
{
    using namespace platform;

    enable_port_clock(Board::SensorCs::port);
    enable_port_clock(Board::SensorSck::port);
    rcc_periph_clock_enable(RCC_SPI1);

    /* CS idle-high before anything else, so the bus init can't glitch it low. */
    Board::SensorCs::setup_output_pp();
    Board::SensorCs::set();

    Board::SensorSck::setup_af(Board::SensorSpiAf);
    Board::SensorMiso::setup_af(Board::SensorSpiAf);
    Board::SensorMosi::setup_af(Board::SensorSpiAf);

    spi_.init_mode3_1p5mhz();

    state_            = InitState::WaitVcc;
    state_started_ms_ = now_ms();
}

template <typename Board, typename Spi>
void Sensor1855<Board, Spi>::init_tick(uint32_t now)
{
    using namespace platform;

    switch (state_) {
    case InitState::Idle:
    case InitState::Ready:
        return;

    case InitState::WaitVcc:
        /* Sensor VCC must settle ≥ ~100 ms before the first SPI byte or
           the init sequence silently corrupts. */
        if ((now - state_started_ms_) < kVccSettleMs) return;
        state_ = InitState::Pass1;
        return;

    case InitState::Pass1:
        for (int i = 0; i < 10; i++) spi_.frame(kFrames[i], nullptr, kFrameSizes[i]);
        state_            = InitState::WaitGap;
        state_started_ms_ = now;
        return;

    case InitState::WaitGap:
        if ((now - state_started_ms_) < kPassGapMs) return;
        state_ = InitState::Pass2;
        return;

    case InitState::Pass2:
        for (int i = 0; i < 10; i++) spi_.frame(kFrames[i], nullptr, kFrameSizes[i]);
        state_ = InitState::SetDpi;
        return;

    case InitState::SetDpi:
        /* Init blob leaves DPI fairly high; clamp to a moderate startup value. */
        set_dpi(0x08);
        state_ = InitState::Ready;
        return;
    }
}

template <typename Board, typename Spi>
bool Sensor1855<Board, Spi>::read_motion(int8_t &dx, int8_t &dy)
{
    /* Motion burst. MISO returns [hdr, status, dxL, dyL, dxyH] — bit 7 of
       status is motion-latched; dx/dy are low 8 bits of a signed 12-bit
       count (high nibbles in dxyH). We clip to int8 for the boot-protocol
       HID report. */
    static constexpr uint8_t req[kMotionLen] = { 0x80, 0x85, 0x86, 0x87, 0x80 };
    uint8_t resp[kMotionLen];

    spi_.frame(req, resp, kMotionLen);

    if ((resp[1] & 0x80) == 0) {
        dx = 0;
        dy = 0;
        return false;
    }
    dx = static_cast<int8_t>(resp[2]);
    dy = static_cast<int8_t>(resp[3]);
    return true;
}

template <typename Board, typename Spi>
void Sensor1855<Board, Spi>::set_dpi(uint8_t code)
{
    /* FRAME8 tail replay: 0x20..0x24 in one CS frame, with intermediate regs
       kept at their init-blob values. Separate 2-byte writes for 0x20 and
       0x24 only latch X — Y stays runaway-fast. Commit with FRAME9 tail. */
    const uint8_t tx_dpi[10] = {
        0x20, code,     // DPI_X
        0x21, 0xFA,
        0x22, 0xC8,
        0x23, 0x97,
        0x24, code,     // DPI_Y
    };
    const uint8_t tx_commit[2] = { 0x02, 0x80 };
    spi_.frame(tx_dpi,    nullptr, sizeof(tx_dpi));
    spi_.frame(tx_commit, nullptr, sizeof(tx_commit));
}

/* Explicit instantiation for the G102 board. When a second board shows up
   add another line here (or move the definitions into the header). */
template class Sensor1855<board::G102,
    platform::SoftCsSpiBus<board::G102::SensorCs>>;

} // namespace drivers
