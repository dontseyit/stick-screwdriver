#pragma once
// -----------------------------------------------------------------------------
//  NoteText - turning what a phone sent into something both ends can show.
//
//  A note is written in a browser and read on a 240x135 panel. The browser will
//  render anything; the panel has a bitmap font with 96 glyphs. So the text is
//  normalised ONCE, on arrival, and both ends show the same normalised thing.
//  Doing it on the way out would have the phone showing a note the device
//  cannot, turning every non-ASCII character into a silent disagreement.
//
//  The escaping is here rather than inline in the page because every note goes
//  back out inside an HTML document. A note containing a `<` is ordinary, "temp
//  < 5", and unescaped it eats the rest of the page including the form you
//  would fix it with. It also GROWS: one apostrophe becomes five bytes, and an
//  escape that fills its buffer mid-entity emits `&am`, which is worse than the
//  character it was protecting against. So the writer below stops on whole
//  entities only.
//
//  Hardware-free; unit-tested on the host.
// -----------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>

namespace sd {

/// The longest note kept. Generous for a panel this size, some forty
/// screenfuls at the smallest font, and small enough to hold in RAM whole.
constexpr size_t kNoteMaxBytes = 2048;

/// How much of the first line the list shows.
///
/// Twenty-six because the list draws titles in the 8 px Turkish font from x=10,
/// and 10 + 26*8 = 218, clear of the right margin and the scroll bar. A list
/// row does not clip.
constexpr size_t kNoteTitleLen = 26;

/// Placeholder for a byte the panel's font cannot draw. One per CHARACTER, not
/// per byte: a UTF-8 sequence is a single thing somebody typed, and `???` for
/// a three-byte glyph misreports how much was lost.
constexpr char kNoteUnprintable = '?';

/// Normalises in place, returning the new length. Idempotent.
///
/// `text` must have room for one byte past `len`: the result is shorter than
/// the input in every case but one, and that one is equality.
///
///  - CRLF and CR become LF, because a browser sends what its platform uses.
///  - Tabs become spaces; the panel's font has no tab stop.
///  - Other control characters are dropped.
///  - Anything outside ASCII becomes one kNoteUnprintable per character.
///  - Trailing whitespace on every line goes, and so do blank lines at either
///    end: a textarea contributes those by itself, and a blank first line costs
///    a quarter of the panel.
size_t noteSanitise(char* text, size_t len);

/// Whether a sanitised note has anything in it but whitespace.
bool noteHasContent(const char* text);

/// The first non-blank line, trimmed and cut to fit, with an ellipsis when it
/// had to be cut. Always NUL-terminates; writes "(blank)" for nothing at all,
/// because an empty list row looks like a drawing fault.
void noteTitle(const char* body, char* out, size_t cap);

/// The filename a note id is stored under, without any directory: eight digits
/// and ".txt". Needs 13 bytes.
void noteNameFor(uint32_t id, char* out, size_t cap);

/// The id `name` encodes, accepting a path or a bare name. False for anything
/// noteNameFor would not have produced.
///
/// Strict on purpose. `sscanf(base, "%8lu.txt", &id)` returns 1 for "7.bak",
/// since a trailing literal that fails to match does not reduce the count of
/// ASSIGNMENTS, so somebody else's file would be taken for a note, counted
/// against the space in use, and would push the next id past it.
bool noteIdFromName(const char* name, uint32_t& id);

/// How many bytes htmlEscape would write, excluding the terminator.
size_t htmlEscapedLen(const char* in);

/// Escapes `in` into `out` for both element text and quoted attributes.
///
/// Stops on a whole entity when `cap` runs out rather than emitting half of
/// one, and always NUL-terminates. Returns bytes written.
///
/// `stoppedAt`, when given, receives where in the INPUT it got to, which is
/// what makes this chunkable: a 2 KB note then needs no buffer at all. A caller
/// looping on it must treat no progress as the end, or a `cap` too small for
/// one entity spins.
size_t htmlEscape(const char* in, char* out, size_t cap,
                  const char** stoppedAt = nullptr);

}  // namespace sd
