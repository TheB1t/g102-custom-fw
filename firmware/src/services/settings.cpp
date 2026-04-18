#include <cstring>
#include "services/settings.hpp"

extern "C" {
    extern uint32_t _settings_page_a;
    extern uint32_t _settings_page_b;
    extern uint32_t _settings_page_sz;
}

namespace services {

Settings::Store Settings::store_{};
uint32_t        Settings::active_addr_ = 0;
uint16_t        Settings::active_seq_  = 0;

static uint32_t page_a() { return reinterpret_cast<uint32_t>(&_settings_page_a); }
static uint32_t page_b() { return reinterpret_cast<uint32_t>(&_settings_page_b); }

uint32_t Settings::crc32(const uint8_t *data, size_t len)
{
    /* Bit-reversed CRC-32 (IEEE 802.3), polynomial 0xEDB88320. Table-less
       to keep flash footprint small; settings blobs are <256 B so the
       per-byte loop is cheap. */
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k) {
            uint32_t m = -static_cast<int32_t>(c & 1u);
            c = (c >> 1) ^ (0xEDB88320u & m);
        }
    }
    return c ^ 0xFFFFFFFFu;
}

static bool header_valid(uint32_t addr, size_t payload_size)
{
    const Header *h = reinterpret_cast<const Header *>(addr);
    if (h->magic != kMagic) return false;
    if (h->version != kVersion) return false;
    if (h->size != payload_size) return false;

    /* CRC covers [version..end-of-payload], i.e. everything after magic
       and the trailing crc field. We lay it out so the crc sits right
       after the fixed header and before the payload isn't useful — keep
       it: crc is the last header field, payload follows immediately. */
    const uint8_t *body = reinterpret_cast<const uint8_t *>(addr) + offsetof(Header, version);
    size_t body_len = (sizeof(Header) - offsetof(Header, version) - sizeof(uint32_t))
                    + payload_size;
    return Settings::crc32(body, body_len) == h->crc;
}

void Settings::load()
{
    const Header *ha = reinterpret_cast<const Header *>(page_a());
    const Header *hb = reinterpret_cast<const Header *>(page_b());

    bool va = header_valid(page_a(), Store::kPayloadSize);
    bool vb = header_valid(page_b(), Store::kPayloadSize);

    const Header *winner = nullptr;
    uint32_t      winner_addr = 0;

    if (va && vb) {
        /* Treat seq as a wraparound-aware 16-bit counter. The newer one
           satisfies (newer - older) as a positive int16. */
        int16_t delta = static_cast<int16_t>(ha->seq - hb->seq);
        if (delta >= 0) { winner = ha; winner_addr = page_a(); }
        else            { winner = hb; winner_addr = page_b(); }
    } else if (va) { winner = ha; winner_addr = page_a(); }
      else if (vb) { winner = hb; winner_addr = page_b(); }

    if (winner) {
        const uint8_t *src = reinterpret_cast<const uint8_t *>(winner_addr) + sizeof(Header);
        std::memcpy(store_.payload(), src, Store::kPayloadSize);
        active_addr_ = winner_addr;
        active_seq_  = winner->seq;
    } else {
        store_.load_defaults();
        active_addr_ = 0;
        active_seq_  = 0;
    }
}

bool Settings::save()
{
    uint32_t target = (active_addr_ == page_a()) ? page_b() : page_a();

    if (!platform::flash::erase_page(target)) return false;

    Header h{};
    h.magic   = kMagic;
    h.version = kVersion;
    h.seq     = static_cast<uint16_t>(active_seq_ + 1);
    h.size    = Store::kPayloadSize;
    h._pad    = 0;

    /* Compute CRC over everything except the crc field itself. We stage
       the blob in RAM so the CRC input matches exactly what we'll write. */
    alignas(4) uint8_t blob[sizeof(Header) + Store::kPayloadSize];
    std::memcpy(blob, &h, sizeof(Header));
    std::memcpy(blob + sizeof(Header), store_.payload(), Store::kPayloadSize);

    const uint8_t *body = blob + offsetof(Header, version);
    size_t body_len = (sizeof(Header) - offsetof(Header, version) - sizeof(uint32_t))
                    + Store::kPayloadSize;
    h.crc = crc32(body, body_len);

    /* Rewrite header with real CRC before programming. */
    std::memcpy(blob, &h, sizeof(Header));

    if (!platform::flash::program(target, blob, sizeof(blob))) return false;

    active_addr_ = target;
    active_seq_  = h.seq;
    return true;
}

} // namespace services
