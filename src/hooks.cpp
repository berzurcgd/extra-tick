// Extra Tick - gameplay hooks.
//
// IDEA
//   A click normally lives for exactly one physics tick. We keep it "live" for
//   up to N ticks (N = tick-window setting) by:
//     1. re-applying the push on each of the next N-1 ticks, and
//     2. holding back the real release until that window is over.
//   Everything is done INSIDE the game's own tick loop (GJBaseGameLayer::
//   processCommands), going through the game's own handleButton, so player
//   position, rotation, velocity and orb/pad logic are all computed by vanilla
//   code in the normal order. We never touch player state directly, which is
//   what keeps the physics from desyncing.
//
// HOOK MAP (check docs.geode-sdk.org/classes/GJBaseGameLayer after a GD update)
//   GJBaseGameLayer::handleButton(bool down, int button, bool isPlayer1)
//   GJBaseGameLayer::processCommands(float dt, bool isHalfTick, bool isLastTick)
//   PlayLayer::resetLevel()
//   PlayLayer::onQuit()
//
// Why handleButton and not queueButton? queueButton is "out of line" (inlined)
// on Windows in current bindings, so it cannot be hooked there. handleButton
// is called from inside the tick loop when a queued input is applied, which is
// also exactly the point we want to observe inputs.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "ExtraTick.hpp"

using namespace geode::prelude;
using namespace extratick;

class $modify(ExtraTickLayer, GJBaseGameLayer) {

    // Calls the ORIGINAL handleButton (bypasses our own hook).
    // Inside a $modify class, `GJBaseGameLayer::handleButton(...)` is the
    // original function, so injected inputs never re-enter the hook below.
    void extraTickApply(bool down, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, jumpButton(), isPlayer1);
    }

    // Gives up on a slot and delivers any release we were holding back.
    // Used when the mod is toggled off or the window is abandoned mid-way.
    void extraTickFlush(Slot& slot, bool isPlayer1) {
        if (slot.releaseDeferred) this->extraTickApply(false, isPlayer1);
        slot = Slot{};
    }

    // HOOK 1: observe inputs as the game applies them.
    //
    // Runs inside processCommands (via the game's queued-button handling), so
    // we see pushes/releases in tick order. We only react to the jump button.
    //   push    -> open (or restart) the window.
    //   release -> if a window is open, hold it back; processCommands will
    //              deliver it once the window closes.
    //
    // GD-UPDATE: if handleButton's signature or the meaning of its bool
    // changes (note: isPlayer1, NOT isPlayer2), fix it here first.
    void handleButton(bool down, int button, bool isPlayer1) {
        if (button != jumpButton() || !shouldAssist(this)) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto& slot = g_slots[isPlayer1 ? 0 : 1];

        if (down) {
            // A new real click arrived while a release was being held back:
            // deliver that release first so the game sees release -> push,
            // exactly as if the player had released and clicked again.
            if (slot.active && slot.releaseDeferred) {
                this->extraTickApply(false, isPlayer1);
            }
            slot.active = true;
            slot.releaseDeferred = false;
            slot.ticksLeft = extraTicks();
            GJBaseGameLayer::handleButton(true, button, isPlayer1);
            return;
        }

        // Release.
        if (slot.active) {
            slot.releaseDeferred = true;   // swallow now, deliver later
            return;
        }
        GJBaseGameLayer::handleButton(false, button, isPlayer1);
    }

    // HOOK 2: the per-tick driver.
    //
    // processCommands runs once per physics tick (and additionally for "half
    // ticks"). We only count full ticks. Order inside one tick:
    //   PRE : re-apply the push for every open window (so the vanilla tick
    //         sees a fresh click this tick).
    //   ORIG: vanilla tick (collisions, orbs, pads, movement, ...).
    //   POST: any window that just used its last extra tick is closed and the
    //         held-back release is delivered; it takes effect on the next tick.
    //
    // Total ticks covered = 1 (the real click) + extraTicks().
    //
    // GD-UPDATE: if the tick loop is restructured (e.g. a new substep scheme
    // or processCommands renamed), verify with Debug Logging that the injected
    // ticks are exactly 1 per physics tick, then adjust the isHalfTick filter.
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        const bool assist = shouldAssist(this);

        if (!isHalfTick) {
            for (int i = 0; i < 2; ++i) {
                auto& slot = g_slots[i];
                if (!slot.active) continue;
                const bool isPlayer1 = (i == 0);

                // Mod disabled / left practice mid-window, or players vanished.
                if (!assist || !m_player1) {
                    this->extraTickFlush(slot, isPlayer1);
                    continue;
                }

                if (slot.ticksLeft > 0) {
                    this->extraTickApply(true, isPlayer1);
                    --slot.ticksLeft;
                    if (settingDebugLog()) {
                        log::debug("[ExtraTick] re-applied click P{} ({} extra left)",
                                   isPlayer1 ? 1 : 2, slot.ticksLeft);
                    }
                }
            }
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        if (!isHalfTick) {
            for (int i = 0; i < 2; ++i) {
                auto& slot = g_slots[i];
                if (!slot.active || slot.ticksLeft > 0) continue;

                // Window finished. If the player already let go, let go now;
                // if they are still holding, nothing to do - their real
                // release will pass straight through because the slot is idle.
                this->extraTickFlush(slot, i == 0);
            }
        }
    }
};

class $modify(ExtraTickPlayLayer, PlayLayer) {

    // Called on death / restart. The game resets player state itself; we just
    // drop our window state and deliver a held-back release so no button is
    // left logically "stuck" in the fresh attempt.
    void resetLevel() {
        const bool p1Deferred = g_slots[0].releaseDeferred;
        const bool p2Deferred = g_slots[1].releaseDeferred;
        clearSlots();
        PlayLayer::resetLevel();
        // Slots are idle now, so these pass through our handleButton hook.
        if (p1Deferred) this->handleButton(false, jumpButton(), true);
        if (p2Deferred) this->handleButton(false, jumpButton(), false);
    }

    // Leaving the level: never carry state into the next one.
    void onQuit() {
        clearSlots();
        PlayLayer::onQuit();
    }
};
