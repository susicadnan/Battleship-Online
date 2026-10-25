#include "controller_defaults.h"

#include <libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h>
#include <libultraship/libultra/controller.h>

namespace ssb64 {

std::shared_ptr<Ship::ControllerDefaultMappings> MakeControllerDefaultMappings() {
    // Mapping a pad to N64 buttons happens before anything reaches the game,
    // and netplay sends what the game sees, so peers can use different
    // layouts without affecting the simulation.
    // libultraship's stock layout, except X/Y also jump (C-Up) and the right
    // stick drives the N64 D-pad instead of the C buttons.
    std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GameControllerButton>> buttons = {
        { BTN_A, { SDL_CONTROLLER_BUTTON_A } },
        { BTN_B, { SDL_CONTROLLER_BUTTON_B } },
        { BTN_L, { SDL_CONTROLLER_BUTTON_LEFTSHOULDER } },
        { BTN_START, { SDL_CONTROLLER_BUTTON_START } },
        { BTN_CUP, { SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y } },
        { BTN_DUP, { SDL_CONTROLLER_BUTTON_DPAD_UP } },
        { BTN_DDOWN, { SDL_CONTROLLER_BUTTON_DPAD_DOWN } },
        { BTN_DLEFT, { SDL_CONTROLLER_BUTTON_DPAD_LEFT } },
        { BTN_DRIGHT, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT } },
    };
    std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GameControllerAxis, int32_t>>> axes = {
        { BTN_R, { { SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1 } } },
        { BTN_Z, { { SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1 } } },
        { BTN_DUP, { { SDL_CONTROLLER_AXIS_RIGHTY, -1 } } },
        { BTN_DDOWN, { { SDL_CONTROLLER_AXIS_RIGHTY, 1 } } },
        { BTN_DLEFT, { { SDL_CONTROLLER_AXIS_RIGHTX, -1 } } },
        { BTN_DRIGHT, { { SDL_CONTROLLER_AXIS_RIGHTX, 1 } } },
    };

    return std::make_shared<LUS::ControllerDefaultMappings>(
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>>(),
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, Ship::KbScancode>>>(), buttons,
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GameControllerButton>>>(),
        axes,
        std::unordered_map<Ship::StickIndex,
                           std::vector<std::pair<Ship::Direction, std::pair<SDL_GameControllerAxis, int32_t>>>>());
}

} // namespace ssb64
