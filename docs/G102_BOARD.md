# Logitech G102 — Board Reference

> Pin assignments, connections, and layout for the Logitech G102 PCB as
> traced by continuity on 2026-04-18. This is the hardware view that
> underpins [`common/board.hpp`](../common/board.hpp) (and the C shim
> [`common/board.h`](../common/board.h) used by the bootloader). If the
> headers and this document disagree, the headers are authoritative (that
> is what the firmware actually reads) — but please fix whichever is wrong.

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
  PA2  ●  LED1_B       RGB LED 1, blue  channel (high-side anode, low-side switched cathode, HIGH = lit)
  PA3  ●  LED2_G       RGB LED 2, green channel
  PA4  ●  LED2_B       RGB LED 2, blue  channel
  PA5  ─  (unused / reserved)
  PA6  ●  LED2_R       RGB LED 2, red   channel
  PA7  ●  LED3_R       RGB LED 3, red   channel
  PA8  ●  ENC_A        scroll-wheel encoder, quadrature channel A
  PA9  ●  ENC_B        scroll-wheel encoder, quadrature channel B
  PA10 ─  (unused / reserved)
  PA11 ●  USB D-       USB full-speed
  PA12 ●  USB D+       USB full-speed
  PA13 ●  SWDIO        SWD debug (exposed on TP2)
  PA14 ●  SWCLK        SWD debug (exposed on TP3)
  PA15 ●  SENSOR_CS    sensor soft CS, active-LOW, idle-HIGH
```

### 3.2 GPIOB

```
  PB0  ●  LED3_B       RGB LED 3, blue  channel
  PB1  ●  LED3_G       RGB LED 3, green channel
  PB2  ─  (unused / reserved)
  PB3  ●  SPI1_SCK     AF0 → sensor SCK
  PB4  ●  SPI1_MISO    AF0 → sensor MISO
  PB5  ●  SPI1_MOSI    AF0 → sensor MOSI
  PB6  ●  BTN_MACRO2   upper side button (thumb, FORWARD)
  PB7  ●  BTN_MACRO1   lower side button (thumb, BACK)
  PB8  ●  LED1_G       RGB LED 1, green channel
  PB9  ●  LED1_R       RGB LED 1, red   channel
  PB10 ─  (unused / reserved)
  PB11 ─  (unused / reserved)
  PB12 ●  BTN_RMB      right mouse button
  PB13 ●  BTN_SCROLL   middle click (encoder push)
  PB14 ●  BTN_LMB      left mouse button
  PB15 ●  BTN_DPI      DPI button (also the bootloader-entry combo)
```

All button inputs are active-LOW with MCU-internal pull-ups enabled.

Side-button naming is a historical quirk: `BtnMacro1` (PB7) is the
lower/rear button and maps to HID `BTN_BACK`; `BtnMacro2` (PB6) is the
upper/front button and maps to HID `BTN_FORWARD`. Physical verification
was done via the bootloader's "forward-button boots firmware" shortcut,
so the correspondence is confirmed on hardware.

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
             │   MACRO2  │      ║   │ W │   ║           │ │
             │  (thumb   │      ║   │hee│   ║           │ │
             │  forward  │      ║   │ l │   ║           │ │
             │   / PB6)  │      ║   └───┘   ║           │ │
             │   MACRO1  │      ║           ║           │ │
             │  (thumb   │      ║    DPI    ║           │ │
             │  back     │      ║   button  ║           │ │
             │   / PB7)  │      ║           ║           │ │
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
  SWDIO = PA13   → exposed on test point TP2
  SWCLK = PA14   → exposed on test point TP3
  NRST           → exposed on test point TP4
  GND   = any ground pad / USB shield — MUST be tied to ST-Link GND even
                   when not sharing 3V3; SWD signals need a common reference
  3V3   = DO NOT source from the ST-Link; power the board from USB. Sourcing
                   3V3 from two supplies makes ground loops that upset SWD on
                   some clone probes.
```

There is no populated SWD header; those three pads (TP2/TP3/TP4) are all
you get.

### 8.1 BOOT0

`BOOT0` is tied to ground through **R20**. With BOOT0 low the MCU boots
from main flash — which means at reset the stock firmware runs and SWD
can't get a clean halt on a locked part. To enter the STM32 system-memory
bootloader (where the MCU halts before running any code), tie BOOT0 to
3V3 through a wire — a 1–10 kΩ resistor is safer than a direct short.

**Keep BOOT0 high for the entire SWD session.** Dropping it mid-session
exits system-memory bootloader mode and SWD loses the target.

### 8.2 RDP / WRP — one-way unlock

The stock firmware ships with **RDP Level 1** and write-protection on the
first flash pages. You cannot read back, dump, or selectively rewrite the
stock firmware — the only path forward is to unlock, which triggers a
**hardware mass-erase**. Once unlocked, the original Logitech firmware is
gone for good; there is no backup path. Proceed only if you intend to run
custom firmware from now on.

Sequence with `openocd` (driving an ST-Link through SWD):

```
  1. BOOT0 = 3V3, USB plugged in.
  2. stm32f0x unlock 0           → drops RDP to Level 0, mass-erases flash.
  3. flash protect 0 0 15 off    → clears the 16 WRP blocks that stick
                                    even after RDP=0 on some parts.
  4. program <bootloader>.elf verify reset
  5. Unplug USB; release BOOT0; replug. Bootloader is live.
```

After the bootloader is in place, the rest of the work happens over
USB DFU — you only need SWD again if you want to change the bootloader
itself.

### 8.3 Flash paths used

- **ST-Link → SWD**, for the bootloader (one-off per board).
- **DFU-over-USB**, for iterative firmware updates once the bootloader is
  alive. Flash the bootloader once, then never again (barring changes to
  the bootloader itself).

---

## 9. Cross-references

- Firmware entry / main loop: [`firmware/src/app/main.cpp`](../firmware/src/app/main.cpp)
- Sensor driver: [`firmware/src/drivers/sensor_1855.cpp`](../firmware/src/drivers/sensor_1855.cpp),
  [`firmware/src/drivers/sensor_1855.hpp`](../firmware/src/drivers/sensor_1855.hpp),
  [`firmware/src/sensor_frames.h`](../firmware/src/sensor_frames.h)
- Buttons: [`firmware/src/drivers/buttons.hpp`](../firmware/src/drivers/buttons.hpp)
- Scroll encoder: [`firmware/src/drivers/encoder_quad.hpp`](../firmware/src/drivers/encoder_quad.hpp)
- RGB lighting: [`firmware/src/drivers/lighting.hpp`](../firmware/src/drivers/lighting.hpp),
  [`firmware/src/drivers/lighting.cpp`](../firmware/src/drivers/lighting.cpp)
- USB HID: [`firmware/src/services/usb_hid.cpp`](../firmware/src/services/usb_hid.cpp)
- Bootloader + DFU: [`bootloader/src/usb_dfu.c`](../bootloader/src/usb_dfu.c),
  [`bootloader/src/led_indicator.c`](../bootloader/src/led_indicator.c)
- Pin headers (authoritative): [`common/board.hpp`](../common/board.hpp)
  (firmware), [`common/board.h`](../common/board.h) (bootloader C shim)
- Sensor protocol / registers: [`docs/SENSOR_1855.md`](SENSOR_1855.md)
