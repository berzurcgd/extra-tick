#pragma once

// Shared state and settings helpers for Extra Tick.
//
// All gameplay state lives here (not in the $modify classes) because Geode's
// $modify classes cannot carry their own fields safely, and only one PlayLayer
// exists at a time anyway.

#include <Geode/Geode.hpp>
#include <array>
#include <algorithm>

namespace extratick {

    // PlayerButton::Jump. We only widen the jump button; left/right in
    // platformer mode are left alone.
    // GD-UPDATE: if PlayerButton is ever renumbered, this is the only place to fix.
    inline int jumpButton() {
        return static_cast<int>(PlayerButton::Jump);
    }

    // One slot per player (index 0 = player 1, 1 = player 2).
    struct Slot {
        bool active = false;           // a widened click is currently in flight
        bool releaseDeferred = false;  // the real release arrived and was held back
        int ticksLeft = 0;             // extra ticks still to inject
    };

    inline std::array<Slot, 2> g_slots{};

    // ---- Settings (single place to touch if Geode's settings API changes) ----

    inline bool settingEnabled() {
        return geode::Mod::get()->getSettingValue<bool>("enabled");
    }

    // Window size 1..4 -> number of EXTRA ticks 0..3.
    inline int extraTicks() {
        auto window = geode::Mod::get()->getSettingValue<int64_t>("tick-window");
        return std::clamp<int>(static_cast<int>(window), 1, 4) - 1;
    }

    inline bool settingPracticeOnly() {
        return geode::Mod::get()->getSettingValue<bool>("practice-only");
    }

    inline bool settingDebugLog() {
        return geode::Mod::get()->getSettingValue<bool>("debug-log");
    }

    // Should this layer be assisted right now?
    // GJBaseGameLayer is also the base of LevelEditorLayer, so we make sure it
    // is the actual PlayLayer, and respect the practice-only option.
    inline bool shouldAssist(GJBaseGameLayer* layer) {
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != layer) return false;
        if (!settingEnabled() || extraTicks() == 0) return false;
        if (settingPracticeOnly() && !(pl->m_isPracticeMode || pl->m_isTestMode)) return false;
        return true;
    }

    inline void clearSlots() {
        g_slots = {};
    }
}
