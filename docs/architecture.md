# Architecture

The device is a box of unrelated tools that must feel like one instrument. The
architecture exists to serve one property above all others:

> **Adding a tool must not require touching a tool that already works.**

Everything below follows from that.

---

## Layers

```
                  ┌───────────────────────────────────────────┐
   apps/          │ LEVEL   SYSTEM   (IR)   (RF)   (…)        │  one .cpp each
                  ├───────────────────────────────────────────┤
   core/          │ AppManager   AppRegistry   App   Events   │  lifecycle
                  ├───────────────────────────────────────────┤
   ui/            │ Canvas   Widgets   Theme                  │  one look
                  ├───────────────────────────────────────────┤
   services/      │ ServiceHub  Imu  Input  Power  Settings   │  owns hardware
                  ├───────────────────────────────────────────┤
   mathx/         │ Vec3  Thermal  ButtonGrammar  WifiQr      │  no hardware
                  ├───────────────────────────────────────────┤
                  │ M5Unified / M5GFX / Arduino-ESP32         │
                  └───────────────────────────────────────────┘
```

Dependencies point strictly downward. `mathx/` is the exception worth noting:
it includes no Arduino headers at all, which is what lets it be tested on the
host — see [Testing](#testing).

---

## 1. Apps register themselves

There is no central list of apps. Each app ends its `.cpp` with:

```cpp
SD_REGISTER_APP(MyApp, kInfo)
```

which constructs a `sd::AppRegistrar` at static-init time that pushes the app's
`AppInfo` and a factory into `AppRegistry`. The registry's storage lives inside
a function-local static, so it is constructed on first use and cannot fall
victim to static-initialisation ordering.

The consequence is the one that matters: **adding an app is a purely additive
change.** No shared enum, no switch statement, no launcher edit, no merge
conflict. The launcher renders whatever the registry holds, and an app that
names a `parent` is rendered by that parent instead - GAMES lists the games,
SYSTEM lists CALIBRATION - so a folder is just another app.

Apps are built by their factory on launch and destroyed on exit, so an app that
is not running costs nothing but its `AppInfo`. Anything that must survive lives
in `Settings`.

## 2. Capabilities arbitrate the hardware

The StickS3 has peripherals that are actively hostile to one another:

- the IR receiver **cannot work** while the speaker amplifier is powered;
- IR needs the `EXT_5V` rail that M5Unified deliberately switches **off** at boot;
- and one rule is a preference rather than a hazard: Library Mode, the
  device-wide silence switch on SYSTEM, is enforced in the same place, because
  a decision every tool must obey cannot be left to every tool.

Left to each app, that knowledge gets copied, drifts, and eventually an audio
app ships that silently breaks the IR app. So apps do not touch peripherals.
They *declare* what they need:

```cpp
.needs = Cap::Display | Cap::Imu,
```

`AppManager` hands that mask to `ServiceHub::applyCaps()` before `onStart()` and
clears it after `onStop()`. All the sequencing lives in one function:

```cpp
static CapMask expandImplied(CapMask want) {
    if (has(want, Cap::IrTx) || has(want, Cap::IrRx)) want |= Cap::Ext5V;
    if (has(want, Cap::PortA)) want |= Cap::Ext5V;
    if (has(want, Cap::IrRx)) want &= ~caps(Cap::Speaker);
    if (systemSettings().libraryMode()) want &= ~caps(Cap::Speaker);
    return want;
}
```

Striking the bit, rather than letting the speaker's `begin()` fail quietly, is
what keeps `_active` truthful: turning Library Mode off later is a change of
mask, so the next request really does start the speaker instead of finding it
already marked acquired.

Releases happen before acquisitions, so the conflicting peripheral is always
freed before its replacement is claimed. `EXT_5V` is refcounted in
`PowerService`, so two consumers cannot switch the rail out from under each
other, and it is sequenced explicitly rather than by bit position — power rails
come up before anything that sits on them and go down after, because an IR
receiver created before its supply arrives spends its first moments watching a
floating pin.

**Release what you did not acquire.** `applyCaps()` works on the difference
between the wanted set and `_active`, which quietly assumes `_active = 0` means
"all hardware is off". That is an assumption about state this code does not own,
and on this board it is wrong: the speaker amplifier is a latched bit in the
M5PM1 PMIC (register `0x11` bit 3), and the PMIC keeps its state across an ESP32
reset. One beep in a previous session leaves the amplifier enabled at the next
boot — and IR reception does not work at all while it is on, silently. So
`ServiceHub::begin()` drives the amplifier down explicitly to establish a
baseline, and acquiring `Cap::IrRx` asserts it again rather than trusting that
clearing a bit released anything. Any future peripheral whose state outlives a
reset needs the same treatment.

**A rail is a dependency, not a decoration.** `Cap::PortA` implies `Cap::Ext5V`
for the same reason IR does: PORT.A's red wire *is* the EXT_5V rail. Without the
implication the bus opens onto an unpowered module, the scan comes back empty,
and the app reports a wiring fault that does not exist. Whenever a capability's
hardware lives on a switched supply, the implication belongs in `expandImplied`
rather than in the app that noticed it first.

**To add a peripheral:** add a bit to `Cap`, add a case to
`ServiceHub::acquire`/`release`, and add any conflict rule to `expandImplied`.
Nothing else changes.

### Pages register themselves too

The web console has the same shape. `src/web/` owns the socket, the captive
portal and one stylesheet; a page is a function beside the feature whose data it
shows, ending in `SD_REGISTER_PAGE`. Routes, buttons and footer links are all
derived from that registry, so deleting a feature's directory deletes its pages
and its place in every other page's navigation.

The front page is the base's own — `web/IndexPage.cpp`, the device's raw readings
and a button per registered page — so a firmware with no apps in it still answers
a phone. It was NOTES' form for as long as NOTES was the only app that wanted a
network, which meant deleting that app took the console's root page with it.

See [writing-apps.md](writing-apps.md#adding-a-page-to-the-console).

## 3. One frame, one owner

Apps never draw to the panel. `AppManager` owns a single `ui::Canvas`
(a 16-bit `M5Canvas`, 240×135 ≈ 64 KB, allocated in internal SRAM with a PSRAM
fallback), and each frame it:

1. clears the canvas,
2. draws the status bar (title, badge, battery),
3. calls `app->onDraw(canvas)`,
4. draws the footer hints and any toast,
5. pushes the whole sprite in one go.

Nothing tears, every app gets the same chrome for free, and the shell can be
restyled in one place. Portrait and landscape need the same number of bytes, so
a rotation change frees and reallocates the same block rather than fragmenting
the heap.

### Presentation is the app's, until the next app starts

`setRotation()` and `setChrome()` let an app reorient itself to how it is being
held. Both are overrides rather than settings: `startApp()` restores the declared
rotation and puts the chrome back, so an app that turned the device sideways and
then exited cannot leave it that way for the one after it.

Call them from `onTick`, never `onDraw`. Changing rotation frees and reallocates
the frame buffer, and doing that half way through a frame pushes a buffer that is
no longer the shape the panel expects.

## 4. Scheduling

`AppInfo` carries `tickHz` and `drawHz` separately, because instruments need
them separate. The level samples at 200 Hz (faster than the BMI270 emits, so no
sample is ever missed) and draws at 25 Hz. A menu can tick at 20 Hz and draw at
20 Hz. `onTick` receives the true elapsed microseconds, not the nominal period.

`cpuMhz` sits alongside them and is asserted on every app start, before the
capabilities, so a clock raised for one tool cannot leak into the next and never
moves under a radio that has just come up. It defaults to 240 - what every app
ran at before the field existed - so a tool's timing is its own to state rather
than the framework's to decide for it. The launcher, HELP and GAMES ask for 160
and idle at 27.6 mA rather than 32.9; APB is a fixed 80 MHz on the S3 whatever
the core does, so display SPI, RMT and I2S are unaffected by the choice.

`dimAfterSec` is the same idea for the panel: the framework's idle ladder dims at
45 s unless the app states otherwise, because a tool that is carried and listened
to rather than watched has the largest load on the cell lit for nobody. Blanking
stays at three minutes, or a minute past the dim if an app asks for one later
than that.

The main loop is single-threaded on purpose: no locks, no races, and an app
whose maths is deterministic stays deterministic. Apps that genuinely need
concurrency can spawn their own FreeRTOS task.

`AppManager::suspendDrawing()` exists so an app can silence the SPI bus - to keep
display traffic out of a measurement, or to stop a frame push costing samples an
app cannot afford to lose. It takes a duration rather than a flag on purpose, and
an app that re-arms it every tick until some condition is met has built a way to
freeze the panel with no error and no log. **Whatever re-arms it needs a
deadline**: bound the hold, release the panel when the deadline passes, and put
the reason on screen. A slow instrument that says why is fine; a stopped one that
says nothing is a bug report that costs a round trip to diagnose.

### The backlight comes down on its own

The panel is the largest single load on a 250 mAh cell, and for a long time
nothing ever turned it down: whatever brightness the last app set ran until the
battery was flat. `AppManager` now dims to a glow at 45 seconds idle and blanks
at three minutes, restoring whatever brightness was in force when it starts.

**Only the panel sleeps.** The app underneath is never told, never stopped and
never loses a capability - a ride goes on recording, a night goes on logging,
the web server goes on answering. What stops is the frame push, which while the
panel is dark is 64 KB of SPI nobody is looking at.

The idle clock lives in `MoodService`, and it resets on a press. The press that wakes a blanked panel is spent doing exactly that
and is not delivered to the app: pressing `A` to see how a recording is getting
on must not also stop it.

## 5. Input is a grammar, not a pin

Two buttons is not many. `InputService` builds a state machine on M5Unified's
debounced state and fixes the grammar device-wide:

| gesture | meaning |
|---|---|
| `A` click | primary action, app-defined |
| `A` hold | secondary action, app-defined (auto-repeats) |
| `B` click | secondary / cycle, app-defined |
| `B` hold | **BACK** — enforced by the framework |
| `A`+`B` together | **MENU** — enforced by the framework |

`Back` and `Menu` are synthesised centrally and handled by `AppManager` when an
app does not consume them, so **no app can ever trap the user** — which matters
a great deal on a device with no home button. Events are queued rather than
dispatched from the poll, so an app can be torn down mid-gesture without an
event landing on a destroyed object.

## 6. Persistence is namespaced

`Settings` wraps NVS with the app's `AppInfo::id` as the namespace, so two apps
cannot collide on a key and removing one cannot corrupt another's state. Blobs
are length-checked and the records carry a magic and a version, so a struct that
changes shape is ignored rather than misread.

---

## Testing

`mathx/` has no Arduino dependency, so the parts where correctness is
non-obvious run on the host:

```bash
pio test -e native
```

The inclinometer suite checks the claims the level app makes — that flip
calibration recovers a known bias and scale, that the reversal method separates
instrument error from surface slope, that the protractor is independent of the
rotation axis, and end-to-end that bias + scale error + misalignment + noise
together still land inside a tenth of a degree. An instrument validated only by
eyeballing a screen is an instrument nobody should trust.

## Adding a tool

See [writing-apps.md](writing-apps.md). The short version is: one file.
