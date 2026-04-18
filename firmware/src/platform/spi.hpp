#pragma once

/*
 * SPI bus wrapper with CS-framed transaction helper.
 *
 * The "1855" sensor wants every logical transaction wrapped by a soft CS
 * edge (there is no peripheral-level framing). This class owns the bus
 * config + a CS pin and exposes a single `frame()` that drives CS low,
 * shifts bytes, and releases CS. Init sequence and sensor driver only
 * see `frame()`.
 */

#include <cstddef>
#include <cstdint>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/spi.h>

namespace platform {

/* Blocking nop-loop delay. Good enough for µs-range CS setup/hold. */
inline void nop_delay(volatile uint32_t n)
{
    while (n--) __asm__ volatile ("nop");
}

template <typename CsPin>
class SoftCsSpiBus {
public:
    explicit SoftCsSpiBus(uint32_t bus_reg) : bus_(bus_reg) {}

    /* Configure SPI1 for mode 3, 8-bit frames, master, software NSS.
       Call once after SPI pins are in AF and the bus clock is on. */
    void init_mode3_1p5mhz()
    {
        rcc_periph_reset_pulse(RST_SPI1);
        spi_init_master(bus_,
            SPI_CR1_BAUDRATE_FPCLK_DIV_32,
            SPI_CR1_CPOL_CLK_TO_1_WHEN_IDLE,
            SPI_CR1_CPHA_CLK_TRANSITION_2,
            SPI_CR1_MSBFIRST);
        spi_set_data_size(bus_, SPI_CR2_DS_8BIT);
        spi_fifo_reception_threshold_8bit(bus_);
        spi_set_full_duplex_mode(bus_);
        spi_enable_software_slave_management(bus_);
        spi_set_nss_high(bus_);
        spi_enable(bus_);
    }

    /* CS-framed transfer: cs_low, shift n bytes, cs_high.
       rx may be nullptr for write-only. */
    void frame(const uint8_t *tx, uint8_t *rx, uint32_t n)
    {
        CsPin::clear();
        nop_delay(kCsSetup);
        for (uint32_t i = 0; i < n; i++) {
            spi_send8(bus_, tx[i]);
            uint8_t r = spi_read8(bus_);
            if (rx) rx[i] = r;
        }
        nop_delay(kCsHold);
        CsPin::set();
        nop_delay(kInterFrame);
    }

private:
    static constexpr uint32_t kCsSetup    = 200;
    static constexpr uint32_t kCsHold     = 200;
    static constexpr uint32_t kInterFrame = 200;
    uint32_t bus_;
};

} // namespace platform
