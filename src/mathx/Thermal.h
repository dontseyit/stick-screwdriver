#pragma once
// -----------------------------------------------------------------------------
//  Thermal - what the dies are doing, and how much of it the device did itself.
//
//  This board has no thermometer. It has dies that know how hot they are, the
//  ESP32-S3 and the BMI270, each reporting its own junction temperature: the
//  air plus whatever the board is dissipating into it.
//
//  Measuring the DISAGREEMENT between the dies does not work. The argument is
//  sound, a board dissipating nothing would have them at the same temperature,
//  but the premise is false here: the SoC and the BMI270 sit a couple of
//  centimetres apart on one small PCB with ground planes joining them, so they
//  rise together and the gap barely moves. Two coupled sensors cannot measure
//  the heat that lifts them both.
//
//  Subtracting a modelled self-heating does not work either. `ambient = die -
//  rise`, with the rise learned from the climb after a cold start, is the right
//  shape and is defeated by the size of the number. Idle, the dies settle
//  around 70 C and 60 C: a self-heating of some FORTY degrees. With the
//  plateau capped at twenty-five, the device measured forty, was forbidden to
//  believe it, subtracted twenty-five, and reported forty degrees for a room at
//  twenty-five, at high confidence.
//
//  Raising the ceiling fixes the arithmetic, not the problem. A correction of
//  forty degrees on a quantity wanted to within one is not a correction: it IS
//  the measurement. The rise moves with the backlight, the CPU, the radio, and
//  whether the charger is working, which on this device it is, all night, for
//  the noise logger. So there is no ambient estimate here. The dies are shown
//  as dies.
//
//  What is honest, and is kept:
//
//  THE SELF-HEATING. Switched on after a spell in a drawer, every die IS the
//  room: nothing has been dissipated yet, so the reading is the answer rather
//  than an estimate of it. The climb from there to the plateau is the board's
//  own contribution, directly observed, and the device runs that experiment
//  every time somebody switches it on cold. MOOD wants it: warmth a device made
//  while being used is a different fact from warmth it was handed.
//
//  COLD READINGS. Self-heating only goes one way, so a die reading is an UPPER
//  bound on the room and never a lower one. A die at 5 C proves a cold room; a
//  die at 65 C proves nothing. That asymmetry is why the appraiser's cold rules
//  are sound while the warm ones are stated in the units actually measured.
//
//  Hardware-free; unit-tested on the host.
// -----------------------------------------------------------------------------
#include <cstdint>

namespace sd {

enum class TempSource : uint8_t { Soc, Imu, Pmic, Count };
constexpr int kTempSourceCount = static_cast<int>(TempSource::Count);

const char* tempSourceName(TempSource s);
/// What the part is, for a screen that has room to say it.
const char* tempSourcePart(TempSource s);

/// One set of die temperatures, in Celsius. A source that did not answer is
/// left invalid rather than zeroed: zero is a temperature.
struct ThermalReading {
    float c[kTempSourceCount]{};
    bool  ok[kTempSourceCount]{};

    void set(TempSource s, float celsius);

    int   valid() const;
    float coolest() const;
    float hottest() const;
    /// Mean of the sources that answered. The dies rise together on this board,
    /// so which one is asked matters less than averaging out what each part's
    /// own accuracy is worth, a degree or two, independently.
    float mean() const;
    /// Kept for the screen. It drives nothing, but a spread that suddenly opens
    /// up is worth being able to see.
    float spread() const;
};

// -- the overheat guard -------------------------------------------------------
//  This device has no cooling and no way to shed heat except stopping. Nothing
//  in normal use comes near these numbers; the guard is for a fault, a charger
//  working inside a closed bag, or a sun-facing dashboard.

/// Where the guard fires. The S3 is rated to 105 C junction and this board
/// idles near 70, so 90 clears ordinary use with room left below the rating.
constexpr float kOverheatC = 90.0f;

/// Consecutive over-limit readings, one a second, before it fires. A die this
/// hot is heated by a board that cannot cool in three seconds, so a single
/// sample over the line is the sensor and not the temperature.
constexpr uint8_t kOverheatSamples = 3;

/// Counts consecutive over-limit readings and returns true once `streak`
/// reaches kOverheatSamples. `valid` false leaves the streak where it is: a
/// sensor that stopped answering is not evidence of a cool board either.
bool overheatTrip(float hottestC, bool valid, float limitC, uint8_t& streak);

/// Watches the board go from cold to warm and measures what that cost it. What
/// is left of an earlier thermometer: the part that was a measurement rather
/// than an inference. See the header.
class ThermalModel {
public:
    /// How high the dies sit above the room once warmed through, before the
    /// device has measured its own. Replaced by the first cold start that runs
    /// long enough to level off.
    static constexpr float kDefaultPlateauC = 7.0f;

