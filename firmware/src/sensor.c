/*
 * "1855" optical sensor driver. Chip silk reads `1855 Rev 007`; Logitech
 * in-house, likely a revision of the Mercury family (see docs/SENSOR_1855.md
 * for the evidence). Protocol is PixArt-family in shape (SROM 2E/2F upload,
 * 0x02 commit, CS-framed) but the part is not a documented PixArt PMW/PAW.
 *
 * Wiring (traced on the G102 PCB):
 *   SPI1 SCK   PB3 (AF0)
 *   SPI1 MISO  PB4 (AF0)
 *   SPI1 MOSI  PB5 (AF0)
 *   CS (soft)  PA15
 *
 * Protocol: SPI mode 3 (CPOL=1, CPHA=1), MSB first, 8-bit frames, ~2 MHz.
 * Every logical transaction must be wrapped by CS low → bytes → CS high —
 * there is no chip-level framing, CS edges delimit transactions.
 *
 * Register access (derived from the captured init capture):
 *   write: [reg, value]            — upper bit of `reg` is 0
 *   read:  [0x80|reg, 0x00] → MISO byte 1 holds the register value
 *
 * Power-on init is a straight replay of ten CS-framed transactions extracted
 * from a scope capture of the stock firmware; the sequence is done twice with
 * a 166 ms gap (matches the stock). Afterwards we can poll a motion burst
 * (regs 0x00, 0x05, 0x06, 0x07 in one CS-framed read) every millisecond.
 */

#include <stddef.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include "board.h"
#include "sensor.h"
#include "sensor_frames.h"

#define SPI_BUS         SPI1
#define SPI_SCK_PIN     GPIO3
#define SPI_MISO_PIN    GPIO4
#define SPI_MOSI_PIN    GPIO5
#define SPI_AF          GPIO_AF0

/* Motion burst layout: {0x80, 0x85, 0x86, 0x87, 0x80}.
   Per reverse-engineering notes on this sensor: MISO comes back as
   [hdr, status, dxL, dyL, dxyH] where dx/dy are signed 12-bit, with the
   high nibbles packed in dxyH (upper nibble = X, lower = Y). For now we
   treat dx/dy as 8-bit and clip, which matches the HID boot-protocol
   report range anyway. */
#define MOTION_LEN      5

static void delay_loop(volatile uint32_t n)
{
    while (n--) __asm__ volatile ("nop");
}

static void cs_low(void)  { gpio_clear(SENSOR_CS_PORT, SENSOR_CS_PIN); }
static void cs_high(void) { gpio_set  (SENSOR_CS_PORT, SENSOR_CS_PIN); }

static uint8_t spi_xfer_byte(uint8_t tx)
{
    spi_send8(SPI_BUS, tx);
    return spi_read8(SPI_BUS);
}

static void spi_frame(const uint8_t *tx, uint8_t *rx, uint32_t n)
{
    cs_low();
    delay_loop(200);                    // CS-to-SCK setup
    for (uint32_t i = 0; i < n; i++) {
        uint8_t r = spi_xfer_byte(tx[i]);
        if (rx) rx[i] = r;
    }
    delay_loop(200);                    // last-SCK-to-CS-rise hold
    cs_high();
    delay_loop(200);                    // inter-frame gap
}

static void spi_pins_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_SPI1);

    // CS on PA15, push-pull output, idle high.
    gpio_mode_setup(SENSOR_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SENSOR_CS_PIN);
    gpio_set_output_options(SENSOR_CS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_HIGH, SENSOR_CS_PIN);
    cs_high();

    // SCK/MOSI/MISO on PB3/5/4, AF0.
    gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE,
                    SPI_SCK_PIN | SPI_MISO_PIN | SPI_MOSI_PIN);
    gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_HIGH,
                    SPI_SCK_PIN | SPI_MOSI_PIN);
    gpio_set_af(GPIOB, SPI_AF, SPI_SCK_PIN | SPI_MISO_PIN | SPI_MOSI_PIN);
}

