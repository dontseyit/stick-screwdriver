// -----------------------------------------------------------------------------
//  Host tests for the thermal model.  pio test -e native
//
//  The fixture is the point of this file. An earlier version built a synthetic
//  board that settled SEVEN degrees above the room and claimed in its own
//  comment that the figure was measured. It was not: this board, idle, settles
//  with the SoC near 70 C and the BMI270 near 60 C in a room at 25, a
//  self-heating of forty degrees.
//
//  Every constant in the model was chosen against that seven, and one of them,
//  a ceiling of 25 C on the plateau, made the answer wrong by fifteen degrees
//  on the hardware that exists. No test caught it, because the fixture and the
//  constant were built from the same wrong premise and agreed with each other.
//
//  So the numbers below are the measured ones, and the ceiling test at the
//  bottom exists so that a future ceiling set from taste fails here.
// -----------------------------------------------------------------------------
#include <unity.h>

#include <cmath>

#include "mathx/Thermal.h"

using namespace sd;

namespace {

/// How far above the room this board settles once warmed through. Measured:
/// dies at 70 C and 60 C with a room at 25 C.
constexpr float kTruePlateau = 40.0f;
/// And how long it takes to get there.
constexpr float kTrueTau = 240.0f;

/// Where each part sits relative to the mean. Not what the part is worth: a
/// real gradient across the board, the SoC burning the power and the BMI270
/// downstream of it. What matters is that it is FIXED, both dies rising
/// together, which is what killed the first model.
constexpr float kSocErr = +5.0f;
constexpr float kImuErr = -5.0f;

/// A board that has been running `sinceSec` in a room at `ambient`.
ThermalReading board(float ambient, float sinceSec) {
    const float rise = kTruePlateau * (1.0f - std::exp(-sinceSec / kTrueTau));
    ThermalReading r;
    r.set(TempSource::Soc, ambient + rise + kSocErr);
    r.set(TempSource::Imu, ambient + rise + kImuErr);
    return r;
}

/// Runs a cold start for `seconds`, one sample a second.
void runFromCold(ThermalModel& m, float ambient, int seconds, uint32_t& clock,
                 float startedSec = 0.0f) {
    for (int i = 0; i < seconds; ++i) {
        m.update(board(ambient, startedSec + static_cast<float>(i)), 1.0f, clock++);
    }
}

/// Runs a board that was already warmed through when the session began.
void runFromWarm(ThermalModel& m, float ambient, int seconds, uint32_t& clock) {
    for (int i = 0; i < seconds; ++i) {
        m.update(board(ambient, 100000.0f), 1.0f, clock++);
    }
}

}  // namespace

// -----------------------------------------------------------------------------
//  The reading itself
// -----------------------------------------------------------------------------
void test_a_source_that_did_not_answer_is_not_a_temperature(void) {
    ThermalReading r;
    TEST_ASSERT_EQUAL_INT(0, r.valid());
    r.set(TempSource::Soc, 40.0f);
    TEST_ASSERT_EQUAL_INT(1, r.valid());
    // Zero degrees is a temperature, so an unanswered source must not read as
    // one: it would drag every mean toward freezing.
    TEST_ASSERT_EQUAL_FLOAT(40.0f, r.mean());
    TEST_ASSERT_EQUAL_FLOAT(40.0f, r.coolest());
}

void test_a_nonsense_reading_is_refused(void) {
    ThermalReading r;
    r.set(TempSource::Soc, std::nanf(""));
    r.set(TempSource::Imu, -300.0f);
    r.set(TempSource::Pmic, 1000.0f);
    TEST_ASSERT_EQUAL_INT(0, r.valid());
}

void test_the_mean_averages_what_each_part_is_worth(void) {
    // Neither die is trusted over the other: the gradient between them is real
    // and nothing here knows which end to believe.
    const ThermalReading r = board(25.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, r.mean());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, r.spread());
}

