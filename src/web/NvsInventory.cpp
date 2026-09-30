// -----------------------------------------------------------------------------
//  NvsInventory - what is in the settings partition, without asking anybody.
//
//  NVS can describe itself. nvs_entry_find/next/info walk every key in the
//  partition and hand back its namespace, name and type, so a complete
//  inventory costs the services that wrote them nothing: no registry, no
//  per-app hook, nothing to keep in step when an app is added.
//
//  A scalar is self-describing, so a float comes out as a float: the
//  microphone's offset and the thermal plateau are readable as the numbers they
//  are. A blob is not. `mood/apps` is 776 bytes of a struct this file has never
//  heard of, and decoding it would mean putting per-record knowledge in the web
//  layer, which is the coupling this avoids. So blobs are reported as a type
//  and a size. A hex dump of 776 bytes is not a middle ground, it is noise.
//
//  That is enough for the question worth asking. This partition filled up once
//  and NOTHING said so: every app failed to save at the same moment, the radio
//  would not start because the WiFi driver could not store its own mode, and
//  the only trace was Arduino core logging nobody was reading. A list of
//  namespaces with their sizes finds that in seconds.
//
//  Nothing here writes. A calibration changed from a phone breaks a measurement
//  silently and leaves no record of when, and deleting a namespace over a
//  network is worse.
//
//  The access point's own key lives in this partition. It is a blob, so the
//  size-only rule already hides it, but that is luck rather than a decision, so
//  it is skipped by name as well.
// -----------------------------------------------------------------------------
#include "web/Console.h"

