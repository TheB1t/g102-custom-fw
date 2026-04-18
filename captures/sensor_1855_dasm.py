#!/usr/bin/env python3
""""1855" sensor SPI capture disassembler (PixArt-family protocol).

Input : Saleae Logic CSV with columns [Time, SCK, MOSI, MISO, CS].
        CS is active-low; each CS-low pulse is one SPI transaction.
        If the CSV has only 3 channels (no CS), falls back to gap-based
        segmentation for backward compatibility.
Output: annotated listing of SPI transactions with semantic decoding.

Usage:
    sensor_1855_dasm.py <capture.csv> [-o out.txt] [--full-srom] [--max-polls N]

Decodes:
  - Register writes (addr < 0x80, addr followed by data byte)
  - Register reads (addr | 0x80, data on MISO next byte)
  - SROM upload preamble / 2E/2F auto-increment stream
  - SROM readback via AE/AF/80 triplets (with 2-byte MISO phase alignment)
  - Motion poll (5-byte: 80 85 86 87 80 → status, Δx_L, Δy_L, ΔxyH)
  - Decodes Δxy high nibbles: upper=X high, lower=Y high (signed 12-bit)

Heuristic segmentation:
  Bytes are grouped into transactions using an inter-byte-gap threshold
  (default 50 µs — large enough to ignore intra-byte jitter, small enough
  to keep the monolithic init transaction intact).
"""

import argparse
import csv
import sys
from collections import Counter
from dataclasses import dataclass, field
from typing import Iterable

# --------------------------------------------------------------------------
# Known register names. Names marked "?" are inferred from observed behavior.
# --------------------------------------------------------------------------
REG_NAMES = {
    0x00: "Motion_Status",
    0x02: "Config_02?",
    0x05: "Delta_X_L",
    0x06: "Delta_Y_L",
    0x07: "Delta_XY_H",
    0x09: "Config_09?",
    0x0A: "Rev_ID?",
    0x0B: "Config_0B?",
    0x0C: "Config_0C?",
    0x0D: "Config_0D?",
    0x0E: "Config_0E?",
    0x1F: "Config_1F?",
    0x20: "DPI_X?",
    0x21: "Pwr_Thresh_21?",
    0x22: "Pwr_Thresh_22?",
    0x23: "Pwr_Thresh_23?",
    0x24: "DPI_Y?",
    0x25: "Pwr_Thresh_25?",
    0x26: "Pwr_Thresh_26?",
    0x27: "Pwr_Thresh_27?",
    0x28: "Pwr_Thresh_28?",
    0x29: "Pwr_Thresh_29?",
    0x2A: "SROM_Cmd",
    0x2B: "SROM_Mode",
    0x2C: "SROM_Ctrl",
    0x2D: "SROM_Addr",
    0x2E: "SROM_Data_L",
    0x2F: "SROM_Data_H",
    0x30: "Tune_30?",
    0x31: "Tune_31?",
    0x32: "Tune_32?",
    0x33: "Tune_33?",
    0x34: "Tune_34?",
    0x35: "Tune_35?",
    0x36: "Tune_36?",
    0x3D: "Motion_En?",
    0x40: "Pixel_Ch_Sel?",
    0x43: "Pixel_Ch_Ctrl?",
    0x61: "Config_61?",
    0x76: "Unlock?",
    0xCA: "Pixel_Calib_CA?",
    0xCB: "Pixel_Calib_CB?",
}

PREAMBLE_6 = [(0x2A, 0xCF), (0x2B, 0x06), (0x2C, 0x0A),
              (0x2D, 0x00), (0x2E, 0x62), (0x2F, 0x80)]

POLL_PATTERN = (0x80, 0x85, 0x86, 0x87, 0x80)

# --------------------------------------------------------------------------
# CSV parsing: edge-compressed or per-sample, SCK/MOSI/MISO on first three chans
# --------------------------------------------------------------------------

def detect_channels(path):
    """Return number of channel columns (excluding time)."""
    with open(path) as f:
        r = csv.reader(f)
        header = next(r)
        return len(header) - 1

