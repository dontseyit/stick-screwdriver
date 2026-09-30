// -----------------------------------------------------------------------------
//  Host tests for the capability rules.  pio test -e native
//
//  These are the board's hostilities written down: which peripherals imply
//  another, and which cannot be powered at the same time. Every request passes
//  through expandImplied() before anything is switched on, so a mistake here is
//  not a wrong answer on a screen, it is a microphone that records silence or
//  an infrared receiver that hears nothing while the amplifier hums beside it.
//
//  That is why they are worth testing and why they were not tested: the rules
//  are pure bit arithmetic, but they lived in ServiceHub.cpp, which cannot be
//  compiled without an ESP32.
//
//  Every failure mode below is silent. Nothing throws, nothing logs, and the
//  device goes on looking like it is working.
// -----------------------------------------------------------------------------
#include <unity.h>

#include <initializer_list>

#include "core/Caps.h"

using namespace sd;

namespace {
constexpr bool kLibraryOff = false;
constexpr bool kLibraryOn  = true;

CapMask want(CapMask m) { return expandImplied(m, kLibraryOff); }
}  // namespace

// -----------------------------------------------------------------------------
//  What a request drags up with it
// -----------------------------------------------------------------------------
void test_infrared_brings_the_five_volt_rail_with_it(void) {
    // The IR silicon sits on EXT_5V, which M5Unified leaves off. An app that
    // asked only for IR and got only IR would transmit nothing.
    TEST_ASSERT_TRUE(has(want(caps(Cap::IrTx)), Cap::Ext5V));
    TEST_ASSERT_TRUE(has(want(caps(Cap::IrRx)), Cap::Ext5V));
}

void test_the_port_brings_the_rail_up_too(void) {
    // PORT.A's 5V pin feeds whatever somebody plugged into it.
    TEST_ASSERT_TRUE(has(want(caps(Cap::PortA)), Cap::Ext5V));
}

void test_nothing_else_asks_for_the_rail(void) {
    // The rail is not free - it is why this is a request and not a default.
    TEST_ASSERT_FALSE(has(want(caps(Cap::Display)), Cap::Ext5V));
    TEST_ASSERT_FALSE(has(want(caps(Cap::Imu)), Cap::Ext5V));
    TEST_ASSERT_FALSE(has(want(Cap::Mic | Cap::WiFi), Cap::Ext5V));
}

// -----------------------------------------------------------------------------
//  What cannot be on at the same time as what
// -----------------------------------------------------------------------------
void test_the_receiver_wins_over_the_amplifier(void) {
    // The IR receiver does not work while the speaker amp is powered. Dropped
    // rather than allowed to half-work.
    const CapMask m = want(Cap::IrRx | Cap::Speaker);
    TEST_ASSERT_TRUE(has(m, Cap::IrRx));
    TEST_ASSERT_FALSE(has(m, Cap::Speaker));
}

void test_transmitting_does_not_cost_the_speaker(void) {
    // Only the RECEIVER conflicts. Striking the speaker for IrTx as well would
    // silence an app that had every right to both.
    TEST_ASSERT_TRUE(has(want(Cap::IrTx | Cap::Speaker), Cap::Speaker));
}

void test_listening_wins_over_playing(void) {
    // Mic and speaker share BCLK, WS and MCLK, and the codec's two callbacks
    // disagree about the clock manager. An app that asked to record and
    // silently got nothing is the worse outcome, so the mic takes it.
    const CapMask m = want(Cap::Mic | Cap::Speaker);
    TEST_ASSERT_TRUE(has(m, Cap::Mic));
    TEST_ASSERT_FALSE(has(m, Cap::Speaker));
}

void test_wifi_takes_the_front_end_from_bluetooth(void) {
    // One 2.4 GHz front end. Both means the hardware time-slices and halves
    // each one's rate, exactly when a hunt needs it.
    const CapMask m = want(Cap::WiFi | Cap::Ble);
    TEST_ASSERT_TRUE(has(m, Cap::WiFi));
    TEST_ASSERT_FALSE(has(m, Cap::Ble));
}

