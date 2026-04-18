# Logitech G102 — Board Reference

> Pin assignments, connections, and layout for the Logitech G102 PCB as
> traced by continuity on 2026-04-18. This is the hardware view that
> underpins [`common/board.h`](../common/board.h). If the header and this
> document disagree, the header is authoritative (it is what the firmware
> actually reads) — but please fix whichever is wrong.

---

## 1. At a glance

```
                     ┌────────────────────────────────┐
                     │   Logitech G102 (clone PCB)    │
                     │                                │
                     │  MCU:    STM32F072CB           │
                     │          128 KiB flash         │
                     │          16 KiB SRAM           │
                     │          USB-FS, HSI48, SPI1   │
                     │                                │
                     │  Sensor: "1855 Rev 007"        │
                     │          Logitech in-house,    │
                     │          likely Mercury family │
                     │          SPI mode 3, ≈1.5 MHz  │
                     │                                │
                     │  USB:    VID:PID 046d:c092     │
                     │          "G102/G203 LIGHTSYNC" │
                     └────────────────────────────────┘
```

Identifies to the host as the stock Logitech G102/G203 LIGHTSYNC so the OS
applies the expected defaults. Device responds correctly to Logitech HID++
requests (verified with Solaar) and carries a Logitech-assigned PCB part
number in the silk — i.e. this is a genuine Logitech board, not a clone.

Hardware inventory (see [images/pcb_top.jpg](images/pcb_top.jpg)):

- **PCB part**: silk reads `210-002146 Rev.006` (Logitech internal number)
- **MCU**: STM32F072CB, LQFP-48
- **Sensor**: `1855 Rev 007` in a small daughter-module at the centre
- **Main switches (LMB/RMB)**: Omron (20M-click rated)
- **Side/wheel micro-switches**: Kailh
- **Scroll encoder**: mechanical quadrature, ~24 detents/rev

All pin assignments below were verified on this specific board by continuity
testing.

---

## 2. Memory map

```
FLASH  0x08000000 ┌──────────────────────────┐
                  │  Bootloader (16 KiB)     │  0x08000000 — 0x08003FFF
       0x08004000 ├──────────────────────────┤
                  │  Firmware  (112 KiB)     │  0x08004000 — 0x0801FFFF
       0x08020000 └──────────────────────────┘

SRAM   0x20000000 ┌──────────────────────────┐
                  │  Stack / BSS / data      │  0x20000000 — 0x20003FFB
       0x20003FFC ├──────────────────────────┤
                  │  BOOT_MAGIC_ADDR (u32)   │  last word, reserved by linker
       0x20004000 └──────────────────────────┘
```

**BOOT_MAGIC** (`0xB007F00D`) is written to the last SRAM word before
`scb_reset_system()` to request a stay-in-bootloader handoff. The linker
scripts (`bootloader/link.ld`, `firmware/link.ld`) reserve that address so
nothing else clobbers it across a reset.

The bootloader self-flash feature is intentionally *not* implemented —
bootloader updates go over ST-Link only.

---

## 3. Pinout (MCU side)

### 3.1 GPIOA

```
  PA0  ─  (unused / reserved)
  PA1  ─  (unused / reserved)
  PA2  ─  (unused / reserved)
  PA3  ─  (unused / reserved on this PCB)
  PA4  ─  (unused / reserved on this PCB)
  PA5  ─  (unused / reserved on this PCB)
  PA6  ─  (unused / reserved on this PCB)
  PA7  ─  (unused / reserved on this PCB)
  PA8  ●  ENC_A        scroll-wheel encoder, quadrature channel A
  PA9  ●  ENC_B        scroll-wheel encoder, quadrature channel B
  PA10 ─  (unused / reserved)
  PA11 ●  USB D-       USB full-speed
  PA12 ●  USB D+       USB full-speed
  PA13 ●  SWDIO        SWD debug
  PA14 ●  SWCLK        SWD debug
  PA15 ●  SENSOR_CS    sensor soft CS, active-LOW, idle-HIGH
```

### 3.2 GPIOB