    /// The ceiling on that. Forty from measurements on this board, plus half as
    /// much again, because a ceiling guards against a corrupt stored value
    /// rather than stating a belief about the hardware. The real bound is
    /// riseC()'s: the model can never subtract heat it did not watch arrive.
    static constexpr float kMaxPlateauC = 60.0f;

    /// Time constant of the warm-up. Fixed rather than fitted: the plateau is
    /// what the answer is sensitive to, and estimating both from one noisy
    /// curve trades a solid number for two shaky ones.
    static constexpr float kWarmupTauSec = 240.0f;

    /// How far the dies must climb before a session counts as having started
    /// cold. Below this the climb could be the room, or a part-cooled board.
    static constexpr float kColdRiseC = 2.5f;

    /// How long a possibly-cold session gets to prove itself before it is
    /// concluded to have started warm. A cold board is five degrees up inside
    /// this, so a board that has not moved was not cold.
    static constexpr uint32_t kColdVerdictSec = 300;

    /// The climb is over when it slows to this, in degrees per minute, having
    /// run at least this long.
    static constexpr float    kPlateauRateCPerMin = 0.12f;
    static constexpr uint32_t kPlateauMinSec      = 600;

    /// How much of each measured plateau is taken. Sessions differ, by screen,
    /// radio and charger, so one is evidence rather than the answer.
    static constexpr float kPlateauBlend = 0.35f;

    void reset();

    /// Called once at start-up. `mayBeCold` is false when the restart itself
    /// says the board was already running (a panic, a watchdog, a reflash),
    /// where no amount of thermal evidence should be believed.
    void beginSession(bool mayBeCold);

    void update(const ThermalReading& r, float dt, uint32_t atSec);

    bool  known() const { return _have; }
    /// The die temperature everything here is built from: the mean of the parts
    /// that answered. A junction temperature, and nothing else.
    float dieC() const { return _die; }
    /// Self-heating the model currently believes is in that number.
    float riseC() const;
    /// The most the dies have risen since the anchor. It bounds the answer: the
    /// device cannot have heated itself more than they have ever gone up.
    float observedClimbC() const { return _maxClimb; }

    /// The plateau in force, and whether it was measured or assumed.
    float plateauC() const { return _plateau; }
    bool  plateauLearned() const { return _learned; }
    void  setPlateau(float c, bool learned);

    /// Whether this session proved itself to have started cold.
    bool anchored() const { return _anchored; }
    /// And what the dies read at that moment. On an anchored session that was
    /// the room: the one instant this device measures it rather than guessing.
    float anchorC() const { return _anchorC; }

private:
    void learnPlateau(uint32_t atSec);

    float _die     = 0.0f;
    float _rise    = 0.0f;
    float _plateau = kDefaultPlateauC;
    bool  _learned = false;
    bool  _have    = false;

    // -- this session --------------------------------------------------------
    bool     _mayBeCold  = true;
    bool     _anchored   = false;
    float    _anchorC    = 0.0f;
    float    _sinceAnchor = 0.0f;
    bool     _started    = false;

    /// Slow-moving copy of the die temperature, for deciding when the climb has
    /// stopped without reading the answer off one noisy sample.
    float _maxClimb  = 0.0f;
    float _slowDie   = 0.0f;
    float _ratePerMin = 0.0f;
    bool  _plateauDone = false;
};

}  // namespace sd
