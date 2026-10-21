// Engine builder: parts catalogue parser. See catalogue.h.
#include "catalogue.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace eb {

namespace {

constexpr const char* kSlotKeys[kNumSlots] = {"heads", "cam", "valvetrain", "pistons", "rotating",
                                              "intake", "carb", "headers", "exhaust", "ignition"};
constexpr const char* kSlotNames[kNumSlots] = {"Cylinder heads", "Camshaft", "Valvetrain", "Pistons",
                                               "Crank and rods", "Intake manifold", "Carburettor / throttle",
                                               "Exhaust manifolds", "Exhaust system", "Ignition"};

std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    const size_t tab = line.find('\t', start);
    out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
    if (tab == std::string::npos) break;
    start = tab + 1;
  }
  return out;
}

bool ParseMod(const std::string& s, Mod& m) {
  const size_t eq = s.find('=');
  if (eq == std::string::npos || eq == 0) return false;
  size_t key_end = eq;
  m.op = '=';
  if (s[eq - 1] == '+' || s[eq - 1] == '*') { m.op = s[eq - 1]; key_end = eq - 1; }
  m.key = s.substr(0, key_end);
  const std::string v = s.substr(eq + 1);
  char* end = nullptr;
  m.value = std::strtod(v.c_str(), &end);
  return !m.key.empty() && end && end != v.c_str() && *end == '\0';
}

bool ParseInt(const std::string& s, int32_t& out) {
  char* end = nullptr;
  const long v = std::strtol(s.c_str(), &end, 10);
  if (!end || end == s.c_str() || *end != '\0') return false;
  out = int32_t(v);
  return true;
}

void Apply(double& field, const Mod& m) {
  if (m.op == '+') field += m.value;
  else if (m.op == '*') field *= m.value;
  else field = m.value;
}
void Apply(int& field, const Mod& m) {
  double d = field;
  Apply(d, m);
  field = int(d + (d >= 0 ? 0.5 : -0.5));
}

}  // namespace

const char* SlotKey(int slot) { return slot >= 0 && slot < kNumSlots ? kSlotKeys[slot] : "?"; }
const char* SlotName(int slot) { return slot >= 0 && slot < kNumSlots ? kSlotNames[slot] : "?"; }
int SlotFromKey(const std::string& key) {
  for (int i = 0; i < kNumSlots; ++i)
    if (key == kSlotKeys[i]) return i;
  return -1;
}

bool Part::Fits(const std::string& family) const {
  return families.empty() || std::find(families.begin(), families.end(), family) != families.end();
}

const Part* Catalogue::FindPart(int32_t id) const {
  const auto it = parts.find(id);
  return it == parts.end() ? nullptr : &it->second;
}
const StockEngine* Catalogue::FindEngine(int32_t engine_id) const {
  const auto it = engines.find(engine_id);
  return it == engines.end() ? nullptr : &it->second;
}
std::vector<const Part*> Catalogue::PartsFor(int slot, const std::string& family, int year) const {
  std::vector<const Part*> out;
  for (const auto& [id, p] : parts)
    if (p.slot == slot && p.Fits(family) && (year <= 0 || p.year <= year)) out.push_back(&p);
  std::sort(out.begin(), out.end(), [](const Part* a, const Part* b) {
    return a->year != b->year ? a->year < b->year : a->name < b->name;
  });
  return out;
}

bool ParseCatalogue(const std::string& text, const std::string& source_name, Catalogue& cat) {
  std::istringstream in(text);
  std::string line;
  int line_no = 0;
  auto bad = [&](const std::string& why) {
    cat.errors.push_back(source_name + ":" + std::to_string(line_no) + ": " + why);
  };
  while (std::getline(in, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const auto f = SplitTabs(line);
    if (f[0] == "F") {
      if (f.size() < 3) { bad("family row needs an id and a name"); continue; }
      cat.families[f[1]] = Family{f[1], f[2]};
    } else if (f[0] == "E") {
      StockEngine e;
      if (f.size() < 4 || !ParseInt(f[1], e.engine_id)) { bad("engine row needs EngineID, family, name"); continue; }
      e.family = f[2];
      e.name = f[3];
      bool ok = true;
      for (size_t i = 4; i < f.size(); ++i) {
        if (f[i].empty()) continue;
        Mod m;
        if (!ParseMod(f[i], m)) { bad("bad modifier '" + f[i] + "'"); ok = false; break; }
        e.mods.push_back(m);
      }
      if (!ok) continue;
      if (!cat.families.count(e.family)) { bad("unknown family '" + e.family + "'"); continue; }
      cat.engines[e.engine_id] = std::move(e);
    } else if (f[0] == "P") {
      Part p;
      int32_t year = 0, price = 0;
      if (f.size() < 9 || !ParseInt(f[1], p.id) || !ParseInt(f[6], year) || !ParseInt(f[7], price)) {
        bad("part row needs id, slot, families, brand, name, year, price, mass");
        continue;
      }
      p.slot = SlotFromKey(f[2]);
      if (p.slot < 0) { bad("unknown slot '" + f[2] + "'"); continue; }
      if (f[3] != "*") {
        std::string fam;
        std::istringstream fs(f[3]);
        while (std::getline(fs, fam, ',')) if (!fam.empty()) p.families.push_back(fam);
      }
      p.brand = f[4];
      p.name = f[5];
      p.year = year;
      p.price = price;
      p.mass_kg = std::strtod(f[8].c_str(), nullptr);
      bool ok = true;
      for (size_t i = 9; i < f.size(); ++i) {
        if (f[i].empty()) continue;
        Mod m;
        if (!ParseMod(f[i], m)) { bad("bad modifier '" + f[i] + "'"); ok = false; break; }
        p.mods.push_back(m);
      }
      if (!ok) continue;
      if (cat.parts.count(p.id)) { bad("duplicate part id " + std::to_string(p.id)); continue; }
      cat.parts[p.id] = std::move(p);
    } else {
      bad("unknown row type '" + f[0] + "'");
    }
  }
  return true;
}

bool LoadCatalogue(const std::string& path, Catalogue& cat) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ParseCatalogue(ss.str(), path, cat);
}