```
  PB0  ─  (unused / reserved)
  PB1  ─  (unused / reserved)
  PB2  ─  (unused / reserved)
  PB3  ●  SPI1_SCK     AF0 → sensor SCK
  PB4  ●  SPI1_MISO    AF0 → sensor MISO
  PB5  ●  SPI1_MOSI    AF0 → sensor MOSI
  PB6  ●  BTN_MACRO2   side button (thumb, back)
  PB7  ●  BTN_MACRO1   side button (thumb, forward)
  PB8  ─  (unused / reserved)
  PB9  ─  (unused / reserved)
  PB10 ─  (unused / reserved)
  PB11 ─  (unused / reserved)
  PB12 ●  BTN_RMB      right mouse button
  PB13 ●  BTN_SCROLL   middle click (encoder push)
  PB14 ●  BTN_LMB      left mouse button
  PB15 ●  BTN_DPI      DPI button (also the bootloader-entry combo)
```

All button inputs are active-LOW with MCU-internal pull-ups enabled.

### 3.3 Alternate-function summary

```
  Function      Pin     AF      Peripheral
  ──────────    ────    ────    ──────────
  SPI1 SCK      PB3     AF0     SPI1 (sensor)
  SPI1 MISO     PB4     AF0     SPI1 (sensor)
  SPI1 MOSI     PB5     AF0     SPI1 (sensor)
  USB DM        PA11    —       USB_FS (hard-wired)
  USB DP        PA12    —       USB_FS (hard-wired)
  SWD IO        PA13    —       SWD    (hard-wired)
  SWD CLK       PA14    —       SWD    (hard-wired)
```

---

## 4. Physical layout

Top-down view, mouse oriented the way you'd hold it (scroll wheel points
away from you):

```
                         ┌───── FORWARD (+Y in HID) ─────┐
                         │                                │
                         │      ╔═══════════╗             │
             ┌───────────┼──────╣  LMB  RMB ╠───────────┐ │
             │           │      ║   ┌───┐   ║           │ │
             │   MACRO1  │      ║   │ W │   ║           │ │
             │  (thumb   │      ║   │hee│   ║           │ │
             │  forward) │      ║   │ l │   ║           │ │
             │           │      ║   └───┘   ║           │ │
             │   MACRO2  │      ║           ║           │ │
             │  (thumb   │      ║    DPI    ║           │ │
             │  back)    │      ║   button  ║           │ │
             │           │      ╠═══════════╣           │ │
             │           │      ║           ║           │ │
             │           │      ║   "1855"  ║           │ │
             │           │      ║   ◀─X─▶   ║           │ │  ← sensor
             │           │      ║     │     ║           │ │    chip is
             │           │      ║     Y     ║           │ │    rotated 90°
             │           │      ║     ▼     ║           │ │
             │           │      ╚═══════════╝           │ │
             └───────────┼────────────────────────────── ┘ │
                         │                                │
                         └──────────── BACKWARD ──────────┘
```

Button group on the left edge = thumb macros (forward/back), on the top
face = LMB / RMB / wheel-click / DPI. The sensor sits roughly under the
user's palm.

### 4.1 Sensor orientation — critical

The "1855" chip is mounted **rotated 90°** relative to the mouse chassis:

```
                     mouse chassis axes            sensor die axes
                     ───────────────────          ───────────────────
                           forward                       +X
                              ▲                           ▲
                              │                           │
                       left ──┼── right           +Y ─────┼───── (−Y)
                              │                           │
                              ▼                           ▼
                          backward                      (−X)
```

Consequence: `sensor.dx` (reg 0x20 DPI_X) controls **forward/back** speed,
`sensor.dy` (reg 0x24 DPI_Y) controls **left/right**. To produce the HID
deltas the OS expects, the firmware remaps:

```c
hid.dx =  sensor.dy;
hid.dy = -sensor.dx;
```

(The sign on dy is because the sensor's +X points toward the back of the
mouse, but HID +Y is forward.)

If you ever refactor the motion path and the cursor starts going sideways
or inverted, re-check this block first.

---

## 5. Scroll-wheel encoder

Standard two-channel mechanical quadrature encoder. ~24 detents per
revolution on this unit.

