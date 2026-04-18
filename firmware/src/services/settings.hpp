#pragma once

/*
 * Persistent settings store.
 *
 * Compile-time arena: every module declares a slot type with a unique
 * kId and a trivially-copyable Data struct, lists it in kSlots below,
 * and accesses it via Settings::get<Slot>(). The total size is checked
 * at compile time against the flash page budget, so slot bloat fails
 * the build instead of corrupting neighbours at runtime.
 *
 * On-flash layout (per page):
 *   [Header: magic 4B | version 2B | seq 2B | size 2B | _pad 2B | crc32 4B]
 *   [slot 0 bytes | slot 1 bytes | ... ]
 *
 * Two pages (A/B) are used as a simple log: whichever has the higher
 * seq and a valid crc wins. save() writes to the *other* page, bumps
 * seq, then the new page becomes authoritative on next boot. A
 * torn write is recovered from the previous page.
 *
 * Version mismatch or both pages invalid → defaults are loaded and the
 * store behaves as empty until the next save(). A firmware reflash
 * erases this region (it lives inside the DFU-writable range), which
 * is the intended migration path — bump the version on schema change
 * and users get a clean slate.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include "platform/flash.hpp"

namespace services {

/* ---- Slot declarations ---------------------------------------------- */

struct LightingSlot {
    static constexpr uint16_t kId = 0x0001;
    struct Data {
        uint8_t  mode;          // 0 = off, 1 = breathing (default)
        uint8_t  brightness;    // 0..255, applied as multiplier
        uint8_t  r1, g1, b1;    // per-LED base colours
        uint8_t  r2, g2, b2;
        uint8_t  r3, g3, b3;
        uint8_t  _reserved;
    };
    static constexpr Data kDefault{
        .mode = 1, .brightness = 255,
        .r1 = 255, .g1 = 0, .b1 = 0,
        .r2 = 0,   .g2 = 255, .b2 = 0,
        .r3 = 0,   .g3 = 0,   .b3 = 255,
        ._reserved = 0,
    };
};

struct DpiSlot {
    static constexpr uint16_t kId = 0x0002;
    struct Data {
        uint16_t cpi[5];        // up to 5 profile steps
        uint8_t  count;         // used entries
        uint8_t  active;        // index 0..count-1
        uint16_t _pad;
    };
    static constexpr Data kDefault{
        .cpi = { 800, 1600, 2400, 3200, 0 },
        .count = 4, .active = 1,
        ._pad = 0,
    };
};

/* ---- Arena plumbing ------------------------------------------------- */

constexpr uint32_t kMagic   = 0x53434647u; // "GFCS" little-endian
constexpr uint16_t kVersion = 0x0001u;
constexpr uint32_t kPageSize = platform::flash::kPageSize;

struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t seq;
    uint16_t size;      // payload bytes after header
    uint16_t _pad;
    uint32_t crc;       // CRC32 over [version..end of payload]
};
static_assert(sizeof(Header) == 16, "settings header must be 16 bytes");

namespace detail {

template <typename Target>
constexpr size_t offset_impl(size_t acc) { return acc; } // target not found → end

template <typename Target, typename Head, typename... Tail>
constexpr size_t offset_impl(size_t acc)
{
    return std::is_same<Target, Head>::value
               ? acc
               : offset_impl<Target, Tail...>(acc + sizeof(typename Head::Data));
}

} // namespace detail

template <typename... Slots>
class Arena {
public:
    static constexpr size_t kPayloadSize = (sizeof(typename Slots::Data) + ... + 0);
    static_assert(sizeof(Header) + kPayloadSize <= kPageSize,
                  "settings blob exceeds one flash page — add another page or shrink slots");

    template <typename S>
    static constexpr size_t offset_of() { return detail::offset_impl<S, Slots...>(0); }

    template <typename S>
    typename S::Data &get() {
        return *reinterpret_cast<typename S::Data *>(&payload_[offset_of<S>()]);
    }

    void load_defaults() { (load_one<Slots>(), ...); }

    uint8_t *payload() { return payload_; }
    const uint8_t *payload() const { return payload_; }

private:
    template <typename S>
    void load_one()
    {
        std::memcpy(&payload_[offset_of<S>()], &S::kDefault, sizeof(typename S::Data));
    }

    alignas(4) uint8_t payload_[kPayloadSize] {};
};

/* ---- Public API ----------------------------------------------------- */

class Settings {
public:
    using Store = Arena<LightingSlot, DpiSlot>;

    /* Call once at boot. Reads whichever page has the higher seq and
       a valid header+crc; falls back to defaults on mismatch. */
    static void load();

    /* Writes the current arena to the inactive page. Returns false on
       any flash error — the previously active page is left untouched. */
    static bool save();

    template <typename S>
    static typename S::Data &get() { return store_.get<S>(); }

    /* Exposed so header_valid() in the .cpp can verify on-flash blobs. */
    static uint32_t crc32(const uint8_t *data, size_t len);

private:
    static Store    store_;
    static uint32_t active_addr_;
    static uint16_t active_seq_;
};

} // namespace services
