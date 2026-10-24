/**
 * overlay.h — the in-match netplay HUD: ping, input delay, rollbacks per
 * second, frame advantage, stalls, relay. A small ImGui window in the top
 * left that only exists while sys/netpeer.c has a session. Toggle with the
 * gNetplay.Overlay setting (Settings → Netplay).
 */
#pragma once

#include <memory>

namespace Ship {
class GuiWindow;
}

namespace ssb64 {

/* Creates the window; the caller adds it to the Gui. */
std::shared_ptr<Ship::GuiWindow> MakeNetplayOverlay();

} // namespace ssb64