```
   A ──┐   ┌───┐   ┌───┐   ┌───       Each detent =
        └─┘   └─┘   └─┘   └─┘         4 edges (×4 decode)
                                       → accumulate state transitions,
   B ────┐   ┌───┐   ┌───┐   ┌─         emit 1 HID wheel tick per detent.
           └─┘   └─┘   └─┘   └─
```

The firmware uses a 4-bit Gray-code state machine (`prev<<2 | curr` → step
table), accumulates ±1 per edge, and divides by 4 to produce one HID tick
per physical detent. Polled from the main loop at ≈1 kHz — mechanical
encoders don't need interrupts.

Both encoder pins use internal pull-ups; the encoder common pin is grounded.

---

## 6. USB

Hard-wired full-speed USB on PA11 (D−) / PA12 (D+). The STM32F072's USB
macrocell is the `st_usbfs_v2` variant; use libopencm3's
`st_usbfs_v2_usb_driver`. Clock: HSI48 → 48 MHz (no external crystal),
via `rcc_clock_setup_in_hsi48_out_48mhz()`.

### 6.1 Descriptors

```
  VID:PID       0x046D : 0xC092    (G102/G203 LIGHTSYNC)
  Manufacturer  "Logitech"
  Product       "G102/G203 LIGHTSYNC Gaming Mouse (CF)"
  Serial        "G102CFW0001"
  Class         HID, boot-mouse subclass
```

Report descriptor is the classic 4-byte boot-protocol layout:

```
  byte 0   buttons bitmap   (bit 0=LMB, 1=RMB, 2=MMB)
  byte 1   dx  (int8, -127..127)
  byte 2   dy  (int8, -127..127)
  byte 3   wheel (int8)
```

### 6.2 Bootloader entry

Holding the DPI button for ≥ ~0.5 s while the firmware is running writes
`BOOT_MAGIC` and triggers a soft reset. The bootloader sees the magic and
stays in DFU mode instead of jumping to firmware. DFU descriptor advertises
DFU 1.1 (`bcdDFUVersion = 0x0110`) — **not** ST's `0x011A` extension, which
makes `dfu-util` try ST-specific GET_STATUS sequences and fail with
`LIBUSB_ERROR_PIPE`.

---

## 7. Power

Bus-powered from USB with a 3.3 V LDO. Declared `bMaxPower = 50` → 100 mA
budget, which is comfortable: MCU + sensor draw peaks around 40 mA.
Remote-wakeup bit is set in the config descriptor for completeness but the
firmware doesn't currently trigger it.

Sensor VCC must be stable for ≥ ~100 ms before the first SPI transaction,
otherwise the init sequence silently corrupts. The firmware enforces this
with a busy-loop before `run_init_sequence()`.

---

## 8. Debug access

```
  SWDIO = PA13
  SWCLK = PA14
  GND   = any ground pad
  3V3   = (optional) present on the programming header; safe to power from
          ST-Link in parallel with USB if you're careful about ground loops,
          but the safer move is USB-power only and SWD for signals.
```

The PCB has unpopulated pads for an SWD header near the MCU. All firmware
on this project has so far been flashed through one of:

- **ST-Link → SWD**, for the bootloader and initial firmware.
- **DFU-over-USB**, for iterative firmware updates once the bootloader is
  alive. Flash the bootloader once, then never again (barring changes to
  the bootloader itself).

---

## 9. Cross-references

- Firmware entry / main loop: [`firmware/src/main.c`](../firmware/src/main.c)
- Sensor driver: [`firmware/src/sensor.c`](../firmware/src/sensor.c),
  [`firmware/src/sensor_frames.h`](../firmware/src/sensor_frames.h)
- Inputs (buttons + encoder): [`firmware/src/inputs.c`](../firmware/src/inputs.c)
- USB HID: [`firmware/src/usb_hid.c`](../firmware/src/usb_hid.c)
- Bootloader + DFU: [`bootloader/src/usb_dfu.c`](../bootloader/src/usb_dfu.c)
- Pin header (authoritative): [`common/board.h`](../common/board.h)
- Sensor protocol / registers: [`docs/SENSOR_1855.md`](SENSOR_1855.md)