// -----------------------------------------------------------------------------
//  Why the first model failed, kept as a test
// -----------------------------------------------------------------------------
void test_the_dies_rise_together_so_their_spread_measures_nothing(void) {
    // The premise the first model rested on, and why it could not work: forty
    // degrees of self-heating move the gap between the sensors not at all.
    const ThermalReading cold = board(25.0f, 0.0f);
    const ThermalReading warm = board(25.0f, 100000.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.05f, kTruePlateau, warm.mean() - cold.mean());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, cold.spread(), warm.spread());
}

// -----------------------------------------------------------------------------
//  The cold start, which is the one measurement here
// -----------------------------------------------------------------------------
void test_the_first_reading_of_a_cold_start_is_the_room(void) {
    ThermalModel m;
    uint32_t     clock = 0;
    m.beginSession(true);
    runFromCold(m, 25.0f, 1, clock);

    TEST_ASSERT_TRUE(m.known());
    // No longer offered as an ambient estimate, but still the one instant the
    // dies and the room are the same number, and what the plateau is measured
    // from, so it has to be kept exactly.
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 25.0f, m.anchorC());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.riseC());
}

void test_a_cold_start_proves_itself_by_climbing(void) {
    ThermalModel m;
    uint32_t     clock = 0;
    m.beginSession(true);
    runFromCold(m, 25.0f, 5, clock);
    TEST_ASSERT_FALSE(m.anchored());   // has not climbed far enough to prove it

    runFromCold(m, 25.0f, 600, clock, 5.0f);
    TEST_ASSERT_TRUE(m.anchored());
}

void test_a_restart_that_says_the_board_was_running_is_never_anchored(void) {
    // A panic or a reflash. The dies may climb afterwards, the board having
    // been hot and the restart cooling nothing, and that climb must not be read
    // as proof of a cold start.
    ThermalModel m;
    uint32_t     clock = 0;
    m.beginSession(false);
    runFromCold(m, 25.0f, 1800, clock);
    TEST_ASSERT_FALSE(m.anchored());
}

void test_a_session_that_never_climbs_concludes_it_started_warm(void) {
    // A battery pulled and put straight back: the restart says power-on, so it
    // might have been cold, but nothing climbs. After long enough that a cold
    // board would have moved five degrees, it stops pretending.
    ThermalModel m;
    uint32_t     clock = 0;
    m.setPlateau(kTruePlateau, true);
    m.beginSession(true);
    runFromWarm(m, 25.0f, ThermalModel::kColdVerdictSec + 120, clock);

    TEST_ASSERT_FALSE(m.anchored());
    TEST_ASSERT_FLOAT_WITHIN(0.8f, kTruePlateau, m.riseC());
}

// -----------------------------------------------------------------------------
//  Measuring its own self-heating
// -----------------------------------------------------------------------------
void test_it_measures_its_own_self_heating(void) {
    ThermalModel m;
    uint32_t     clock = 0;
    TEST_ASSERT_FALSE(m.plateauLearned());

    m.beginSession(true);
    runFromCold(m, 25.0f, 2400, clock);

    TEST_ASSERT_TRUE(m.plateauLearned());
    TEST_ASSERT_FLOAT_WITHIN(0.6f, kTruePlateau, m.plateauC());
}

void test_the_ceiling_does_not_clamp_the_board_it_is_meant_to_describe(void) {
    // The regression. A ceiling of 25 C turned a measured 40 into a stored 25,
    // and the device reported forty degrees for a room at twenty-five while
    // holding the right answer in observedClimbC() the whole time.
    //
    // Both halves are asserted deliberately: that the learned figure is the one
    // observed, AND that it is not against the ceiling. The second is what
    // fails if somebody sets kMaxPlateauC from taste again.
    ThermalModel m;
    uint32_t     clock = 0;
    m.beginSession(true);
    runFromCold(m, 25.0f, 2400, clock);

    TEST_ASSERT_FLOAT_WITHIN(0.6f, kTruePlateau, m.observedClimbC());
    TEST_ASSERT_FLOAT_WITHIN(0.6f, m.observedClimbC(), m.plateauC());
    TEST_ASSERT_TRUE(m.plateauC() < ThermalModel::kMaxPlateauC - 1.0f);
}

