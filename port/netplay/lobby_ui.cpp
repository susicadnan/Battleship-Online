/**
 * lobby_ui.cpp — see lobby_ui.h.
 */
#include "lobby_ui.h"
#include "lobby.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <imgui.h>

#include <cstring>

namespace {

/* decomp FTKind / GRKind values, in enum order (ft/ftdef.h, gr/grdef.h). */
const char *const kFighters[] = { "Mario",  "Fox",   "Donkey Kong", "Samus",   "Luigi",      "Link",
                                  "Yoshi",  "Captain Falcon", "Kirby", "Pikachu", "Jigglypuff", "Ness" };
const char *const kStages[] = { "Peach's Castle", "Sector Z",  "Kongo Jungle", "Planet Zebes",    "Hyrule Castle",
                                "Yoshi's Island", "Dream Land", "Saffron City", "Mushroom Kingdom" };

char sName[16];
char sServer[64];
char sFriend[24];
char sDirect[32];
bool sLoaded = false;

void load_fields()
{
	if (sLoaded) return;
	sLoaded = true;
	std::strncpy(sName, CVarGetString("gNetplay.Name", "PLAYER"), sizeof(sName) - 1);
	std::strncpy(sServer, CVarGetString("gNetplay.Server", ""), sizeof(sServer) - 1);
	std::strncpy(sDirect, CVarGetString("gNetplay.Direct", "192.168.1.2:7000"), sizeof(sDirect) - 1);
}

bool busy()
{
	int s = port_lobby_state();
	return s == PORT_LOBBY_PUNCHING || s == PORT_LOBBY_BOOTSTRAP || s == PORT_LOBBY_IN_MATCH ||
	       s == PORT_LOBBY_POST_MATCH;
}

int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

void PortNetplayDrawPanel()
{
	load_fields();

	ImGui::SeparatorText("Your connect code");
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::InputText("Name", sName, sizeof(sName), ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank)) {
		port_lobby_set_name(sName);
	}
	ImGui::SameLine();
	ImGui::TextUnformatted(port_lobby_code());
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Give this to the friend you want to play. Click to copy.");
	if (ImGui::IsItemClicked()) ImGui::SetClipboardText(port_lobby_code());

	ImGui::SeparatorText("Match settings");
	int fighter = clamp(CVarGetInteger("gNetplay.Fighter", 0), 0, (int)(sizeof(kFighters) / sizeof(kFighters[0])) - 1);
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::Combo("Your fighter", &fighter, kFighters, (int)(sizeof(kFighters) / sizeof(kFighters[0])))) {
		CVarSetInteger("gNetplay.Fighter", fighter);
		CVarSave();
	}
	int stage = clamp(CVarGetInteger("gNetplay.Stage", 0), 0, (int)(sizeof(kStages) / sizeof(kStages[0])) - 1);
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::Combo("Stage (host picks)", &stage, kStages, (int)(sizeof(kStages) / sizeof(kStages[0])))) {
		CVarSetInteger("gNetplay.Stage", stage);
		CVarSave();
	}
	int delay = clamp(CVarGetInteger("gNetplay.Delay", 2), 0, 8);
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::SliderInt("Input delay (frames)", &delay, 0, 8)) {
		CVarSetInteger("gNetplay.Delay", delay);
		CVarSave();
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Frames your own inputs are held back so both sides apply them together.\n"
		                  "Lower = more rollbacks, higher = more lag. 2 is a good default.");
	bool overlay = CVarGetInteger("gNetplay.Overlay", 1) != 0;
	if (ImGui::Checkbox("Show ping / rollback HUD during matches", &overlay)) {
		CVarSetInteger("gNetplay.Overlay", overlay ? 1 : 0);
		CVarSave();
	}

	ImGui::SeparatorText("Play a friend (connect code)");
	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::InputText("Rendezvous server", sServer, sizeof(sServer))) {
		CVarSetString("gNetplay.Server", sServer);
		CVarSave();
	}
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputText("Friend's code", sFriend, sizeof(sFriend), ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
	ImGui::BeginDisabled(busy());
	if (ImGui::Button("Connect")) {
		port_lobby_connect(sFriend);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(port_lobby_state() == PORT_LOBBY_IDLE);
	if (ImGui::Button("Cancel")) {
		port_lobby_cancel();
	}
	ImGui::EndDisabled();

	ImGui::SeparatorText("Direct connect (LAN or port forwarding)");
	ImGui::SetNextItemWidth(200.0f);
	if (ImGui::InputText("Friend's ip:port", sDirect, sizeof(sDirect), ImGuiInputTextFlags_CharsNoBlank)) {
		CVarSetString("gNetplay.Direct", sDirect);
		CVarSave();
	}
	ImGui::BeginDisabled(busy());
	if (ImGui::Button("Host")) port_lobby_connect_direct(sDirect, 1);
	ImGui::SameLine();
	if (ImGui::Button("Join")) port_lobby_connect_direct(sDirect, 0);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("Local UDP port: %d", CVarGetInteger("gNetplay.Port", 7000));

	ImGui::SeparatorText("Status");
	ImGui::TextWrapped("%s", port_lobby_status());
	if (port_lobby_state() == PORT_LOBBY_POST_MATCH) {
		if (port_lobby_peer_wants_rematch()) {
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Your opponent wants a rematch!");
		}
		if (ImGui::Button("Rematch")) port_lobby_request_rematch();
		ImGui::SameLine();
		if (ImGui::Button("Leave")) port_lobby_cancel();
	}
	if (port_lobby_public_addr()[0] != '\0') {
		ImGui::TextDisabled("Public endpoint: %s", port_lobby_public_addr());
	}
}
