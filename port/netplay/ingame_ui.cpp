/**
 * ingame_ui.cpp — see ingame_ui.h.
 */
#include "ingame_ui.h"
#include "lobby.h"

#include <ship/window/gui/GuiWindow.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <imgui.h>

#include <cstring>

/* decomp/src/sys (C) */
extern "C" int syNetMenuCheckOnlinePanelScene(void);
extern "C" int syNetMenuCheckActive(void);
extern "C" int syNetPeerCheckActive(void);
extern "C" unsigned syNetPeerGetPingMs(void);

namespace ssb64 {

namespace {

class NetplayIngamePanel : public Ship::GuiWindow {
  public:
	NetplayIngamePanel()
	    : Ship::GuiWindow("gNetplay.IngamePanel", true, "Online", ImVec2(0, 0),
	                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
	                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
	                          ImGuiWindowFlags_NoMove) {}

	void Draw() override {
		if (CVarGetInteger("gNetplay.IngamePanel", 1) == 0) {
			return;
		}
		int state = port_lobby_state();
		bool connected = state == PORT_LOBBY_IN_MATCH || state == PORT_LOBBY_BOOTSTRAP;
		/* On the mode select and VS menu, always; elsewhere only while a
		 * connection is being made or exists in the menus (so Leave stays
		 * reachable). The battle has its own HUD. */
		if (!syNetMenuCheckOnlinePanelScene() && !(connected && syNetMenuCheckActive()) &&
		    !(state != PORT_LOBBY_IDLE && state != PORT_LOBBY_FAILED && !connected)) {
			return;
		}
		ImGuiViewport *vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - 16.0f),
		                        ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.80f);
		Ship::GuiWindow::Draw();
	}

  protected:
	void InitElement() override {
		std::strncpy(mDirect, CVarGetString("gNetplay.Direct", "192.168.1.2:7000"), sizeof(mDirect) - 1);
	}
	void UpdateElement() override {}

	void DrawElement() override {
		int state = port_lobby_state();
		bool busy = state == PORT_LOBBY_PUNCHING || state == PORT_LOBBY_BOOTSTRAP || state == PORT_LOBBY_IN_MATCH ||
		            state == PORT_LOBBY_POST_MATCH || state == PORT_LOBBY_REGISTERING ||
		            state == PORT_LOBBY_REGISTERED || state == PORT_LOBBY_WAITING;

		ImGui::SetWindowFontScale(1.15f);
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "ONLINE PLAY");
		ImGui::SameLine();
		ImGui::TextDisabled("  you are %s, UDP port %d", port_lobby_code(), CVarGetInteger("gNetplay.Port", 7000));

		if (state == PORT_LOBBY_IN_MATCH) {
			unsigned ping = syNetPeerGetPingMs();
			if (ping != 0) ImGui::Text("Connected, ping %u ms. Both controllers are live in these menus.", ping);
			else ImGui::TextUnformatted("Connected. Both controllers are live in these menus.");
			if (ImGui::Button("Leave")) port_lobby_cancel();
			ImGui::SetWindowFontScale(1.0f);
			return;
		}

		bool server_set = CVarGetString("gNetplay.Server", "")[0] != '\0';
		if (server_set) {
			ImGui::SetNextItemWidth(170.0f);
			ImGui::InputText("Friend's code", mFriend, sizeof(mFriend),
			                 ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
			ImGui::SameLine();
			ImGui::BeginDisabled(busy);
			if (ImGui::Button("Connect")) port_lobby_connect(mFriend);
			ImGui::EndDisabled();
		}

		ImGui::SetNextItemWidth(170.0f);
		if (ImGui::InputText("Friend's IP:port", mDirect, sizeof(mDirect), ImGuiInputTextFlags_CharsNoBlank)) {
			CVarSetString("gNetplay.Direct", mDirect);
			CVarSave();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(busy);
		if (ImGui::Button("Host")) port_lobby_connect_direct(mDirect, 1);
		ImGui::SameLine();
		if (ImGui::Button("Join")) port_lobby_connect_direct(mDirect, 0);
		ImGui::EndDisabled();
		if (busy) {
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) port_lobby_cancel();
		}
		if (ImGui::IsItemHovered() && !busy) {
			ImGui::SetTooltip("One of you presses Host, the other Join, both with the other's address.\n"
			                  "The host needs its UDP port reachable (same LAN, or a port forward).");
		}

		int delay = CVarGetInteger("gNetplay.Delay", 2);
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::SliderInt("Input delay", &delay, 0, 8)) {
			CVarSetInteger("gNetplay.Delay", delay);
			CVarSave();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(2 for most connections)");

		const char *status = port_lobby_status();
		if (state == PORT_LOBBY_FAILED) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", status);
		else if (state != PORT_LOBBY_IDLE) ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "%s", status);
		else ImGui::TextDisabled("%s", status);
		ImGui::SetWindowFontScale(1.0f);
	}

  private:
	char mDirect[32] = {};
	char mFriend[24] = {};
};

} // namespace

std::shared_ptr<Ship::GuiWindow> MakeNetplayIngamePanel() {
	return std::make_shared<NetplayIngamePanel>();
}

} // namespace ssb64