void test_a_learned_plateau_carries_a_warm_start(void) {
    // The payoff for MOOD: a device switched on already hot knows how much of
    // its own heat it carries, without watching it arrive again.
    ThermalModel m;
    uint32_t     clock = 0;
    m.beginSession(true);
    runFromCold(m, 25.0f, 2400, clock);
    const float learned = m.plateauC();

    ThermalModel warm;
    warm.setPlateau(learned, true);
    warm.beginSession(false);
    runFromWarm(warm, 25.0f, 60, clock);

    TEST_ASSERT_FLOAT_WITHIN(0.8f, kTruePlateau, warm.riseC());
}

void test_the_rise_is_small_early_and_large_later(void) {
    // What the appraiser asks of this: a board thirty seconds off a cold start
    // has made none of its own heat, and one an hour in has made all of it. A
    // number read straight off the plateau would say forty at both moments.
    ThermalModel m;
    uint32_t     clock = 0;
    m.setPlateau(kTruePlateau, true);
    m.beginSession(true);

    runFromCold(m, 25.0f, 30, clock);
    const float early = m.riseC();
    runFromCold(m, 25.0f, 3600, clock, 30.0f);
    const float late = m.riseC();

    TEST_ASSERT_TRUE(early < 6.0f);
    TEST_ASSERT_TRUE(late > kTruePlateau - 2.0f);
}

void test_the_rise_never_exceeds_what_the_dies_have_risen(void) {
    // Switched on cold and left idle with the screen dim: it warms far less
    // than the plateau, and claiming a self-heating it never earned would tell
    // the appraiser it had made forty degrees it had not.
    ThermalModel m;
    uint32_t     clock = 0;
    m.setPlateau(kTruePlateau, true);
    m.beginSession(true);
    for (int i = 0; i < 240; ++i) {
        // Anchored at the room, then half a degree of warming and no more: the
        // anchor is the first sample, so the climb has to happen after it.
        const float warmed = (i == 0) ? 0.0f : 0.5f;
        ThermalReading r;
        r.set(TempSource::Soc, 25.0f + kSocErr + warmed);
        r.set(TempSource::Imu, 25.0f + kImuErr + warmed);
        m.update(r, 1.0f, clock++);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, m.observedClimbC());
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, m.riseC());
}

// -----------------------------------------------------------------------------
//  Degrading rather than failing
// -----------------------------------------------------------------------------
void test_one_die_is_enough(void) {
    // Unlike the model this replaced, which needed two to have a spread at all.
    ThermalModel m;
    uint32_t     clock = 0;
    m.beginSession(true);
    for (int i = 0; i < 60; ++i) {
        ThermalReading r;
        r.set(TempSource::Soc, 25.0f);
        m.update(r, 1.0f, clock++);
    }
    TEST_ASSERT_TRUE(m.known());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, m.dieC());
}

void test_a_plateau_from_flash_is_still_clamped(void) {
    // The ceiling's actual job: a corrupt or absurd stored value, not a belief
    // about how warm a board is allowed to get.
    ThermalModel m;
    m.setPlateau(500.0f, true);
    TEST_ASSERT_EQUAL_FLOAT(ThermalModel::kMaxPlateauC, m.plateauC());
    m.setPlateau(-5.0f, true);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.plateauC());
}