def parse_csv(path, has_cs):
    """Yield (timestamp, sck, mosi, miso, cs) tuples from the capture.
    If has_cs is False, cs is always 0 (always-active)."""
    with open(path) as f:
        r = csv.reader(f)
        next(r)
        if has_cs:
            for row in r:
                yield (float(row[0]), int(row[1]), int(row[2]),
                       int(row[3]), int(row[4]))
        else:
            for row in r:
                yield (float(row[0]), int(row[1]), int(row[2]),
                       int(row[3]), 0)

def extract_tx_by_cs(sample_iter):
    """Walk sample stream, produce list of transactions.
    Each transaction = list of (ts, mosi_byte, miso_byte).
    Transaction boundaries = CS falling (open) / rising (close).
    Bits are sampled on SCK rising while CS=0. MSB-first, 8 bits per byte."""
    sck = 1; mosi = 0; miso = 0; cs = 1
    cur_ts = []; cur_mo = []; cur_mi = []
    in_tx = False
    txs = []

    def flush():
        nonlocal cur_ts, cur_mo, cur_mi
        out = []
        n = len(cur_ts) - (len(cur_ts) % 8)
        for i in range(0, n, 8):
            bm = 0; br = 0
            for j in range(8):
                bm = (bm << 1) | cur_mo[i + j]
                br = (br << 1) | cur_mi[i + j]
            out.append((cur_ts[i], bm, br))
        cur_ts, cur_mo, cur_mi = [], [], []
        return out

    for ts, nsck, nm, ni, nc in sample_iter:
        # CS falling edge: open new transaction
        if nc == 0 and cs == 1:
            cur_ts, cur_mo, cur_mi = [], [], []
            in_tx = True
        # CS rising edge: close current transaction
        if nc == 1 and cs == 0 and in_tx:
            tx = flush()
            if tx:
                txs.append(tx)
            in_tx = False
        # Sample on SCK rising edge while CS low
        if nsck == 1 and sck == 0 and nc == 0 and in_tx:
            cur_ts.append(ts)
            cur_mo.append(mosi)
            cur_mi.append(miso)
        sck, mosi, miso, cs = nsck, nm, ni, nc
    # Trailing open transaction (capture ended mid-frame)
    if in_tx:
        tx = flush()
        if tx:
            txs.append(tx)
    return txs

def extract_tx_by_gap(sample_iter, gap_us):
    """Legacy path for 3-channel captures: extract bytes, group by gap."""
    sck = 1; mosi = 0; miso = 0
    ts_bits = []; mo_bits = []; mi_bits = []
    for ts, nsck, nm, ni, _ in sample_iter:
        if nsck == 1 and sck == 0:
            ts_bits.append(ts)
            mo_bits.append(mosi)
            mi_bits.append(miso)
        sck = nsck; mosi = nm; miso = ni
    bstream = []
    for i in range(0, len(ts_bits) - 7, 8):
        bm = 0; br = 0
        for j in range(8):
            bm = (bm << 1) | mo_bits[i + j]
            br = (br << 1) | mi_bits[i + j]
        bstream.append((ts_bits[i], bm, br))
    if not bstream:
        return []
    txs = [[bstream[0]]]
    for b in bstream[1:]:
        if (b[0] - txs[-1][-1][0]) * 1e6 > gap_us:
            txs.append([])
        txs[-1].append(b)
    return txs

# --------------------------------------------------------------------------
# Decoders
# --------------------------------------------------------------------------

def reg_name(addr):
    return REG_NAMES.get(addr & 0x7F, f"reg_{addr & 0x7F:02X}")

def sign12(v):
    return v - 0x1000 if v & 0x800 else v

@dataclass
class DecodedTx:
    idx: int
    ts: float
    size: int
    kind: str                      # 'preinit' / 'init' / 'poll' / 'poll+tweak' / 'generic'
    lines: list = field(default_factory=list)

def decode_preamble(m):
    """Recognize the known 12-byte SROM preamble."""
    pairs = [(m[i], m[i + 1]) for i in range(0, 12, 2)]
    if pairs == PREAMBLE_6:
        return ["; SROM loader preamble (standard)"] + [
            f"    WR  {a:02X} ({reg_name(a):15s}) = {v:02X}"
            for a, v in pairs
        ]
    return [f"; preamble (non-standard): {bytes(m[:12]).hex()}"]

