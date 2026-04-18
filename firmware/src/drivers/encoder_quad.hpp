#pragma once

/*
 * Gray-code quadrature decoder for a 2-channel mechanical scroll encoder.
 *
 * One physical detent = 4 edges (×4 decode). The accumulator runs at every
 * state transition, and we emit a HID wheel tick every 4 counts.
 *
 * Call init() once, poll() from the main loop (≈1 kHz), and
 * consume_ticks() when you assemble a HID report.
 */

#include <cstdint>

namespace drivers {

template <typename Board>
class QuadEncoder {
public:
    void init()
    {
        using namespace platform;
        enable_port_clock(Board::EncA::port);
        Board::EncA::setup_input_pullup();
        Board::EncB::setup_input_pullup();
        last_ = phase();
        accum_  = 0;
        ticks_  = 0;
    }

    /* Drive the decoder — must be called frequently enough that we don't
       miss two edges in one polling window (~1 kHz is plenty for a hand
       spinning a scroll wheel). */
    void poll()
    {
        uint8_t now = phase();
        if (now == last_) return;

        /* (old<<2)|new → step table. Invalid/bounce entries read as 0 and
           naturally stall the counter rather than emitting spurious ticks. */
        static constexpr int8_t kStep[16] = {
             0, -1, +1,  0,
            +1,  0,  0, -1,
            -1,  0,  0, +1,
             0, +1, -1,  0,
        };
        int8_t d = kStep[(last_ << 2) | now];
        last_ = now;

        accum_ = static_cast<int8_t>(accum_ + d);
        if (accum_ >= +4) { emit(+1); accum_ = 0; }
        if (accum_ <= -4) { emit(-1); accum_ = 0; }
    }

    /* Returns accumulated ticks since last call and clears the counter. */
    int8_t consume_ticks()
    {
        int8_t t = ticks_;
        ticks_ = 0;
        return t;
    }

private:
    static uint8_t phase()
    {
        return (Board::EncA::read() ? 1 : 0) | (Board::EncB::read() ? 2 : 0);
    }

    void emit(int8_t step)
    {
        int16_t next = static_cast<int16_t>(ticks_) + step;
        if (next > +127) next = +127;
        if (next < -127) next = -127;
        ticks_ = static_cast<int8_t>(next);
    }

    uint8_t last_   = 0;
    int8_t  accum_  = 0;
    int8_t  ticks_  = 0;
};

} // namespace drivers