void test_reset_forgets_what_it_learned(void) {
    ThermalModel m;
    uint32_t     clock = 0;
    m.beginSession(true);
    runFromCold(m, 25.0f, 2400, clock);
    TEST_ASSERT_TRUE(m.plateauLearned());

    m.reset();
    TEST_ASSERT_FALSE(m.known());
    TEST_ASSERT_FALSE(m.plateauLearned());
    TEST_ASSERT_FALSE(m.anchored());
    TEST_ASSERT_EQUAL_FLOAT(ThermalModel::kDefaultPlateauC, m.plateauC());
}

// -----------------------------------------------------------------------------
void setUp() {}
void tearDown() {}

// -- the overheat guard -------------------------------------------------------

void test_a_warm_board_never_trips() {
    uint8_t streak = 0;
    for (int i = 0; i < 100; ++i) {
        TEST_ASSERT_FALSE(overheatTrip(70.0f, true, kOverheatC, streak));
    }
}

void test_one_hot_sample_is_the_sensor_and_not_the_board() {
    uint8_t streak = 0;
    TEST_ASSERT_FALSE(overheatTrip(120.0f, true, kOverheatC, streak));
    TEST_ASSERT_FALSE(overheatTrip(70.0f, true, kOverheatC, streak));
    TEST_ASSERT_EQUAL_UINT8(0, streak);
}

void test_a_board_that_stays_over_the_limit_trips() {
    uint8_t streak = 0;
    for (int i = 1; i < kOverheatSamples; ++i) {
        TEST_ASSERT_FALSE(overheatTrip(96.0f, true, kOverheatC, streak));
    }
    TEST_ASSERT_TRUE(overheatTrip(96.0f, true, kOverheatC, streak));
}

void test_a_sensor_that_stopped_answering_neither_trips_nor_clears() {
    uint8_t streak = 0;
    overheatTrip(96.0f, true, kOverheatC, streak);
    TEST_ASSERT_FALSE(overheatTrip(0.0f, false, kOverheatC, streak));
    TEST_ASSERT_EQUAL_UINT8(1, streak);
}

/// The guard must sit above what this board does in ordinary use, or it
/// switches the device off for working.
void test_the_limit_clears_the_warm_board_by_a_wide_margin() {
    const ThermalReading warm = board(35.0f, 3600.0f);
    TEST_ASSERT_TRUE(warm.hottest() < kOverheatC - 10.0f);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_a_source_that_did_not_answer_is_not_a_temperature);
    RUN_TEST(test_a_nonsense_reading_is_refused);
    RUN_TEST(test_the_mean_averages_what_each_part_is_worth);

    RUN_TEST(test_the_dies_rise_together_so_their_spread_measures_nothing);

    RUN_TEST(test_the_first_reading_of_a_cold_start_is_the_room);
    RUN_TEST(test_a_cold_start_proves_itself_by_climbing);
    RUN_TEST(test_a_restart_that_says_the_board_was_running_is_never_anchored);
    RUN_TEST(test_a_session_that_never_climbs_concludes_it_started_warm);

    RUN_TEST(test_it_measures_its_own_self_heating);
    RUN_TEST(test_the_ceiling_does_not_clamp_the_board_it_is_meant_to_describe);
    RUN_TEST(test_a_learned_plateau_carries_a_warm_start);
    RUN_TEST(test_the_rise_is_small_early_and_large_later);
    RUN_TEST(test_the_rise_never_exceeds_what_the_dies_have_risen);

    RUN_TEST(test_one_die_is_enough);
    RUN_TEST(test_a_plateau_from_flash_is_still_clamped);
    RUN_TEST(test_reset_forgets_what_it_learned);

    RUN_TEST(test_a_warm_board_never_trips);
    RUN_TEST(test_one_hot_sample_is_the_sensor_and_not_the_board);
    RUN_TEST(test_a_board_that_stays_over_the_limit_trips);
    RUN_TEST(test_a_sensor_that_stopped_answering_neither_trips_nor_clears);
    RUN_TEST(test_the_limit_clears_the_warm_board_by_a_wide_margin);

    return UNITY_END();
}
