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
extern "C" int syNetPeerGetMemberInfo(int member, const char **code, int *slot, int *queue_pos, int *is_me);
extern "C" int syNetPeerCheckGroupHost(void);

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
		/* While the panel is up we are reachable by our code. */
		port_lobby_listen();
		ImGui::SetNextWindowBgAlpha(0.80f);
		Ship::GuiWindow::Draw();
	}

  protected:
	void InitElement() override {
		std::strncpy(mName, CVarGetString("gNetplay.Name", "PLAYER"), sizeof(mName) - 1);
		std::strncpy(mFriend, CVarGetString("gNetplay.LastFriend", ""), sizeof(mFriend) - 1);
	}
	void UpdateElement() override {}

	void DrawElement() override {
		int state = port_lobby_state();
		bool busy = state == PORT_LOBBY_PUNCHING || state == PORT_LOBBY_BOOTSTRAP || state == PORT_LOBBY_IN_MATCH ||
		            state == PORT_LOBBY_POST_MATCH || state == PORT_LOBBY_REGISTERING ||
		            state == PORT_LOBBY_REGISTERED || state == PORT_LOBBY_WAITING || state == PORT_LOBBY_SEEKING ||
		            state == PORT_LOBBY_HOSTING;

		ImGui::SetWindowFontScale(1.15f);
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "ONLINE PLAY");
		ImGui::SameLine();
		ImGui::TextDisabled("  your code");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", port_lobby_code());
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(port_lobby_code());

		DrawRequests();

		if (state == PORT_LOBBY_HOSTING) {
			ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "%s", port_lobby_status());
			ImGui::BulletText("%s (you, host)", port_lobby_code());
			for (int i = 0; i < port_lobby_host_guest_count(); i++) ImGui::BulletText("%s", port_lobby_host_guest_code(i));
			DrawGroupSettings();
			ImGui::BeginDisabled(port_lobby_host_guest_count() == 0);
			if (ImGui::Button("Start")) port_lobby_host_start();
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) port_lobby_cancel();
			ImGui::SetWindowFontScale(1.0f);
			return;
		}
		if (state == PORT_LOBBY_IN_MATCH || (state == PORT_LOBBY_BOOTSTRAP && DrawRoster(false))) {
			unsigned ping = syNetPeerGetPingMs();
			if (ping != 0) ImGui::Text("Connected, ping %u ms.", ping);
			else ImGui::TextUnformatted("Connected.");
			DrawRoster(true);
			if (syNetPeerCheckGroupHost()) DrawGroupSettings();
			if (ImGui::Button("Leave")) port_lobby_cancel();
			ImGui::SetWindowFontScale(1.0f);
			return;
		}

		if (!busy) {
			ImGui::SetNextItemWidth(130.0f);
			if (ImGui::InputText("Your name", mName, sizeof(mName),
			                     ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank)) {
				port_lobby_set_name(mName);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(shown over your fighter)");
		}

		ImGui::SetNextItemWidth(170.0f);
		ImGui::BeginDisabled(busy);
		bool enter = ImGui::InputText("Friend's code", mFriend, sizeof(mFriend),
		                              ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank |
		                                  ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if (ImGui::Button("Play") || enter) {
			CVarSetString("gNetplay.LastFriend", mFriend);
			CVarSave();
			port_lobby_play_code(mFriend);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered() && !busy) {
			ImGui::SetTooltip("Your friend enters your code too, or presses Accept when your request shows up.\n"
			                  "No addresses or port forwarding needed.");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("or");
		ImGui::SameLine();
		ImGui::BeginDisabled(busy);
		if (ImGui::Button("Open a lobby")) port_lobby_connect_direct("", 1);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered() && !busy) {
			ImGui::SetTooltip("Up to four people: friends send requests with your code,\n"
			                  "you accept each one, then press Start.");
		}
		if (busy) {
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) port_lobby_cancel();
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
	/* "BOB#2222 wants to play  [Accept] [No]" for everyone who asked. */
	void DrawRequests() {
		for (int i = 0; i < port_lobby_request_count(); i++) {
			ImGui::PushID(i);
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s wants to play", port_lobby_request_code(i));
			ImGui::SameLine();
			if (ImGui::Button("Accept")) port_lobby_request_answer(i, 1);
			ImGui::SameLine();
			if (ImGui::Button("No")) port_lobby_request_answer(i, 0);
			ImGui::PopID();
		}
	}

	/* Who plays which port and who is watching (sys/netpeer.c's round).
	 * draw = false only asks whether there is a roster yet. */
	bool DrawRoster(bool draw) {
		bool any = false;
		for (int m = 0; m < 4; m++) {
			const char *code = "";
			int slot = -1, queue_pos = -1, is_me = 0;
			if (!syNetPeerGetMemberInfo(m, &code, &slot, &queue_pos, &is_me)) continue;
			any = true;
			if (!draw) continue;
			const char *name = code[0] != '\0' ? code : (m == 0 ? "host" : "guest");
			if (slot >= 0) {
				ImGui::BulletText("P%d  %s%s", slot + 1, name, is_me ? "  (you)" : "");
			} else if (queue_pos >= 0) {
				ImGui::BulletText("watching, %s  %s%s", queue_pos == 0 ? "next up" : "in line", name,
				                  is_me ? "  (you)" : "");
			} else {
				ImGui::BulletText("joins at the next character select  %s%s", name, is_me ? "  (you)" : "");
			}
		}
		return any;
	}

	/* The host's choices; they apply from the next character select. */
	void DrawGroupSettings() {
		int mode = CVarGetInteger("gNetplay.GroupMode", 0);
		if (ImGui::RadioButton("1v1, winner stays on", mode == 0)) {
			CVarSetInteger("gNetplay.GroupMode", 0);
			CVarSave();
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Everyone plays", mode == 1)) {
			CVarSetInteger("gNetplay.GroupMode", 1);
			CVarSave();
		}
		bool preset = CVarGetInteger("gNetplay.Preset", 1) != 0;
		if (ImGui::Checkbox("Dream Land, no items, no hazards", &preset)) {
			CVarSetInteger("gNetplay.Preset", preset ? 1 : 0);
			CVarSave();
		}
	}

	char mName[16] = {};
	char mFriend[24] = {};
};

} // namespace

std::shared_ptr<Ship::GuiWindow> MakeNetplayIngamePanel() {
	return std::make_shared<NetplayIngamePanel>();
}

} // namespace ssb64
