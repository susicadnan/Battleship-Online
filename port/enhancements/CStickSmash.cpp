#include "enhancements.h"
#include "internal/buttons.h"

#include <libultraship/bridge/consolevariablebridge.h>

namespace {

constexpr const char* kCStickSmashCVars[PORT_ENHANCEMENT_MAX_PLAYERS] = {
    "gEnhancements.CStickSmash.P1",
    "gEnhancements.CStickSmash.P2",
    "gEnhancements.CStickSmash.P3",
    "gEnhancements.CStickSmash.P4",
};

constexpr const char* kDPadSmashCVars[PORT_ENHANCEMENT_MAX_PLAYERS] = {
    "gEnhancements.DPadSmash.P1",
    "gEnhancements.DPadSmash.P2",
    "gEnhancements.DPadSmash.P3",
    "gEnhancements.DPadSmash.P4",
};

struct DirectionBits {
    unsigned short up, down, left, right;
};

// Shared by the C-button and D-pad flavours: four digital directions become a
// one-frame stick flick plus A.
void DirectionalSmash(const DirectionBits& dir, unsigned short* button_hold, unsigned short* button_tap,
                      signed char* stick_x, signed char* stick_y, unsigned short tap_pre_remap) {
    using namespace ssb64::enhancements::internal;

    // Always strip the direction bits from both hold/tap so the engine never
    // sees the raw input on this player when the remap is on (otherwise the
    // stock C-button jump assignments would also fire alongside the
    // synthesised attack).
    const unsigned short bits = dir.up | dir.down | dir.left | dir.right;
    *button_hold &= ~bits;
    *button_tap  &= ~bits;

    // Fire the smash/aerial only on the rising edge of the button. A real
    // C-stick is analog and self-centers; translating it from a digital button
    // means as long as the button is pressed, the engine would otherwise see
    // stick pinned at ±80 — which manifests as drift in the air / running on
    // the ground (issue #97 [1]). Holding A across multiple frames also isn't
    // useful in SSB64 (no smash-attack charging), so a single-frame edge is
    // enough to register the attack.
    //
    // Cross-axis is forced to 0 so the input lands as a pure-axis tilt rather
    // than a 45° diagonal when the L-stick is already held sideways — without
    // this, e.g. C-up while running right reads as (80, 80) and the engine's
    // dominant-axis logic picks fair/bair instead of upair (issue #97 [2]).
    signed char x, y;
    if (tap_pre_remap & dir.up) {
        x = 0;   y = 80;
    } else if (tap_pre_remap & dir.down) {
        x = 0;   y = -80;
    } else if (tap_pre_remap & dir.left) {
        x = -80; y = 0;
    } else if (tap_pre_remap & dir.right) {
        x = 80;  y = 0;
    } else {
        return;
    }
    *stick_x = x;
    *stick_y = y;
    *button_hold |= A_BUTTON;
    *button_tap  |= A_BUTTON;
}

} // namespace

extern "C" void port_enhancement_c_stick_smash(int player_index, unsigned short* button_hold, unsigned short* button_tap, signed char* stick_x, signed char* stick_y, unsigned short tap_pre_remap) {
    using namespace ssb64::enhancements::internal;

    if (player_index < 0 || player_index >= PORT_ENHANCEMENT_MAX_PLAYERS) return;
    if (!CVarGetInteger(kCStickSmashCVars[player_index], 0)) return;

    DirectionalSmash({ U_CBUTTON, D_CBUTTON, L_CBUTTON, R_CBUTTON }, button_hold, button_tap, stick_x, stick_y,
                     tap_pre_remap);
}

// On by default: the stock gamepad layout (see port/controller_defaults.cpp)
// puts the right stick on the N64 D-pad, so this is what makes the right stick
// an attack stick. Menus still read the D-pad as-is because the caller only
// runs this during gameplay.
extern "C" void port_enhancement_dpad_smash(int player_index, unsigned short* button_hold, unsigned short* button_tap, signed char* stick_x, signed char* stick_y, unsigned short tap_pre_remap) {
    using namespace ssb64::enhancements::internal;

    if (player_index < 0 || player_index >= PORT_ENHANCEMENT_MAX_PLAYERS) return;
    if (!CVarGetInteger(kDPadSmashCVars[player_index], 1)) return;

    DirectionalSmash({ U_JPAD, D_JPAD, L_JPAD, R_JPAD }, button_hold, button_tap, stick_x, stick_y, tap_pre_remap);
}

namespace ssb64 {
namespace enhancements {

const char* CStickSmashCVarName(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= PORT_ENHANCEMENT_MAX_PLAYERS) {
        return kCStickSmashCVars[0];
    }
    return kCStickSmashCVars[playerIndex];
}

const char* DPadSmashCVarName(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= PORT_ENHANCEMENT_MAX_PLAYERS) {
        return kDPadSmashCVars[0];
    }
    return kDPadSmashCVars[playerIndex];
}

} // namespace enhancements
} // namespace ssb64
