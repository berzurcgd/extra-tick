# Extra Tick (Geode mod)

Widens the timing window of a click from about 1 physics tick to up to 4, so
frame-perfect inputs are easier to practice. Think of it as aim assist for clicks.

Targets Geometry Dash 2.2081 with Geode 5.x. Bindings were checked against the
Geode docs for `GJBaseGameLayer` and `PlayerButtonCommand`, but this code has
**not been compiled or run in-game** by me, so expect to do a first build and
test pass yourself.

## Project layout

```
extra-tick/
  mod.json                  id, name, version, developer, settings
  CMakeLists.txt            standard Geode mod build
  src/
    ExtraTick.hpp           shared state + settings helpers
    hooks.cpp               GJBaseGameLayer / PlayLayer hooks (the logic)
    StatusWindow.cpp        ON/OFF status window + toggle hotkey (Windows)
  .github/workflows/
    build.yml               CI build for Windows, macOS, Android
```

## Settings

| Setting | Default | Notes |
|---|---|---|
| Enable Extra Tick | on | Master switch. Also flipped by the hotkey below. |
| Tick Window Size | 2 | Slider 1-4. 1 = vanilla. 4 = click is re-applied for 3 extra ticks. |
| Toggle Hotkey | F7 | Windows only. Pick from F6-F10, Insert, Home, End, PageUp, PageDown, Pause. Only fires while GD is the focused window. |
| Show Status Window | on | Windows only. Small always-on-top window showing ON/OFF and the tick window size. Drag it anywhere. Does not show over exclusive fullscreen. |
| Practice / Test Mode Only | off | When on, assist only runs in Practice Mode or Startpos test runs. When off, it also works in normal mode. |
| Debug Logging | off | Logs each injected tick to the Geode console. |

## Building

1. Install the Geode CLI and SDK: https://docs.geode-sdk.org/getting-started/
   ```
   geode sdk install
   geode sdk update
   geode sdk install-binaries     # needed on Windows/macOS
   ```
   Make sure `GEODE_SDK` is set (the CLI does this for you).
2. Update `id` and `developer` in `mod.json`, and `repository` if you publish.
3. Build:
   ```
   cd extra-tick
   geode build
   ```
   The result is `build/yourname.extra-tick.geode`. Copy it into your Geode
   `mods` folder (or run `geode package install <file>`), then launch GD.
4. The hotkey and status window are built in. No other mods are needed. On
   macOS/Android those two features are skipped; the click assist still works.

CI: pushing to GitHub runs `.github/workflows/build.yml`, which builds all
platforms and uploads a combined `.geode`.

## How the tick expansion works

GD runs physics in fixed ticks. A click is queued, then applied inside one
tick by `GJBaseGameLayer::handleButton`. If the player is not in a state where
a jump is possible during that exact tick (one tick before landing, one tick
before touching an orb, and so on), the click is wasted.

Extra Tick keeps a small state record per player (`active`, `releaseDeferred`,
`ticksLeft`):

1. **Click arrives** (`handleButton(down=true)`): pass it to the game as normal
   and open a window with `ticksLeft = window - 1`.
2. **Every full tick** (`processCommands`, before the vanilla tick runs): for each
   open window, call the original `handleButton(true)` again and decrement
   `ticksLeft`. The vanilla tick then sees a fresh click, so a jump, orb or pad
   that becomes available 1 to 3 ticks late still registers.
3. **Release arrives** while a window is open: swallow it and remember
   `releaseDeferred`.
4. **After the vanilla tick**, if `ticksLeft` reached 0, close the window and
   deliver the held-back release (if any). It takes effect on the next tick,
   so the player is never left "stuck" holding.
5. **New click during a held-back release**: deliver the release first, then the
   push, so the game sees the same release -> push sequence the player made.
6. **Reset / quit / mod disabled / left practice mid-window**: state is cleared
   and any held release is delivered.

### Why this should not desync position or rotation

- We never write to player position, velocity, rotation or any physics field.
- All injected inputs go through the game's own `handleButton`, inside the
  game's own tick loop, in tick order. The game computes everything else.
- Half ticks are ignored so the window counts physics ticks, not rendered or
  substep calls.

### Known trade-offs

- Because the click is genuinely re-applied, it can trigger a second orb or pad
  that you did not intend if one is within the window. That is the point of the
  assist, but lower the window if it feels too forgiving.
- This changes what inputs the game sees, so it is a practice aid. Do not use
  it for rated-level records or verification. The "Practice / Test Mode Only"
  setting is there if you want to restrict it to practice.

## What to check when GD or Geode updates

Search for `GD-UPDATE` and `GEODE-UPDATE` in the source. In priority order:

1. `GJBaseGameLayer::handleButton(bool down, int button, bool isPlayer1)`
   signature and the meaning of `isPlayer1`.
2. `GJBaseGameLayer::processCommands(float, bool isHalfTick, bool isLastTick)`
   exists and still runs once per physics tick. Turn on Debug Logging and
   confirm exactly one re-apply per tick.
3. `PlayLayer::resetLevel`, `PlayLayer::onQuit`, `m_isPracticeMode`, `m_isTestMode`.
4. `PlayerButton::Jump` enum value (`jumpButton()` in `ExtraTick.hpp`).
5. `StatusWindow.cpp`: the `CCScheduler::update` hook, the Notification API, and
   the "one-of" key list (must match `toggle-key` in `mod.json`).
6. `mod.json`: bump `geode` and `gd` versions.