void ApplyMods(EngineSpec& s, const std::vector<Mod>& mods, int* unknown) {
  for (const Mod& m : mods) {
    const std::string& k = m.key;
    if (k == "bore") Apply(s.bore_mm, m);
    else if (k == "stroke") Apply(s.stroke_mm, m);
    else if (k == "cyl") Apply(s.cylinders, m);
    else if (k == "rod") Apply(s.rod_length_mm, m);
    else if (k == "cr") Apply(s.compression, m);
    else if (k == "rmass") Apply(s.rotating_mass_kg, m);
    else if (k == "rodrpm") Apply(s.rod_limit_rpm, m);
    else if (k == "vpc") Apply(s.valves_per_cyl, m);
    else if (k == "ivd") Apply(s.intake_valve_mm, m);
    else if (k == "evd") Apply(s.exhaust_valve_mm, m);
    else if (k == "port") Apply(s.port_flow, m);
    else if (k == "portcc") Apply(s.port_volume_cc, m);
    else if (k == "idur") Apply(s.intake_duration, m);
    else if (k == "edur") Apply(s.exhaust_duration, m);
    else if (k == "ilift") Apply(s.intake_lift_mm, m);
    else if (k == "elift") Apply(s.exhaust_lift_mm, m);
    else if (k == "lsa") Apply(s.lobe_separation, m);
    else if (k == "icl") Apply(s.intake_centerline, m);
    else if (k == "lifter") Apply(s.lifter, m);
    else if (k == "rocker") Apply(s.rocker_ratio, m);
    else if (k == "nrocker") Apply(s.nominal_rocker_ratio, m);
    else if (k == "float") Apply(s.valve_float_rpm, m);
    else if (k == "intake") Apply(s.intake, m);
    else if (k == "runner") Apply(s.runner_length_mm, m);
    else if (k == "cfm") Apply(s.carb_cfm, m);
    else if (k == "carb") Apply(s.carbureted, m);
    else if (k == "filter") Apply(s.air_filter_restriction, m);
    else if (k == "header") Apply(s.header, m);
    else if (k == "pdia") Apply(s.primary_diameter_mm, m);
    else if (k == "plen") Apply(s.primary_length_mm, m);
    else if (k == "pipe") Apply(s.pipe_diameter_mm, m);
    else if (k == "duals") Apply(s.dual_exhaust, m);
    else if (k == "muffler") Apply(s.muffler_restriction, m);
    else if (k == "ign") Apply(s.ignition, m);
    else if (k == "octane") Apply(s.fuel_octane, m);
    else if (k == "tune") Apply(s.spark_tune, m);
    else if (unknown) ++*unknown;
  }
}

EngineSpec StockSpec(const StockEngine& e) {
  EngineSpec s;
  ApplyMods(s, e.mods);
  return s;
}

EngineSpec BuildSpec(const Catalogue& cat, const StockEngine& e, const int32_t (&parts)[kNumSlots]) {
  EngineSpec s = StockSpec(e);
  // Slot order matters for '+' and '*' modifiers: heads before pistons (a piston adds compression to the head's
  // ratio), rotating before cam (a stroker sets stroke, the cam then only touches the valvetrain fields).
  for (int slot = 0; slot < kNumSlots; ++slot) {
    if (parts[slot] <= 0) continue;
    const Part* p = cat.FindPart(parts[slot]);
    if (!p || p->slot != slot || !p->Fits(e.family)) continue;
    ApplyMods(s, p->mods);
  }
  return s;
}

}  // namespace eb
