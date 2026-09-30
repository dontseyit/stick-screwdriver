#include "mathx/NoteText.h"

#include "mathx/NoteChars.h"

#include <cstring>

namespace sd {

namespace {

bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/// How many bytes the UTF-8 sequence starting at `lead` claims to occupy. Only
/// the claim: a truncated or malformed sequence is the caller's problem, and
/// the caller treats one byte as the floor so a bad lead cannot stall the walk.
int utf8Len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;  // a stray continuation byte, standing alone
}

/// The codepoint a sequence of `n` bytes at `p` encodes, or 0 for one this must
/// not accept.
///
/// `formed` separates the two ways that happens, and the caller needs them
/// apart to know how far to step:
///
///   structurally broken - a promised continuation byte that is not one. The
///   claim is already false, so only the lead byte belongs to it.
///
///   overlong - every byte in place, encoding a character that already has a
///   shorter spelling. The sequence IS n bytes and all of them go; accepting
///   two spellings of one character is how a filter gets walked past.
uint32_t utf8Decode(const char* p, int n, bool& formed) {
    formed = true;
    const unsigned char c0 = static_cast<unsigned char>(p[0]);
    if (n == 1) return c0 < 0x80 ? c0 : 0;

    uint32_t cp = c0 & (0x7Fu >> n);
    for (int i = 1; i < n; ++i) {
        const unsigned char ci = static_cast<unsigned char>(p[i]);
        if ((ci & 0xC0) != 0x80) { formed = false; return 0; }
        cp = (cp << 6) | (ci & 0x3Fu);
    }
    static const uint32_t kFloor[5] = {0, 0, 0x80, 0x800, 0x10000};
    return (cp >= kFloor[n]) ? cp : 0;
}

/// Whether the panel font has a drawing of this. The list is generated from the
/// same table that builds the font; see mathx/NoteChars.h.
bool drawable(uint32_t cp) {
    for (int i = 0; i < kNoteExtraCount; ++i) {
        if (kNoteExtraCp[i] == cp) return true;
        if (kNoteExtraCp[i] > cp) break;   // sorted
    }
    return false;
}

/// The escape for `c`, or nullptr when it can go through as itself.
const char* entityFor(char c) {
    switch (c) {
        case '&':  return "&amp;";
        case '<':  return "&lt;";
        case '>':  return "&gt;";
        case '"':  return "&quot;";
        // Numeric rather than &apos;, which HTML 4 never defined: a note read by
        // something older than HTML5 would show the entity as written.
        case '\'': return "&#39;";
        default:   return nullptr;
    }
}

}  // namespace

// -----------------------------------------------------------------------------
size_t noteSanitise(char* text, size_t len) {
    if (!text) return 0;

    size_t w = 0;
    for (size_t r = 0; r < len;) {
        const unsigned char c = static_cast<unsigned char>(text[r]);

        if (c == '\r') {
            // CR, or the CR of a CRLF. Either way one newline comes out, and
            // the LF that may follow is stepped over here rather than read as
            // itself and doubled.
            text[w++] = '\n';
            ++r;
            if (r < len && text[r] == '\n') ++r;
            continue;
        }
        if (c == '\n') { text[w++] = '\n'; ++r; continue; }
        if (c == '\t') { text[w++] = ' ';  ++r; continue; }
        if (c < 0x20 || c == 0x7F) { ++r; continue; }   // other controls
        if (c < 0x80) { text[w++] = static_cast<char>(c); ++r; continue; }

        // What the lead byte claims, clamped to what is actually there.
        size_t step = static_cast<size_t>(utf8Len(c));
        if (step > len - r) step = len - r;

        bool           formed = true;
        const uint32_t cp =
            (step > 1) ? utf8Decode(text + r, static_cast<int>(step), formed) : 0;

        if (cp && drawable(cp)) {
            // Kept as typed, so the phone and the panel show the same word.
            for (size_t i = 0; i < step; ++i) text[w++] = text[r + i];
            r += step;
            continue;
        }

        // One placeholder for the whole SEQUENCE, not one per byte: a
        // three-byte glyph is a single character somebody typed.
        //
        // But only for a sequence that WAS one. A lead byte followed by
        // something that is not a continuation consumes one byte, not the two
        // it promised: swallowing the next character on a claim already shown
        // false loses a letter that was never part of it.
        text[w++] = kNoteUnprintable;
        r += formed ? step : 1;
    }

    // Trailing spaces on each line, then trailing blank lines. A second pass,
    // because the first cannot know a space is trailing until it sees what
    // follows.
    size_t out = 0;
    for (size_t i = 0; i < w; ++i) {
        if (text[i] == '\n') {
            while (out > 0 && text[out - 1] == ' ') --out;
        }
        text[out++] = text[i];
    }
    while (out > 0 && isBlank(text[out - 1])) --out;

    // And leading blank lines, which a textarea contributes by itself and which
    // cost a quarter of the panel to display.
    size_t lead = 0;
    while (lead < out && isBlank(text[lead])) ++lead;
    if (lead) {
        out -= lead;
        std::memmove(text, text + lead, out);
    }

    text[out] = '\0';
    return out;
}

