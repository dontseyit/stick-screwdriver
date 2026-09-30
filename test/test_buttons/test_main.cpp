// -----------------------------------------------------------------------------
//  Host tests for the button grammar.  pio test -e native
//
//  The first three chord tests are the sequences that failed on the device for
//  a year: a chord pressed and released the way a hand presses one, each ending
//  with a stray Click or a Back. They are written as the exact event list,
//  because "one Menu and nothing else" is the whole claim.
// -----------------------------------------------------------------------------
#include <unity.h>

#include <string>
#include <vector>

#include "mathx/ButtonGrammar.h"

using namespace sd;

void setUp(void) {}
void tearDown(void) {}

namespace {

const char* typeName(EvType t) {
    switch (t) {
        case EvType::Click:       return "Click";
        case EvType::DoubleClick: return "Double";
        case EvType::LongPress:   return "Long";
        case EvType::Repeat:      return "Repeat";
        case EvType::Release:     return "Release";
        case EvType::Back:        return "Back";
        case EvType::Menu:        return "Menu";
        default:                  return "None";
    }
}

/// A finger script: at each time, which of A and B are down. Sampled every 5 ms
/// as the main loop does, with everything the grammar says collected as
/// "TypeBtn" tokens: "ClickA", "Back", "Menu".
struct Step { uint32_t atMs; bool a; bool b; };

std::string run(const std::vector<Step>& script, uint32_t untilMs) {
    ButtonGrammar g;
    std::string   out;
    size_t        i = 0;
    bool          a = false, b = false;
    for (uint32_t t = 0; t <= untilMs; t += 5) {
        while (i < script.size() && script[i].atMs <= t) { a = script[i].a; b = script[i].b; ++i; }
        g.poll(a, b, false, t);
        for (InputEvent ev = g.next(); ev; ev = g.next()) {
            if (!out.empty()) out += ' ';
            out += typeName(ev.type);
            if (ev.type != EvType::Menu && ev.type != EvType::Back) out += (ev.btn == Btn::A ? "A" : "B");
        }
    }
    return out;
}

}  // namespace

// -----------------------------------------------------------------------------
//  The chord
// -----------------------------------------------------------------------------
void test_chord_released_early_and_unevenly_is_one_menu(void) {
    // Both down together, A up at 300 ms, B lingers to 600 ms. This used to be
    // "ClickA Back": the click from the first finger and the app closing.
    const std::string got = run({{0, true, true}, {300, false, true}, {600, false, false}}, 800);
    TEST_ASSERT_EQUAL_STRING("Menu", got.c_str());
}

void test_quick_tap_chord_is_a_menu(void) {
    const std::string got = run({{0, true, true}, {80, false, false}}, 400);
    TEST_ASSERT_EQUAL_STRING("Menu", got.c_str());
}

void test_second_finger_landing_late_is_still_a_chord(void) {
    // A at 0, B at 400 - past the old 180 ms window but before A goes long.
    const std::string got = run({{0, true, false}, {400, true, true}, {700, false, false}}, 900);
    TEST_ASSERT_EQUAL_STRING("Menu", got.c_str());
}

void test_chord_held_long_stays_one_menu(void) {
    // Held two seconds: no LongPress, no Repeat, no Back, no Release.
    const std::string got = run({{0, true, true}, {2000, false, false}}, 2200);
    TEST_ASSERT_EQUAL_STRING("Menu", got.c_str());
}

void test_menu_arrives_the_moment_the_second_finger_lands(void) {
    ButtonGrammar g;
    g.poll(true, false, false, 0);
    TEST_ASSERT_FALSE(g.next());
    g.poll(true, true, false, 100);
    const InputEvent ev = g.next();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EvType::Menu), static_cast<int>(ev.type));
    TEST_ASSERT_EQUAL_UINT32(100, ev.heldMs);
}

