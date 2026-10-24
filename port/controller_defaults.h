#pragma once

#include <memory>

namespace Ship {
class ControllerDefaultMappings;
}

namespace ssb64 {

// The layout a gamepad gets the first time it is seen (players can still
// rebind everything in the Input Editor). Modern-pad style:
//   A = A, B = B, X/Y = jump (C-Up), LB/LT = grab (R), RB/RT = shield (Z),
//   D-Pad = taunt (L), right stick = attacks (N64 D-Pad, turned into smash
//   attacks in matches by the Right Stick Attacks option), left stick = move.
// Keyboard defaults are libultraship's.
std::shared_ptr<Ship::ControllerDefaultMappings> MakeControllerDefaultMappings();

} // namespace ssb64