bool noteHasContent(const char* text) {
    if (!text) return false;
    for (const char* p = text; *p; ++p) {
        if (!isBlank(*p)) return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
void noteTitle(const char* body, char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!body) { std::strncpy(out, "(blank)", cap - 1); out[cap - 1] = '\0'; return; }

    // Past any blank lines: a note that opens with a newline still has a first
    // line, it is just further down.
    const char* p = body;
    while (*p && isBlank(*p)) ++p;
    if (!*p) { std::strncpy(out, "(blank)", cap - 1); out[cap - 1] = '\0'; return; }

    const char* end = p;
    while (*end && *end != '\n') ++end;
    while (end > p && isBlank(end[-1])) --end;

    size_t     want = static_cast<size_t>(end - p);
    const bool cut  = (want > kNoteTitleLen) || (want > cap - 1);
    if (want > kNoteTitleLen) want = kNoteTitleLen;
    if (want > cap - 1) want = cap - 1;

    // Room for the ellipsis comes out of the budget BEFORE the boundary check.
    // Writing it over the last three bytes afterwards picks three bytes by
    // arithmetic rather than by where the characters are, landing mid-sequence
    // half the time and leaving an orphaned lead byte before it.
    size_t take = want;
    if (cut && take >= 4) take -= 3;

    // Never mid-character: a title cut between the two bytes of a g-breve is a
    // broken glyph, and the byte that survives is not a letter.
    while (take > 0 && (static_cast<unsigned char>(p[take]) & 0xC0) == 0x80) --take;

    std::memcpy(out, p, take);
    out[take] = '\0';

    // `cut` means only that the TITLE was cut. A note with a second line is a
    // different fact, carried by the row's preview.
    if (cut && take > 0 && take + 3 <= cap - 1) std::memcpy(out + take, "...", 4);
}

// -----------------------------------------------------------------------------
namespace {
/// Zero-padded so the names sort the way the ids do, and fixed width so the
/// parse below can be a length check rather than a search.
constexpr int kNameDigits = 8;
}  // namespace

void noteNameFor(uint32_t id, char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (cap < static_cast<size_t>(kNameDigits) + 5) return;

    for (int i = kNameDigits - 1; i >= 0; --i) {
        out[i] = static_cast<char>('0' + (id % 10u));
        id /= 10u;
    }
    std::memcpy(out + kNameDigits, ".txt", 5);
}

bool noteIdFromName(const char* name, uint32_t& id) {
    if (!name) return false;
    const char* slash = std::strrchr(name, '/');
    const char* base  = slash ? slash + 1 : name;

    uint32_t v = 0;
    for (int i = 0; i < kNameDigits; ++i) {
        if (base[i] < '0' || base[i] > '9') return false;
        v = v * 10u + static_cast<uint32_t>(base[i] - '0');
    }
    if (std::strcmp(base + kNameDigits, ".txt") != 0) return false;
    // Ids start at one, so a zero is a name that looks right and is not.
    if (v == 0) return false;

    id = v;
    return true;
}

// -----------------------------------------------------------------------------
size_t htmlEscapedLen(const char* in) {
    if (!in) return 0;
    size_t n = 0;
    for (const char* p = in; *p; ++p) {
        const char* e = entityFor(*p);
        n += e ? std::strlen(e) : 1;
    }
    return n;
}

size_t htmlEscape(const char* in, char* out, size_t cap, const char** stoppedAt) {
    if (stoppedAt) *stoppedAt = in;
    if (!out || cap == 0) return 0;
    if (!in) { out[0] = '\0'; return 0; }

    size_t      w = 0;
    const char* p = in;
    for (; *p; ++p) {
        const char*  e    = entityFor(*p);
        const size_t need = e ? std::strlen(e) : 1;
        // Whole entities only. Half of one is not a shorter escape, it is a
        // different and worse character sequence.
        if (w + need + 1 > cap) break;
        if (e) {
            std::memcpy(out + w, e, need);
        } else {
            out[w] = *p;
        }
        w += need;
    }
    out[w] = '\0';
    if (stoppedAt) *stoppedAt = p;
    return w;
}

}  // namespace sd