void test_a_button_already_gone_long_is_not_a_chord(void) {
    // B held for Back, then A pressed while B is still down: Back has already
    // happened, and A is its own gesture.
    const std::string got = run({{0, false, true}, {700, true, true}, {800, false, true}, {900, false, false}}, 1100);
    TEST_ASSERT_EQUAL_STRING("Back ReleaseA ClickA ReleaseB", got.c_str());
}

void test_the_gesture_after_a_chord_is_ordinary(void) {
    const std::string got = run({{0, true, true}, {200, false, false},
                                 {400, true, false}, {500, false, false}}, 700);
    TEST_ASSERT_EQUAL_STRING("Menu ReleaseA ClickA", got.c_str());
}

void test_lifting_one_finger_and_repressing_does_not_refire(void) {
    // A stays down, B taps twice on top of it: one chord per gesture.
    const std::string got = run({{0, true, true}, {200, true, false}, {300, true, true},
                                 {400, true, false}, {500, false, false}}, 700);
    TEST_ASSERT_EQUAL_STRING("Menu", got.c_str());
}

// -----------------------------------------------------------------------------
//  Everything else, unchanged and pinned
// -----------------------------------------------------------------------------
void test_click(void) {
    TEST_ASSERT_EQUAL_STRING("ReleaseA ClickA", run({{0, true, false}, {100, false, false}}, 300).c_str());
}

void test_double_click(void) {
    const std::string got = run({{0, true, false}, {80, false, false}, {200, true, false}, {280, false, false}}, 500);
    TEST_ASSERT_EQUAL_STRING("ReleaseA ClickA ReleaseA ClickA DoubleA", got.c_str());
}

void test_b_held_is_back(void) {
    TEST_ASSERT_EQUAL_STRING("Back ReleaseB", run({{0, false, true}, {700, false, false}}, 900).c_str());
}

void test_a_held_is_long_then_repeats(void) {
    // Long at 550, first repeat 400 later, then every 110.
    const std::string got = run({{0, true, false}, {1200, false, false}}, 1400);
    TEST_ASSERT_EQUAL_STRING("LongA RepeatA RepeatA RepeatA ReleaseA", got.c_str());
}

void test_power_button_is_independent(void) {
    ButtonGrammar g;
    g.poll(true, true, true, 0);
    InputEvent ev = g.next();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EvType::Menu), static_cast<int>(ev.type));
    g.poll(false, false, false, 100);
    ev = g.next();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EvType::Release), static_cast<int>(ev.type));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Btn::Pwr), static_cast<int>(ev.btn));
    ev = g.next();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EvType::Click), static_cast<int>(ev.type));
    TEST_ASSERT_FALSE(g.next());
}

void test_flush_forgets_a_gesture_in_progress(void) {
    ButtonGrammar g;
    g.poll(true, false, false, 0);
    g.flush();
    TEST_ASSERT_FALSE(g.down(Btn::A));
    g.poll(false, false, false, 100);
    TEST_ASSERT_FALSE(g.next());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_chord_released_early_and_unevenly_is_one_menu);
    RUN_TEST(test_quick_tap_chord_is_a_menu);
    RUN_TEST(test_second_finger_landing_late_is_still_a_chord);
    RUN_TEST(test_chord_held_long_stays_one_menu);
    RUN_TEST(test_menu_arrives_the_moment_the_second_finger_lands);
    RUN_TEST(test_a_button_already_gone_long_is_not_a_chord);
    RUN_TEST(test_the_gesture_after_a_chord_is_ordinary);
    RUN_TEST(test_lifting_one_finger_and_repressing_does_not_refire);
    RUN_TEST(test_click);
    RUN_TEST(test_double_click);
    RUN_TEST(test_b_held_is_back);
    RUN_TEST(test_a_held_is_long_then_repeats);
    RUN_TEST(test_power_button_is_independent);
    RUN_TEST(test_flush_forgets_a_gesture_in_progress);
    return UNITY_END();
}