static void spi_bus_init(void)
{
    rcc_periph_reset_pulse(RST_SPI1);
    // F0 SPI is v2: 8-bit data via CR2.DS, FRXTH for 8-bit RX threshold.
    spi_init_master(SPI_BUS,
        SPI_CR1_BAUDRATE_FPCLK_DIV_32,  // 48 MHz / 32 = 1.5 MHz — sensor is fine up to ~2 MHz
        SPI_CR1_CPOL_CLK_TO_1_WHEN_IDLE,
        SPI_CR1_CPHA_CLK_TRANSITION_2,
        SPI_CR1_MSBFIRST);
    spi_set_data_size(SPI_BUS, SPI_CR2_DS_8BIT);
    spi_fifo_reception_threshold_8bit(SPI_BUS);
    spi_set_full_duplex_mode(SPI_BUS);
    spi_enable_software_slave_management(SPI_BUS);
    spi_set_nss_high(SPI_BUS);
    spi_enable(SPI_BUS);
}

static void run_init_sequence(void)
{
    static const uint8_t * const frames[10] = {
        FRAME0, FRAME1, FRAME2, FRAME3, FRAME4,
        FRAME5, FRAME6, FRAME7, FRAME8, FRAME9,
    };
    static const uint32_t sizes[10] = {
        sizeof(FRAME0), sizeof(FRAME1), sizeof(FRAME2), sizeof(FRAME3), sizeof(FRAME4),
        sizeof(FRAME5), sizeof(FRAME6), sizeof(FRAME7), sizeof(FRAME8), sizeof(FRAME9),
    };

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 10; i++) {
            spi_frame(frames[i], NULL, sizes[i]);
        }
        if (pass == 0) {
            // 166 ms gap between the two passes — measured against stock.
            for (volatile uint32_t t = 0; t < 48000u * 166u; t++) __asm__ volatile ("nop");
        }
    }
}

void sensor_init(void)
{
    spi_pins_init();
    spi_bus_init();

    // Sensor needs ~100 ms of VCC-stable before accepting SPI.
    for (volatile uint32_t t = 0; t < 48000u * 100u; t++) __asm__ volatile ("nop");

    run_init_sequence();

    // The init blob leaves DPI fairly high; clamp to a sane startup value.
    // Empirically 0x08 is moderate (≈1× baseline), 0x50 is very fast.
    sensor_set_dpi(0x08);
}

int sensor_read_motion(int8_t *dx, int8_t *dy)
{
    static const uint8_t motion_req[MOTION_LEN] = { 0x80, 0x85, 0x86, 0x87, 0x80 };
    uint8_t resp[MOTION_LEN];

    spi_frame(motion_req, resp, MOTION_LEN);

    // resp[1]=status, resp[2]=dxL, resp[3]=dyL, resp[4]=dxyH.
    // Bit 7 of status set = motion since last read.
    if ((resp[1] & 0x80) == 0) {
        *dx = 0;
        *dy = 0;
        return 0;
    }
    *dx = (int8_t)resp[2];
    *dy = (int8_t)resp[3];
    return 1;
}

void sensor_set_dpi(uint8_t code)
{
    // Replay the tail of FRAME8 (0x20..0x24 one burst) + FRAME9 commit so
    // the sensor latches both axes. Keep registers 0x21..0x23 at their
    // init-blob values — they're not DPI, just happen to sit between.
    uint8_t tx_dpi[10] = {
        0x20, code,     // DPI_X  (forward/back on G102 — sensor rotated 90°)
        0x21, 0xFA,
        0x22, 0xC8,
        0x23, 0x97,
        0x24, code,     // DPI_Y  (left/right on G102)
    };
    uint8_t tx_commit[2] = { 0x02, 0x80 };
    spi_frame(tx_dpi, NULL, sizeof(tx_dpi));
    spi_frame(tx_commit, NULL, sizeof(tx_commit));
}