def walk_srom_upload(m, start):
    """Walk the (2E lo, 2F hi) auto-increment stream from `start`.
    Return (upload_bytes, end_offset)."""
    up = []
    i = start
    while i + 3 < len(m):
        if m[i] == 0x2E and m[i + 2] == 0x2F:
            up.append(m[i + 1])
            up.append(m[i + 3])
            i += 4
        else:
            break
    return up, i

def walk_readback(m, r, start):
    """Walk (AE AF 80) triplets starting from `start`.
    Returns (readback_bytes, end_offset, trip_count).
    MISO[i+1] = previous reg value; readback data live at i+1,i+2
    with a 2-byte phase shift, which we correct by dropping the first word."""
    rb = []
    trips = 0
    i = start
    while i + 2 < len(m):
        if m[i] == 0xAE and m[i + 1] == 0xAF and m[i + 2] == 0x80:
            if i + 2 < len(r):
                rb.append(r[i + 1])
                rb.append(r[i + 2])
            trips += 1
            i += 3
        else:
            break
    return rb, i, trips

def decode_config_stream(m, r, base_offset):
    """Walk a generic register-write / read-triplet stream.
    Convention:
      - 'AE AF 80' triplet             -> RD_WORD (SROM_Data_L, SROM_Data_H) + filler
      - byte < 0x80 at current pos     -> register address, data byte follows
      - byte == 0x80 alone             -> loader state marker (arm/NOP)
      - otherwise                      -> unexpected; noted literally
    """
    lines = []
    i = 0
    while i < len(m):
        b = m[i]
        # AE AF 80 read-word triplet
        if b == 0xAE and i + 2 < len(m) and m[i + 1] == 0xAF and m[i + 2] == 0x80:
            lo = r[i + 1] if i + 1 < len(r) else 0
            hi = r[i + 2] if i + 2 < len(r) else 0
            lines.append(
                f"    @+{base_offset + i:04X}  RD_WORD  SROM_Data_L,SROM_Data_H "
                f"-> MISO lo={lo:02X} hi={hi:02X}"
            )
            i += 3
            continue
        if b == 0x80:
            lines.append(f"    @+{base_offset + i:04X}  LOADER_MARK  80")
            i += 1
        elif b < 0x80 and i + 1 < len(m):
            v = m[i + 1]
            lines.append(
                f"    @+{base_offset + i:04X}  WR  {b:02X} ({reg_name(b):15s}) = {v:02X}"
            )
            i += 2
        elif b >= 0x80:
            # lone read address (single byte, MISO returned on next clock byte)
            # The returned value is visible on r[i+1] if next byte was a filler.
            # But here we treat 'b' standalone as RD_REG.
            addr = b & 0x7F
            nxt = m[i + 1] if i + 1 < len(m) else None
            miso_next = r[i + 1] if i + 1 < len(r) else 0
            # If next byte is ALSO a read address and not 0x80, treat b as a
            # single 1-byte read (MISO captured on next byte's clock).
            lines.append(
                f"    @+{base_offset + i:04X}  RD  {addr:02X} ({reg_name(addr):15s}) "
                f"-> next MISO={miso_next:02X}"
            )
            i += 1
        else:
            lines.append(f"    @+{base_offset + i:04X}  ???  {b:02X}")
            i += 1
    return lines

