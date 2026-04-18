# "1855" Optical Sensor — Reference Manual (reverse-engineered)

> **Sensor identity:** the chip in the Logitech G102 LIGHTSYNC silkscreens as
> `1855 Rev 007`. No vendor logo is present on the package and no public
> PixArt PMW/PAW datasheet matches the observed register map. Circumstantial
> evidence points to this being a revision of **Logitech's in-house "Mercury"
> sensor family**: earlier G102/G203 Prodigy boards (`210-001709 Rev.002`)
> carry a sensor module silkscreened "Mercury" in the same footprint, and the
> protocol here (SROM upload via 2E/2F auto-increment, `0x02` commit latch,
> CS-framed transactions, motion burst at `0x80+`) matches the shape described
> in the public reverse of HERO — Logitech's documented successor to Mercury
> ([hero-re](https://github.com/you-wouldnt-reverse-engineer-a-rat/hero-re)).
>
> Enthusiast databases (sensor.fyi, EloShapes) label this part "PixArt 1855",
> but no primary source for the PixArt attribution has been found. We have
> not captured SPI from a Mercury-era G102 to do a byte-level comparison, so
> the Mercury-family claim is by protocol-shape inference, not confirmed.
>
> This document is assembled entirely from empirical observation: Saleae
> captures of the stock Logitech G102 firmware, bit-banged probing on a
> Raspberry Pi, and live register dumps. Every number here has been observed,
> not guessed.

---

## 1. Overview

The "1855" is a low-cost optical mouse sensor found in the Logitech G102
LIGHTSYNC and clones. It is register-based, SPI-driven, and presents motion
data through a single compact burst read. No SROM upload is required for
motion mode (unlike PMW3360/3389) — the sensor ships ready to track after a
fixed init sequence.

```
                   ┌────────────────────────┐
                   │                        │
    SCK  ─────────▶│ "1855"                 │
    MOSI ─────────▶│  optical mouse sensor  │
    MISO ◀─────────│                        │
    CS   ─────────▶│  (soft chip-select)    │
                   │                        │
    VCC  ─────────▶│  (3.3 V)               │
    GND  ─────────▶│                        │
                   └────────────────────────┘
                            │
                            ▼ laser diode + photo array
                            (physical X axis → sensor forward/back)
```

---

## 2. Electrical / SPI

### 2.1 Signals

| Signal | Direction | Notes |
|--------|-----------|-------|
| SCK    | host → sensor | Clock. Idle HIGH. |
| MOSI   | host → sensor | Data in, MSB first. |
| MISO   | sensor → host | High-Z when CS is deasserted (see §2.3). |
| CS     | host → sensor | Soft chip-select, active LOW. |
| VCC    | — | 3.3 V. Datasheet-unknown exact tolerance; stock supply is a buck reg from USB 5 V. |
| GND    | — | — |

### 2.2 SPI mode

**Mode 3** (CPOL = 1, CPHA = 1):

```
            CS ──┐                                                 ┌───
                 └─────────────────────────────────────────────────┘
                  ◀ setup ▶                                ◀ hold ▶
           SCK    ┌─┐   ┌─┐   ┌─┐   ┌─┐   ┌─┐   ┌─┐   ┌─┐   ┌─┐
            ──────┘ └───┘ └───┘ └───┘ └───┘ └───┘ └───┘ └───┘ └──── (idle HIGH)
                    ▲                                          ▲
             rising edge: sensor samples MISO out
                          (host samples MISO in)
          falling edge: host shifts next MOSI bit
```

- Clock rate: stock firmware uses ≈ 1.5 MHz. Sensor is happy up to ≈ 2 MHz in
  our measurements; above that, register reads start losing bits.
- Frame size: 8 bits, MSB first.

### 2.3 Chip-select framing (critical)

**Every logical transaction MUST be wrapped by CS low → bytes → CS high.**
The sensor has *no* internal framing (no sync byte, no length field). CS
edges are the only transaction delimiters.

MISO is high-Z while CS is high. This bit us hard during bringup: without
CS asserted, capacitive coupling between MOSI and the floating MISO line
produced a ghostly ≈ 1-bit-shifted echo of MOSI, which looked *exactly*
like a sensor response. If you think your sensor is "responding but
garbage," verify CS first.

Required timing around CS edges (empirically reliable, comfortably
conservative):

```
CS ──┐◀──── ≥ ~1 µs ─────▶◀──── byte stream ────▶◀─ ≥ ~1 µs ─┐
     └                                                       └──
                    CS-to-SCK           SCK-to-CS-rise
                      setup                 hold

                  + ≥ ~1 µs inter-frame gap before next CS low
```

In the g102-custom-fw firmware these are implemented as `delay_loop(200)`
nop spins at 48 MHz, which is roughly 4 µs each — wasteful but bulletproof.

---

## 3. Register access

### 3.1 Byte-level protocol

Each register access is one CS-framed 2-byte transaction.

**Write:**

```
 ┌────────┬────────┐
 │   addr │  value │       upper bit of addr = 0
 └────────┴────────┘
MOSI:  addr, value
MISO:  xx,   xx              (ignored)
```

**Read:**

```
 ┌────────┬────────┐
 │ 80|addr│  0x00  │       upper bit of addr = 1  → read request
 └────────┴────────┘
MOSI:  0x80|addr, 0x00
MISO:  xx (junk), value      ← value is the second MISO byte
```

The first MISO byte in any transaction is always junk (sensor hasn't
latched the address yet). This is why motion-burst reads send a 5-byte
request but real data only starts at `resp[1]`.

### 3.2 Motion burst

A single burst read pulls the entire motion snapshot atomically:

```
MOSI:  0x80  0x85  0x86  0x87  0x80
MISO:  [ --  stat   dxL   dyL   dxyH ]
        │    │     │     │     │
        │    │     │     │     └─ packed high nibbles (see §3.3)
        │    │     │     └─────── dy low 8 bits  (signed)
        │    │     └───────────── dx low 8 bits  (signed)
        │    └─────────────────── motion status
        └──────────────────────── junk (address-latch cycle)
```

The burst is one CS frame — do not raise CS between bytes. The trailing
`0x80` in MOSI re-arms motion latching for the next read.

### 3.3 12-bit deltas

dx/dy are natively signed **12-bit**. The low 8 bits land in `dxL`/`dyL`;
the high 4 bits of each are packed into the fifth byte `dxyH`:

```
  dxyH =  [ dx[11:8] | dy[11:8] ]
           bits 7..4    bits 3..0

  dx_s16 = sign_extend_12( (dxH << 8) | dxL )
  dy_s16 = sign_extend_12( (dyH << 8) | dyL )
```

For USB HID boot-protocol mice (8-bit deltas) the low byte alone is fine —
reports are sent every 1 ms and physical motion within one poll rarely
exceeds ±127 counts even at high DPI. Clip, don't scale.

### 3.4 Motion status

`status = resp[1]` after a motion burst:

| Bit | Meaning |
|-----|---------|
| 7   | **MOT** — set if motion occurred since the last burst read. Clears on read. |
| 6:0 | Unspecified. Observed stable `0x12` at rest, `0x03` during motion — looks like a state machine / surface-quality summary, not essential for motion decode. |

Typical at-rest value: `0x92`. Typical in-motion: `0x83`. Only bit 7 matters
for the polling loop.

---

## 4. Register map (empirical)

Post-init dump from a live G102 after the stock firmware's init sequence,
captured with `bbspi map 0x00 0x7F`:

```
       +0  +1  +2  +3  +4  +5  +6  +7  +8  +9  +A  +B  +C  +D  +E  +F
 0x00: 92  01  80  00  03  00  00  00  00  60  AA  40  06  06  11  02
 0x10: 0C  08  00  32  06  03  30  0B  00  00  00  00  00  00  00  20
 0x20: 1F  FA  C8  97  1F  0D  F2  76  A0  80  00  00  00  F1  F0  00
 0x30: 16  40  0A  0A  12  0B  88  00  FF  FF  FF  FF  11  10  0A  A1
 0x40: 00  00  00  00  00  00  01  01  02  00  3B  CC  00  00  00  00
 0x50: 00  00  00  00  00  00  00  00  00  00  00  00  00  40  03  02
 0x60: 14  09  00  0A  06  03  20  00  00  00  00  00  00  00  00  00
 0x70: 00  03  FF  00  00  00  00  00  00  00  00  10  00  00  00  00
```

Confirmed functions:

| Addr | Name | Default | Notes |
|------|------|---------|-------|
| 0x00 | Motion_Status         | 0x92 | see §3.4 |
| 0x02 | Commit / latch        | —    | written 0x80 by init FRAME9 to apply staged config |
| 0x0A | Rev_ID                | 0xAA | constant, use as init-OK sanity check |
| 0x20 | DPI_X                 | 0x1F | see §5 — **forward/back** on G102 (chip rotated) |
| 0x21..0x23 | (config)        | FA C8 97 | part of DPI burst; keep at init values when rewriting 0x20/0x24 |
| 0x24 | DPI_Y                 | 0x1F | **left/right** on G102 |
| 0x3B, 0x3C | SQUAL / shutter | —    | fluctuate during motion; surface-quality telemetry |
| 0x3D | (config/enable)       | 0x10 | stable |

Not a register function:

- **0x07, 0x08** — always read 0. Motion is *only* exposed via the 5-byte
  burst, not via direct reads of hypothetical dx/dy registers.
- **0x58** — NOT a frame-grabber address (writing 0x12←0x83 then reading
  0x58 1024× returns all zeros). The "1855" does not expose a raw-frame dump
  mechanism through the same mechanism PMW3360/3389 use.

### 4.1 DPI scaling

0x20/0x24 is not linear counts-per-inch. Empirical points over identical
physical motion:

```
  reg = 0x08  → Σ|dx| ≈    257    (≈ 1× baseline)
  reg = 0x50  → Σ|dx| ≈  12277    (≈ 48× baseline)
```

Scale grows faster than register value → likely exponential or piecewise.
For a "sane startup" value, `0x08` feels about right on a normal desk pad;
`0x1F` (init default) is already quite fast.

### 4.2 Committing DPI changes

**Important:** writing 0x20 and 0x24 as two independent CS-framed
transactions leaves Y in a shadow register. The sensor only latches after
a "commit" write to 0x02.

Correct DPI update sequence (one CS-framed burst, then commit):

```
  CS frame 1:  20 <code>  21 FA  22 C8  23 97  24 <code>
  CS frame 2:  02 80
```

Why that particular burst: the stock init sequence ends with FRAME8
containing `... 20 1F 21 FA 22 C8 23 97 24 1F ...`, then FRAME9 pokes
`02 80`. Replaying the same shape is what makes the sensor apply both
axes in lockstep. Rewriting just `0x20 <code>` and `0x24 <code>` (bare)
applies X only.

---

## 5. Power-on initialization

Boot order observed on the stock firmware:

1. Hold VCC stable for ≥ ~100 ms before first SPI transaction.
2. Play 10 CS-framed transactions (`FRAME0..FRAME9`) verbatim. These are
   extracted from a Saleae capture of the stock G102 boot and embedded as
   `sensor_frames.h` in the g102-custom-fw firmware.
3. Wait **166 ms**.
4. Replay the same 10 frames a second time.
5. After that the sensor is in motion mode; burst reads work immediately.

Frame sizes (bytes): 2, 2, 2103, 1580, 83, 2, 14, 2, 34, 2. FRAME2 and
FRAME3 are large because they stream SROM-like config tables; FRAME8
sets the DPI/config block; FRAME9 commits.

The double-play with a 166 ms gap is not optional — single-play leaves the
sensor in a degraded state where motion reads return status but no deltas.
We don't know why; the stock firmware does it, so we do.

---

## 6. Motion polling

Steady-state read loop (g102-custom-fw firmware runs this at 1 kHz,
synchronized with USB HID polls):

```c
tx[5] = { 0x80, 0x85, 0x86, 0x87, 0x80 };
spi_frame(tx, rx, 5);                       // one CS frame

if (rx[1] & 0x80) {                         // MOT bit
    int8_t dx = (int8_t)rx[2];
    int8_t dy = (int8_t)rx[3];
    // (high nibbles in rx[4] if you want 12-bit precision)
}
```

Typical loop cost at 1.5 MHz SCK: 5 bytes × 8 bits / 1.5 MHz ≈ 27 µs, plus
~12 µs of CS setup/hold → ≈ 40 µs per burst. Easily fits inside a 1 ms
HID polling window.

---

## 7. Known unknowns

Things we have *not* verified on this sensor, but which are plausible given
its protocol shape (Mercury/HERO-family):

- Lift-off distance control register
- Power-saving / rest modes (0x15 looks suspiciously like a rest-timer byte,
  defaults to 0x32 = 50; untested)
- Frame capture (ruled out via 0x58; unclear if exposed elsewhere at all)
- LED current / illumination control

If any of these turn out to matter for your application, probe them
carefully — sensors in this class have been known to brick semi-permanently
when reserved bits are poked.

---

## 8. Tooling

- **Pi-side bit-bang**: `bbspi` on a Pi Zero W (GPIO11/10/9/8 = SCK/MOSI/MISO/CS).
  Mode 3, `--delay 500` nop spins per half-bit for reliable post-init motion.
- **Decoder**: `captures/sensor_1855_dasm.py` — CS-aware SPI decoder
  that splits a Saleae CSV into logical transactions.
- **Captures**: stock-firmware boot capture in Saleae format, SROM-like blobs
  in `sensor_frames.h`.
