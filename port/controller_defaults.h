#pragma once

#include <memory>

namespace Ship {
class ControllerDefaultMappings;
}

namespace ssb64 {

// The layout a gamepad gets the first time it is seen (players can still
// rebind everything in the Input Editor): libultraship's stock layout
// (A, B, LB = L, LT = Z, RT = R, Start, D-pad) plus X/Y = jump (C-Up) and the
// right stick on the N64 D-pad, which the Right Stick Attacks option turns
// into smash attacks / aerials in matches. Keyboard defaults are
// libultraship's.
std::shared_ptr<Ship::ControllerDefaultMappings> MakeControllerDefaultMappings();

} // namespace ssb64