void test_bluetooth_alone_is_left_alone(void) {
    TEST_ASSERT_TRUE(has(want(caps(Cap::Ble)), Cap::Ble));
}

// -----------------------------------------------------------------------------
//  The standing decision
// -----------------------------------------------------------------------------
void test_library_mode_outranks_whatever_an_app_asked_for(void) {
    // A standing decision rather than a per-request one, so it wins the way IR
    // does. Every request passes through here, which is what makes "no app can
    // forget to check" a fact rather than a hope.
    TEST_ASSERT_FALSE(
        has(expandImplied(caps(Cap::Speaker), kLibraryOn), Cap::Speaker));
    TEST_ASSERT_TRUE(
        has(expandImplied(caps(Cap::Speaker), kLibraryOff), Cap::Speaker));
}

void test_library_mode_silences_nothing_but_the_speaker(void) {
    const CapMask m = expandImplied(Cap::Speaker | Cap::Mic | Cap::Display,
                                    kLibraryOn);
    TEST_ASSERT_TRUE(has(m, Cap::Mic));
    TEST_ASSERT_TRUE(has(m, Cap::Display));
    TEST_ASSERT_FALSE(has(m, Cap::Speaker));
}

// -----------------------------------------------------------------------------
//  Properties of the whole rule set
// -----------------------------------------------------------------------------
void test_a_request_with_nothing_hostile_in_it_is_left_alone(void) {
    const CapMask asked = Cap::Display | Cap::Imu;
    TEST_ASSERT_EQUAL_UINT32(asked, want(asked));
}

void test_asking_for_nothing_gets_nothing(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, want(0u));
}

void test_expanding_an_expanded_mask_changes_nothing(void) {
    // The hub compares the expanded mask against what is already active, so a
    // rule that kept moving bits on a second pass would make every request look
    // like a change and tear working peripherals down to rebuild them.
    for (uint32_t bits = 0; bits < 1024u; ++bits) {
        for (bool lib : {false, true}) {
            const CapMask once  = expandImplied(bits, lib);
            const CapMask twice = expandImplied(once, lib);
            TEST_ASSERT_EQUAL_UINT32(once, twice);
        }
    }
}

void test_the_speaker_never_survives_anything_that_beats_it(void) {
    // Whatever else is asked for alongside, these three always win. Swept
    // rather than spot-checked, because the rules apply in sequence and an
    // order that let one undo another would show on some combinations only.
    for (uint32_t bits = 0; bits < 1024u; ++bits) {
        const CapMask asked = bits | caps(Cap::Speaker);
        for (bool lib : {false, true}) {
            const CapMask m = expandImplied(asked, lib);
            const bool beaten = lib || has(asked, Cap::Mic) || has(asked, Cap::IrRx);
            TEST_ASSERT_EQUAL(!beaten, has(m, Cap::Speaker));
        }
    }
}

// -----------------------------------------------------------------------------
int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_infrared_brings_the_five_volt_rail_with_it);
    RUN_TEST(test_the_port_brings_the_rail_up_too);
    RUN_TEST(test_nothing_else_asks_for_the_rail);

    RUN_TEST(test_the_receiver_wins_over_the_amplifier);
    RUN_TEST(test_transmitting_does_not_cost_the_speaker);
    RUN_TEST(test_listening_wins_over_playing);
    RUN_TEST(test_wifi_takes_the_front_end_from_bluetooth);
    RUN_TEST(test_bluetooth_alone_is_left_alone);

    RUN_TEST(test_library_mode_outranks_whatever_an_app_asked_for);
    RUN_TEST(test_library_mode_silences_nothing_but_the_speaker);

    RUN_TEST(test_a_request_with_nothing_hostile_in_it_is_left_alone);
    RUN_TEST(test_asking_for_nothing_gets_nothing);
    RUN_TEST(test_expanding_an_expanded_mask_changes_nothing);
    RUN_TEST(test_the_speaker_never_survives_anything_that_beats_it);

    return UNITY_END();
}
