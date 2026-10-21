// ---------------------------------------------------------------------------
// Used lot: mileage in the listing name (Forza-4-Evolution). Experiment, see the note below.
//
// The buy list takes each row's name from the DisplayName column of its query (a string-table key like
// '_&3444142413'). The database only holds the key, so the words cannot be extended in SQL. This file gives the
// garage query a replacement column that carries the words themselves, "<year> <make> <name> - 61,000 km", from an
// English name list generated offline (used_lot_names.inc, tools: _claude_stage/shop-names/gen_car_names.py).
//
//   std::string UsedLotNameColumnSql();   // "CASE Garage.Id WHEN 336 THEN '...' ... END AS DisplayName," or "" (off / no lot)
//
// garage_menu.cpp puts the result into the select head next to the "0 AS PerformanceIndex, 0 AS ClassID," trick.
// UNTESTED: whether the screen shows a literal string (and which of two DisplayName columns it reads). English only.
// Off by default (the status box in used_lot_overlay.cpp shows the kilometres); on: fm4_used_lot_mileage_names true. FM4_USED_LOT_NAMES=0 forces off. Cars without a name in the list keep their own name.
// ---------------------------------------------------------------------------
#include <rex/cvar.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>

REXCVAR_DEFINE_BOOL(fm4_used_lot_mileage_names, false, "Forza Motorsport 4/Evolution",
                    "Used lot: show the mileage in the listing name (English; next list open)");

int UsedLotCount();
bool UsedLotGet(int index, uint32_t* car_id, uint32_t* price, uint32_t* mileage_km, bool* condition_unknown,
                uint32_t* thumb_car_id);

namespace {

const std::unordered_map<uint32_t, const char*>& Names() {
  static const std::unordered_map<uint32_t, const char*> names = {
#include "used_lot_names.inc"
  };
  return names;
}

bool NamesOn() {
  static const bool env_on = [] {
    const char* v = std::getenv("FM4_USED_LOT_NAMES");
    return !(v && v[0] == '0');
  }();
  return env_on && REXCVAR_GET(fm4_used_lot_mileage_names);
}

std::string Thousands(uint32_t n) {
  std::string s = std::to_string(n), out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i && (s.size() - i) % 3 == 0) out += ',';
    out += s[i];
  }
  return out;
}

}  // namespace

std::string UsedLotNameColumnSql() {
  if (!NamesOn()) return {};
  std::string whens;
  for (int i = 0, n = UsedLotCount(); i < n; ++i) {
    uint32_t id = 0, km = 0;
    if (!UsedLotGet(i, &id, nullptr, &km, nullptr, nullptr)) continue;
    const auto it = Names().find(id);
    if (it == Names().end()) continue;
    std::string text = std::string(it->second) + " - " + Thousands(km) + " km", esc;
    for (char c : text) {
      if (c == '\'') esc += '\'';
      esc += c;
    }
    whens += " WHEN " + std::to_string(id) + " THEN '" + esc + "'";
  }
  if (whens.empty()) return {};
  return "CASE Garage.Id" + whens + " ELSE Garage.DisplayName END AS DisplayName,";
}

bool UsedLotMileageNamesEnabled() { return REXCVAR_GET(fm4_used_lot_mileage_names); }
void UsedLotMileageNamesSetEnabled(bool on) { REXCVAR_SET(fm4_used_lot_mileage_names, on); }
