/**
 * ingame_ui.h — the Online panel shown over the game's own mode select and
 * VS menu: connect to a friend (direct ip:port, or by connect code when a
 * rendezvous server is set) without opening the settings menu. Once two
 * players are connected the game's menus themselves run in lockstep
 * (sys/netmenu.c), so this panel only has to get them connected and offer
 * a way out.
 */
#pragma once

#include <memory>

namespace Ship {
class GuiWindow;
}

namespace ssb64 {
std::shared_ptr<Ship::GuiWindow> MakeNetplayIngamePanel();
}
