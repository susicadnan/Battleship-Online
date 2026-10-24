/**
 * overlay.cpp — see overlay.h.
 */
#include "overlay.h"
#include "lobby.h"

#include <ship/window/gui/GuiWindow.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <imgui.h>

#include <chrono>
#include <cstdint>

/* decomp/src/sys (C) */
extern "C" int syNetPeerCheckActive(void);
extern "C" unsigned syNetPeerGetPingMs(void);
extern "C" unsigned syNetPeerGetInputDelay(void);
extern "C" int syNetPeerGetLocalAdvantage(void);
extern "C" int syNetPeerGetAdvantageDelta(void);
extern "C" int syNetPeerCheckSpectating(void);
extern "C" void syNetStateGetStats(unsigned *rollbacks, unsigned *resim_ticks, unsigned *max_depth, unsigned *stalls,
                                   unsigned *timesync_skips);
extern "C" unsigned syNetInputGetRemoteLag(void);

namespace ssb64 {

namespace {

class NetplayOverlay : public Ship::GuiWindow {
  public:
	NetplayOverlay()
	    : Ship::GuiWindow("gNetplay.Overlay", true, "Netplay HUD", ImVec2(0, 0),
	                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
	                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
	                          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs) {}

	void Draw() override {
		/* Only while a netplay session exists; the Gui draws every visible
		 * window every frame and we have nothing to say otherwise. */
		if (!syNetPeerCheckActive() || CVarGetInteger("gNetplay.Overlay", 1) == 0) {
			return;
		}
		ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.55f);
		Ship::GuiWindow::Draw();
	}

  protected:
	void InitElement() override {}
	void UpdateElement() override {}

	void DrawElement() override {
		if (syNetPeerCheckSpectating()) {
			/* A spectator never predicts, so the rollback numbers say nothing. */
			unsigned ping = syNetPeerGetPingMs();
			ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "WATCHING");
			ImGui::SameLine();
			if (ping != 0) ImGui::TextDisabled("| ping %u ms to the host", ping);
			else ImGui::TextDisabled("| you play when it is your turn");
			return;
		}
		unsigned rollbacks = 0, resim = 0, max_depth = 0, stalls = 0, timesync = 0;
		syNetStateGetStats(&rollbacks, &resim, &max_depth, &stalls, &timesync);

		/* Rollbacks per second from the running counter. */
		auto now = std::chrono::steady_clock::now();
		if (mLastSample.time_since_epoch().count() == 0) {
			mLastSample = now;
			mLastRollbacks = rollbacks;
		}
		double elapsed = std::chrono::duration<double>(now - mLastSample).count();
		if (elapsed >= 1.0) {
			mRollbacksPerSecond = (float)((rollbacks - mLastRollbacks) / elapsed);
			mLastRollbacks = rollbacks;
			mLastSample = now;
		}

		unsigned ping = syNetPeerGetPingMs();
		int delta = syNetPeerGetAdvantageDelta();
		unsigned lag = syNetInputGetRemoteLag();
		int state = port_lobby_state();

		if (ping == 0) ImGui::TextDisabled("ping --");
		else if (ping < 60) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "ping %u ms", ping);
		else if (ping < 120) ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "ping %u ms", ping);
		else ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "ping %u ms", ping);
		ImGui::SameLine();
		ImGui::TextUnformatted("|");
		ImGui::SameLine();
		ImGui::Text("delay %u", syNetPeerGetInputDelay());
		ImGui::SameLine();
		ImGui::TextUnformatted("|");
		ImGui::SameLine();
		ImGui::Text("rollback %.1f/s (max %u)", mRollbacksPerSecond, max_depth);
		ImGui::SameLine();
		ImGui::TextUnformatted("|");
		ImGui::SameLine();
		ImGui::Text("predicting %u", lag);
		if (delta > 1) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "ahead +%d", delta);
		} else if (delta < -1) {
			ImGui::SameLine();
			ImGui::TextDisabled("behind %d", -delta);
		}
		if (stalls != 0 || timesync != 0) {
			ImGui::SameLine();
			ImGui::TextDisabled("| stalls %u sync %u", stalls, timesync);
		}
		if (state == PORT_LOBBY_IN_MATCH && port_lobby_status()[0] != '\0') {
			/* nothing more: the lobby status would just repeat "Match starting" */
		}
	}

  private:
	std::chrono::steady_clock::time_point mLastSample{};
	unsigned mLastRollbacks = 0;
	float mRollbacksPerSecond = 0.0f;
};

} // namespace

std::shared_ptr<Ship::GuiWindow> MakeNetplayOverlay() {
	return std::make_shared<NetplayOverlay>();
}

} // namespace ssb64