def decode_init_tx(tx_m, tx_r, idx, full_srom=False):
    """Decode the monolithic init transaction."""
    m = [v for _, v in tx_m]
    r = [v for _, v in tx_r]
    ts = tx_m[0][0]
    d = DecodedTx(idx=idx, ts=ts, size=len(m), kind='init')

    d.lines.append(f"=== INIT transaction @ {ts:.6f}s  size={len(m)} bytes ===")

    # Preamble [0..11]
    d.lines.append("--- Phase A: Preamble [0..11] ---")
    d.lines.extend(decode_preamble(m[:12]))

    # SROM upload [12..]
    upload, up_end = walk_srom_upload(m, 12)
    d.lines.append(f"--- Phase B: SROM upload [12..{up_end - 1}] — "
                   f"{up_end - 12} bus bytes = {len(upload)} SROM bytes ---")
    d.lines.append(f"    pattern: {(up_end - 12) // 4}× (WR SROM_Data_L, WR SROM_Data_H)")
    d.lines.append(f"    SROM first 16: {bytes(upload[:16]).hex()}")
    d.lines.append(f"    SROM last  16: {bytes(upload[-16:]).hex()}")
    if full_srom:
        for off in range(0, len(upload), 32):
            chunk = bytes(upload[off:off + 32])
            d.lines.append(f"      {off:04X}: {chunk.hex()}")

    # Config between upload and readback
    rb_search_start = up_end
    # find first AE AF 80 triplet
    rb_start = None
    for j in range(up_end, len(m) - 2):
        if m[j] == 0xAE and m[j + 1] == 0xAF and m[j + 2] == 0x80:
            rb_start = j
            break
    if rb_start is not None:
        cfg_between = m[up_end:rb_start]
        d.lines.append(f"--- Phase C: Config between upload & readback "
                       f"[{up_end}..{rb_start - 1}] — {len(cfg_between)} bytes ---")
        d.lines.extend(decode_config_stream(cfg_between, r[up_end:rb_start], up_end))

        readback, rb_end, trips = walk_readback(m, r, rb_start)
        d.lines.append(f"--- Phase D: SROM readback [{rb_start}..{rb_end - 1}] — "
                       f"{trips}× (RD SROM_Data_L, RD SROM_Data_H, filler 80) ---")
        d.lines.append(f"    readback first 16 (MISO): {bytes(readback[:16]).hex()}")
        d.lines.append(f"    readback last  16 (MISO): {bytes(readback[-16:]).hex()}")

        # Phase-aligned comparison: upload[i] should equal readback[i+2]
        if len(upload) > 0 and len(readback) >= len(upload) + 2:
            L = len(upload)
            match = sum(1 for k in range(L) if upload[k] == readback[k + 2])
            d.lines.append(f"    integrity: upload[0..{L}) vs readback[2..{L + 2}): "
                           f"{match}/{L} match" + (" ★" if match == L else ""))

        cfg_after = m[rb_end:]
        d.lines.append(f"--- Phase E: Post-readback config "
                       f"[{rb_end}..{len(m) - 1}] — {len(cfg_after)} bytes ---")
        d.lines.extend(decode_config_stream(cfg_after, r[rb_end:], rb_end))
    else:
        d.lines.append(f"; no AE/AF/80 triplet found after SROM upload")
    return d

def decode_poll(tx_m, tx_r, idx):
    m = [v for _, v in tx_m]
    r = [v for _, v in tx_r]
    ts = tx_m[0][0]
    # 5-byte poll
    b0, b1, b2, b3, b4 = r[0], r[1], r[2], r[3], r[4]
    x = sign12(b2 | ((b4 & 0xF0) << 4))
    y = sign12(b3 | ((b4 & 0x0F) << 8))
    kind = 'poll' if len(m) == 5 else 'poll+tweak'
    d = DecodedTx(idx=idx, ts=ts, size=len(m), kind=kind)
    moving = (x != 0 or y != 0)
    tag = "MOT" if moving else "   "
    d.lines.append(
        f"[{tag}] poll @ {ts:.6f}s  status=0x{b1:02X}  "
        f"Δx={x:+5d}  Δy={y:+5d}   (raw MISO={bytes(r).hex()})"
    )
    # tweak tail (len 9): first 5 are poll, then WR pairs
    if len(m) == 9 and tuple(m[:5]) == POLL_PATTERN:
        d.lines.append(
            f"      +tweak: WR {m[5]:02X} ({reg_name(m[5])}) = {m[6]:02X}; "
            f"WR {m[7]:02X} ({reg_name(m[7])}) = {m[8]:02X}"
        )
    return d

def decode_generic_tx(tx_m, tx_r, idx):
    m = [v for _, v in tx_m]
    r = [v for _, v in tx_r]
    ts = tx_m[0][0]
    d = DecodedTx(idx=idx, ts=ts, size=len(m), kind='generic')
    if len(m) == 2:
        a, v = m
        if a & 0x80:
            d.lines.append(
                f"@ {ts:.6f}s  RD  {a & 0x7F:02X} ({reg_name(a)})  -> "
                f"MISO={r[1]:02X}"
            )
        else:
            d.lines.append(
                f"@ {ts:.6f}s  WR  {a:02X} ({reg_name(a)}) = {v:02X}"
            )
        return d
    # burst
    first = m[0]
    mode = 'RD' if (first & 0x80) else 'WR'
    d.lines.append(
        f"@ {ts:.6f}s  {mode}-burst len={len(m)}  "
        f"MOSI={bytes(m).hex()}  MISO={bytes(r).hex()}"
    )
    return d