#include <nvs.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sd {
namespace {

/// One key, as the partition describes it.
struct NvsEntry {
    const char* ns    = "";
    const char* key   = "";
    nvs_type_t  type  = NVS_TYPE_ANY;
    /// Bytes on flash for a blob or string; zero for a scalar.
    uint32_t    bytes = 0;
    /// Exactly what is stored, in the type NVS says it is. Never a guess.
    const char* value = "";
    /// And what it plausibly MEANS, when something generic can be said; empty
    /// when nothing can.
    ///
    /// Offered beside the raw rather than instead of it, because both readings
    /// of an i32 are usually legitimate and picking one is where a diagnostic
    /// starts lying. Arduino's Preferences stores a float as an i32 of the same
    /// bits and a bool as an i8, so `95.0` and `1120403456` are the same four
    /// bytes and the partition does not know which was meant.
    const char* reading = "";
};

/// "blob", "float", "u32"... never a number the caller has to look up.
const char* nvsTypeName(nvs_type_t t);

/// Distinct namespaces, newest-found first, into `out`. Returns how many.
int nvsNamespaces(char out[][NVS_NS_NAME_MAX_SIZE], int max);

/// Every key in `ns`, in the order the partition holds them. `row` is called
/// once per key and nothing is buffered, so a page can stream it.
int nvsWalk(const char* ns, void (*row)(const NvsEntry&, void*), void* ctx);

struct NvsTotals {
    uint32_t used  = 0;
    uint32_t freeE = 0;
    uint32_t total = 0;
};
bool nvsTotals(NvsTotals& out);

constexpr const char* kPart = NVS_DEFAULT_PART_NAME;

/// The access point's own key. Hidden by name as well as by the size-only rule
/// for blobs, so it does not depend on that rule staying true.
///
/// The namespace is CONSOLE's, which is where the key has lived since raising
/// the network stopped being NOTES' job. This said "notes" for as long as it
/// took somebody to read it: the blob rule was hiding the key by itself, which
/// is exactly the luck the name was written down to avoid depending on.
constexpr const char* kHiddenNs  = "console";
constexpr const char* kHiddenKey = "apkey";

bool hidden(const char* ns, const char* key) {
    return std::strcmp(ns, kHiddenNs) == 0 && std::strcmp(key, kHiddenKey) == 0;
}

/// Whether these four bytes read as a float somebody plausibly stored.
///
/// The discrimination is better than it looks. A small integer reinterpreted as
/// a float lands in the denormals, 5 becoming 7e-45, and a float anybody meant
/// to keep does not. So the test is "would a float this size ever have been
/// written", which the tiny ones fail.
bool plausibleFloat(int32_t raw, float& out) {
    std::memcpy(&out, &raw, sizeof(out));
    if (!std::isfinite(out)) return false;
    if (out == 0.0f) return false;   // a stored zero is an integer zero already
    const float m = std::fabs(out);
    return m >= 1e-6f && m <= 1e9f;
}

/// Renders what is stored into `out`, and what it may mean into `say`. Either
/// can be left empty; neither is ever a substitute for the other.
void readValue(nvs_handle_t h, const nvs_entry_info_t& info, char* out, size_t cap,
               char* say, size_t sayCap, uint32_t& bytes) {
    out[0] = '\0';
    say[0] = '\0';
    bytes  = 0;

    switch (info.type) {
        // Preferences keeps a bool as a one-byte integer, so 0 and 1 carry a
        // second reading and nothing else does.
        case NVS_TYPE_U8:  {
            uint8_t v;
            if (nvs_get_u8(h, info.key, &v) == ESP_OK) {
                std::snprintf(out, cap, "%u", static_cast<unsigned>(v));
                if (v < 2) std::snprintf(say, sayCap, "%s as bool", v ? "true" : "false");
            }
            break;
        }
        case NVS_TYPE_I8:  {
            int8_t v;
            if (nvs_get_i8(h, info.key, &v) == ESP_OK) {
                std::snprintf(out, cap, "%d", static_cast<int>(v));
                if (v == 0 || v == 1) std::snprintf(say, sayCap, "%s as bool", v ? "true" : "false");
            }
            break;
        }
        case NVS_TYPE_U16: { uint16_t v; if (nvs_get_u16(h, info.key, &v) == ESP_OK) std::snprintf(out, cap, "%u", static_cast<unsigned>(v)); break; }
        case NVS_TYPE_I16: { int16_t  v; if (nvs_get_i16(h, info.key, &v) == ESP_OK) std::snprintf(out, cap, "%d", static_cast<int>(v)); break; }
        case NVS_TYPE_U32: { uint32_t v; if (nvs_get_u32(h, info.key, &v) == ESP_OK) std::snprintf(out, cap, "%lu", static_cast<unsigned long>(v)); break; }
        case NVS_TYPE_I32: {
            // The interesting one. Preferences writes a float as an i32 of the
            // same bits, so most of the calibration on this device is in here
            // wearing an integer's clothes. Both readings are shown and neither
            // asserted: the partition does not know which was meant either.
            int32_t v;
            if (nvs_get_i32(h, info.key, &v) == ESP_OK) {
                std::snprintf(out, cap, "%ld", static_cast<long>(v));
                float f;
                if (plausibleFloat(v, f)) {
                    std::snprintf(say, sayCap, "%.4g as float", static_cast<double>(f));
                }
            }
            break;
        }
        case NVS_TYPE_U64: { uint64_t v; if (nvs_get_u64(h, info.key, &v) == ESP_OK) std::snprintf(out, cap, "%llu", static_cast<unsigned long long>(v)); break; }
        case NVS_TYPE_I64: { int64_t  v; if (nvs_get_i64(h, info.key, &v) == ESP_OK) std::snprintf(out, cap, "%lld", static_cast<long long>(v)); break; }

        case NVS_TYPE_STR: {
            // Text needs no interpreting, so it goes in the raw column as
            // itself, truncated to what a row can hold rather than to what NVS
            // allows, which is far more.
            size_t len = 0;
            if (nvs_get_str(h, info.key, nullptr, &len) != ESP_OK) break;
            bytes = static_cast<uint32_t>(len);
            if (len > 0 && len <= cap) {
                size_t got = cap;
                if (nvs_get_str(h, info.key, out, &got) != ESP_OK) out[0] = '\0';
            } else if (len > cap) {
                std::snprintf(say, sayCap, "%lu characters", static_cast<unsigned long>(len - 1));
            }
            break;
        }
        case NVS_TYPE_BLOB: {
            size_t len = 0;
            if (nvs_get_blob(h, info.key, nullptr, &len) == ESP_OK) {
                bytes = static_cast<uint32_t>(len);
            }
            break;
        }
        default: break;
    }
}

// -----------------------------------------------------------------------------
const char* nvsTypeName(nvs_type_t t) {
    switch (t) {
        case NVS_TYPE_U8:   return "u8";
        case NVS_TYPE_I8:   return "i8";
        case NVS_TYPE_U16:  return "u16";
        case NVS_TYPE_I16:  return "i16";
        case NVS_TYPE_U32:  return "u32";
        case NVS_TYPE_I32:  return "i32";
        case NVS_TYPE_U64:  return "u64";
        case NVS_TYPE_I64:  return "i64";
        case NVS_TYPE_STR:  return "str";
        case NVS_TYPE_BLOB: return "blob";
        default:            return "?";
    }
}

int nvsNamespaces(char out[][NVS_NS_NAME_MAX_SIZE], int max) {
    if (!out || max <= 0) return 0;

    int             n  = 0;
    nvs_iterator_t  it = nullptr;
    esp_err_t       res = nvs_entry_find(kPart, nullptr, NVS_TYPE_ANY, &it);
    while (res == ESP_OK && it) {
        nvs_entry_info_t info{};
        nvs_entry_info(it, &info);

        // The partition does not promise to hand keys back grouped, so the
        // names are collected first and each namespace walked on its own, which
        // is also what makes the page readable.
        bool seen = false;
        for (int i = 0; i < n; ++i) {
            if (std::strcmp(out[i], info.namespace_name) == 0) { seen = true; break; }
        }
        if (!seen && n < max) {
            std::strncpy(out[n], info.namespace_name, NVS_NS_NAME_MAX_SIZE - 1);
            out[n][NVS_NS_NAME_MAX_SIZE - 1] = '\0';
            ++n;
        }
        res = nvs_entry_next(&it);
    }
    if (it) nvs_release_iterator(it);
    return n;
}

int nvsWalk(const char* ns, void (*row)(const NvsEntry&, void*), void* ctx) {
    if (!ns || !row) return 0;

    nvs_handle_t h = 0;
    // Read only, and it is the only mode this file ever opens with.
    const bool haveHandle = (nvs_open(ns, NVS_READONLY, &h) == ESP_OK);

    int            n   = 0;
    nvs_iterator_t it  = nullptr;
    esp_err_t      res = nvs_entry_find(kPart, ns, NVS_TYPE_ANY, &it);
    while (res == ESP_OK && it) {
        nvs_entry_info_t info{};
        nvs_entry_info(it, &info);

        if (!hidden(info.namespace_name, info.key)) {
            char     value[48] = {};
            char     say[32]   = {};
            uint32_t bytes     = 0;
            if (haveHandle) {
                readValue(h, info, value, sizeof(value), say, sizeof(say), bytes);
            }

            NvsEntry e;
            e.ns      = info.namespace_name;
            e.key     = info.key;
            e.type    = info.type;
            e.bytes   = bytes;
            e.value   = value;
            e.reading = say;
            row(e, ctx);
            ++n;
        }
        res = nvs_entry_next(&it);
    }
    if (it) nvs_release_iterator(it);
    if (haveHandle) nvs_close(h);
    return n;
}

bool nvsTotals(NvsTotals& out) {
    nvs_stats_t st{};
    if (nvs_get_stats(kPart, &st) != ESP_OK) return false;
    out.used  = static_cast<uint32_t>(st.used_entries);
    out.freeE = static_cast<uint32_t>(st.free_entries);
    out.total = static_cast<uint32_t>(st.total_entries);
    return true;
}

// -----------------------------------------------------------------------------
//  The page
// -----------------------------------------------------------------------------
void nvsPage(Page& p) {
    p.open();

    WebOut& out = p.out();
    out.put("<h1>STORED</h1>");

    NvsTotals  t;
    const bool haveTotals = nvsTotals(t);
    if (haveTotals && t.total) {
        out.putf("<p class=sub>%lu of %lu entries used &middot; %lu%% of the "
                 "settings partition</p>",
                 static_cast<unsigned long>(t.used),
                 static_cast<unsigned long>(t.total),
                 static_cast<unsigned long>(t.used * 100u / t.total));
    }

    // One namespace at a time. NVS does not promise to hand keys back grouped,
    // and a flat list in whatever order the pages hold them is a worse answer
    // to "what is taking the space" than no list at all.
    char      names[24][NVS_NS_NAME_MAX_SIZE];
    const int nsCount = nvsNamespaces(names, 24);

    for (int i = 0; i < nsCount && !out.gone(); ++i) {
        // The name is escaped although it came from flash: a key is somebody
        // else's string, and the page it lands in is not the place to find out
        // it was not. escaped() handles any length.
        out.put("<h2>");
        out.escaped(names[i]);
        out.put("</h2><table>");

        // Streamed a row at a time through the walker's callback, so nothing
        // here holds the inventory: a partition with a thousand keys costs the
        // same memory as one with three.
        nvsWalk(names[i], [](const NvsEntry& e, void* ctx) {
            auto* o = static_cast<WebOut*>(ctx);
            o->put("<tr><td class=k>");
            o->escaped(e.key);
            o->putf("</td><td class=t>%s</td><td class=v>",
                    nvsTypeName(e.type));

            if (e.value[0]) {
                o->escaped(e.value);
            } else if (e.bytes) {
                o->putf("%lu B", static_cast<unsigned long>(e.bytes));
            } else {
                o->put("&mdash;");
            }

            // The reading sits UNDER the raw rather than instead of it, in a
            // quieter colour, so which one the store holds is never in doubt.
            if (e.reading[0]) {
                o->put("<span class=r>");
                o->escaped(e.reading);
                o->put("</span>");
            }
            o->put("</td></tr>");
        }, &out);

        out.put("</table>");
    }
}

/// A third column, for the type between a key and its value. Only this page
/// has one: everywhere else a row is a label and a reading.
const char kNvsCss[] PROGMEM = "td.t{color:#4a525e;width:3.4em}";

/// What the inventory says about itself. It matters that this is on the page
/// rather than only in the source: somebody reading a list of sizes needs to
/// know it is a list of sizes and not a list of contents.
const char kNvsNote[] PROGMEM =
    "<p class=foot>Read only. A scalar is self-describing so its value is "
    "shown; a blob is a struct this page has never heard of, so it gets a size "
    "and no more &mdash; decoding it would mean putting every app's record "
    "layout in here. The access point key is skipped.</p>";

constexpr PageInfo kNvsPage{
    .path   = "/nvs",
    .method = Method::Get,
    .title  = "STORED",
    .nav    = "the raw store",
    .note   = kNvsNote,
    .css    = kNvsCss,
    .fn     = nvsPage,
};

SD_REGISTER_PAGE(NvsPage, kNvsPage)

}  // namespace
}  // namespace sd
