// ---------------------------------------------------------------------------
// Used lot: car status box next to the sell price (Forza-4-Evolution).
//
// A small ImGui box in the dark space under the price on the buy / used dealership screen (about where "SELL PRICE
// 18,000 CR" sits in the header) for the HIGHLIGHTED listing: mileage, owners, condition. Drawn from the Evolution HUD's
// OnDraw (one call: UsedLotOverlayDraw(io)), so it needs no new hook.
//
//   * Which car is highlighted: BuyStageHighlightedCar() (buy_stage.cpp remembers it from the hook at 0x824FC6D0;
//     it only tracks while the Buy Cars 3D stage is on). 0 = unknown, the box stays hidden.
//   * Shown only while the used dealership list is open (GarageUsedLotActive()).
//   * Before purchase the build kind (swapped down / broken engine) stays hidden, like the PI and class. The switch
//     "reveal condition" shows it. After purchase the garage shows the car as it is.
//   * Owners: the same estimate used_lot.cpp writes when the car is bought (1 + km / 60,000, one more for a swapped or
//     broken build).
// Not built yet: needs BuyStageHighlightedCar() in buy_stage.cpp and UsedLotKindForCar() in used_lot.cpp (see the note to
// the coordinator), the one-line call from the HUD, and the switch rows.
// Switches: fm4_used_lot_overlay (default on), fm4_used_lot_reveal (default off); env FM4_USED_LOT_OVERLAY=0 hides it.
// ---------------------------------------------------------------------------
#include <imgui.h>
#include <rex/cvar.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

REXCVAR_DEFINE_BOOL(fm4_used_lot_overlay, true, "Forza Motorsport 4/Evolution",
                    "Used lot: status box (mileage, owners, condition) next to the price for the highlighted car");
REXCVAR_DEFINE_BOOL(fm4_used_lot_reveal, false, "Forza Motorsport 4/Evolution",
                    "Used lot: show the hidden condition (swapped or broken engine) before buying");

int UsedLotCount();
bool UsedLotGet(int index, uint32_t* car_id, uint32_t* price, uint32_t* mileage_km, bool* condition_unknown,
                uint32_t* thumb_car_id);
int UsedLotKindForCar(uint32_t car_id);      // used_lot.cpp: build kind 0..5, -1 unknown
uint32_t BuyStageHighlightedCar();            // buy_stage.cpp: highlighted car id while the list is open, else 0
bool GarageUsedLotActive();                   // garage_menu.cpp: the used dealership list is open

namespace {

bool EnvOn() {
  static const bool on = [] {
    const char* v = std::getenv("FM4_USED_LOT_OVERLAY");
    return !(v && v[0] == '0');
  }();
  return on;
}

std::string Thousands(uint32_t n) {
  std::string s = std::to_string(n), out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i && (s.size() - i) % 3 == 0) out += ',';
    out += s[i];
  }
  return out;
}

const char* ConditionText(int kind) {
  switch (kind) {
    case 0: return "Original engine";
    case 1: return "Original engine, street tuned";
    case 2: return "Upgraded";
    case 3: return "Wild build";
    case 4: return "Engine has been swapped";
    case 5: return "Engine pulled and broken";
    default: return "Condition unknown until bought";
  }
}

}  // namespace

bool UsedLotOverlayEnabled() { return REXCVAR_GET(fm4_used_lot_overlay); }
void UsedLotOverlaySetEnabled(bool on) { REXCVAR_SET(fm4_used_lot_overlay, on); }
bool UsedLotRevealEnabled() { return REXCVAR_GET(fm4_used_lot_reveal); }
void UsedLotRevealSetEnabled(bool on) { REXCVAR_SET(fm4_used_lot_reveal, on); }

void UsedLotOverlayDraw(ImGuiIO& io) {
  if (!EnvOn() || !REXCVAR_GET(fm4_used_lot_overlay) || !GarageUsedLotActive()) return;
  const uint32_t car = BuyStageHighlightedCar();
  if (!car) return;
  uint32_t km = 0;
  bool found = false;
  for (int i = 0, n = UsedLotCount(); i < n && !found; ++i) {
    uint32_t id = 0;
    if (UsedLotGet(i, &id, nullptr, &km, nullptr, nullptr) && id == car) found = true;
  }
  if (!found) return;
  const int kind = UsedLotKindForCar(car);
  const bool reveal = REXCVAR_GET(fm4_used_lot_reveal);
  const bool bad = kind == 4 || kind == 5;
  const uint32_t owners = 1 + km / 60000 + (bad ? 1 : 0);

  const float scale = std::max(0.6f, io.DisplaySize.y / 1080.0f);
  // Under the red price box of the header (about x 78%..100%, y 13.5% of the screen at 1920x1080 layout).
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.7785f, io.DisplaySize.y * 0.138f), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.2215f, 0.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.0f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
  if (!ImGui::Begin("##fm4_used_lot_status", nullptr, flags)) { ImGui::End(); return; }
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f * scale);
  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.8f, 1.0f), "%s km   |   %u owner%s", Thousands(km).c_str(), owners, owners == 1 ? "" : "s");
  if (reveal && kind >= 0)
    ImGui::TextColored(bad ? ImVec4(1.0f, 0.55f, 0.35f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", ConditionText(kind));
  else
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", ConditionText(-1));
  ImGui::PopFont();
  ImGui::End();
}