# --------------------------------------------------------------------------
# Top-level
# --------------------------------------------------------------------------

def disassemble(path, out, full_srom=False, max_polls=None, gap_us=50.0,
                skip_idle=False):
    nch = detect_channels(path)
    has_cs = nch >= 4
    samples = parse_csv(path, has_cs)
    if has_cs:
        txs = extract_tx_by_cs(samples)
        seg_mode = "CS-framed"
    else:
        txs = extract_tx_by_gap(samples, gap_us)
        seg_mode = f"gap-grouped ({gap_us} µs)"

    # MISO bytes reuse same stream — a byte triple already has both.
    # Split each tx into two parallel lists (timestamps paired with mosi, miso).
    txs_m = []
    txs_r = []
    for tx in txs:
        txs_m.append([(t, mo) for (t, mo, mi) in tx])
        txs_r.append([(t, mi) for (t, mo, mi) in tx])

    # Header
    print(f'; "1855" sensor SPI disassembly (PixArt-family protocol)', file=out)
    print(f"; source: {path}", file=out)
    print(f"; segmentation: {seg_mode}", file=out)
    print(f"; total transactions: {len(txs_m)}", file=out)
    size_hist = Counter(len(t) for t in txs_m)
    print(f"; size histogram (top 10): {size_hist.most_common(10)}", file=out)
    print("", file=out)

    poll_count = 0
    motion_count = 0
    idle_count = 0
    skipped = 0
    for idx, (tx_m, tx_r) in enumerate(zip(txs_m, txs_r)):
        m = [v for _, v in tx_m]
        if len(m) >= 1000:
            d = decode_init_tx(tx_m, tx_r, idx, full_srom=full_srom)
        elif (len(m) == 5 and tuple(m) == POLL_PATTERN) or \
             (len(m) == 9 and tuple(m[:5]) == POLL_PATTERN):
            poll_count += 1
            # Check motion here to allow skip-idle filtering
            r = [v for _, v in tx_r]
            b2, b3, b4 = r[2], r[3], r[4]
            x = sign12(b2 | ((b4 & 0xF0) << 4))
            y = sign12(b3 | ((b4 & 0x0F) << 8))
            is_motion = (x != 0 or y != 0) or (len(m) == 9)
            if is_motion:
                motion_count += 1
            else:
                idle_count += 1
            if skip_idle and not is_motion:
                skipped += 1
                continue
            if max_polls is not None and poll_count > max_polls:
                skipped += 1
                continue
            d = decode_poll(tx_m, tx_r, idx)
        else:
            d = decode_generic_tx(tx_m, tx_r, idx)

        print(f"tx[{idx:5d}] ({d.kind})", file=out)
        for line in d.lines:
            print(line, file=out)
        print("", file=out)

    print(f"; summary: {poll_count} polls total "
          f"({motion_count} with motion, {idle_count} idle), "
          f"{skipped} skipped in output", file=out)

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", help="Saleae CSV export (Time, SCK, MOSI, MISO, ...)")
    ap.add_argument("-o", "--output", help="output file (default: stdout)")
    ap.add_argument("--full-srom", action="store_true",
                    help="dump full SROM contents in hex inside Phase B")
    ap.add_argument("--max-polls", type=int, default=200,
                    help="max motion polls to print (default: 200; set to -1 for all)")
    ap.add_argument("--gap-us", type=float, default=50.0,
                    help="inter-byte gap threshold in µs (default: 50)")
    ap.add_argument("--skip-idle", action="store_true",
                    help="suppress idle polls (dx=0 and dy=0) in output")
    args = ap.parse_args()

    mp = None if args.max_polls < 0 else args.max_polls
    out = open(args.output, "w") if args.output else sys.stdout
    try:
        disassemble(args.capture, out,
                    full_srom=args.full_srom,
                    max_polls=mp,
                    gap_us=args.gap_us,
                    skip_idle=args.skip_idle)
    finally:
        if args.output:
            out.close()

if __name__ == "__main__":
    main()
