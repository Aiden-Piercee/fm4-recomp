// ---------------------------------------------------------------------------
// Engine builder: player car state, saved builds and the physics multiplier (Forza-4-Evolution).
// Design: docs/ENGINE_BUILDER.md. Overlay: builder_ui.cpp.
//
// Data flow:
//   * part_tune.cpp resolves the player's garage car (Main/CareerCar -> Career_Garage.Id) and calls
//     EngineBuilderSetGarage. A frame callback (game thread) then reads, through the Evolution database helper:
//       - the fitted engine (List_UpgradeEngine.EngineID via Career_Garage.Engine), the car's year, the stock and
//         fitted camshaft rows (redline, end of the torque curve) and the product of the fitted FM4 engine upgrade
//         TorqueScales, which the builder divides out so FM4 levels and builder parts never stack;
//       - the build: {P}Evolution_EngineBuild(GarageId, Slot, PartId), created on first use.
//   * Whenever the car or the build changes, the stock and built engines are run through the model (dyno.cpp) and
//     a ratio table build/stock is stored. The engine substep hook (overheat.cpp) multiplies the engine power
//     multiplier by EngineBuilderTorqueRatio and raises the rev limiter with EngineBuilderLimiter.
//   * PI: FM4's PI does not know about builds, so a build only applies outside career events unless the switch
//     fm4_engine_builder_career is on. A race counts as a career event when the event id race start stores at
//     0x82DDB228 is a row of Events (unverified for free play after a career race: the word may keep the old id).
//
// Switches: fm4_engine_builder (default on), fm4_engine_builder_career (default off). FM4_ENGINE_BUILDER=0 or
// FM4_EVO=0 turns the whole feature off. Log lines start with "Engine builder:".
// ---------------------------------------------------------------------------
#include "engine_builder.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(fm4_engine_builder, true, "Forza Motorsport 4/Evolution",
                    "Engine builder: parts-based engines (Insert overlay). Off = every car drives as stock FM4");
REXCVAR_DEFINE_BOOL(fm4_engine_builder_career, false, "Forza Motorsport 4/Evolution",
                    "Engine builder: also apply builds in career events (PI and class do not account for builds yet)");

// dev_keys.cpp (Evolution database helper and frame hooks).
void EvoDbQueue(const char* sql);
void EvoDbRead(const char* sql, const char* const* column_names, int count, void (*cb)(const int32_t* values, int count));
bool EvoProfileReady();
uint32_t EvoProfileGeneration();
void EvoOnFrame(void (*cb)());
int EvoNewestUnlockedDecadeYear();

namespace eb {
namespace {

constexpr uint32_t kCurrentEventIdAddr = 0x82DDB228;  // written at race start (sub_82492210), see hooks.cpp
constexpr float kRadPerRpm = 0.10471976f;
constexpr float kRatioMin = 0.35f, kRatioMax = 3.2f;  // a blown or nitrous build can triple the stock torque

std::mutex g_mu;
Catalogue g_cat;
bool g_cat_loaded = false;
std::string g_cat_source = "not loaded";
CarState g_car;
LiveBuild g_live;

std::atomic<int> g_want_garage{0};     // garage the player is in (from part_tune.cpp)
std::atomic<uint32_t> g_player{0};     // player car physics object (from overheat.cpp)
std::atomic<bool> g_race_started{false};
std::atomic<int> g_career{-1};         // -1 unknown, 0 not a career event, 1 career event
int g_loaded_garage = 0;               // game thread only
uint32_t g_loaded_gen = 0;
int g_pending_garage = 0;              // garage the outstanding reads are for
bool g_registered = false;

// Rev limiter bookkeeping (physics thread).
uint32_t g_lim_car = 0;
float g_lim_stock = 0.0f, g_lim_written = 0.0f;

bool EnvOn() {
  static const bool on = [] {
    const char* evo = std::getenv("FM4_EVO");
    const char* v = std::getenv("FM4_ENGINE_BUILDER");
    return !(evo && evo[0] == '0') && !(v && v[0] == '0');
  }();
  return on;
}

uint32_t LoadBE32(uint32_t a) {
  const uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(a);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// The parts the model uses: the fitted parts, minus the blower when an FM4 shop turbo / supercharger is fitted (FM4
// applies its own forced-induction model on top of the curve, so a builder blower would boost twice).
void EffectiveParts(const int32_t (&in)[kNumSlots], int32_t (&out)[kNumSlots]) {
  std::memcpy(out, in, sizeof out);
  if (g_car.fm4_fi_upgraded) out[kBlowerSlot] = 0;
}

// Rebuilds g_live from g_car. Caller holds g_mu.
void RebuildLocked() {
  LiveBuild live;
  const StockEngine* e = g_cat.FindEngine(g_car.engine_id);
  bool any = false;
  for (int s = 0; s < kNumSlots; ++s) any |= g_car.parts[s] > 0;
  if (!e || !g_car.info_ok || !g_car.build_ok || !any) { g_live = live; return; }
  live.rpm_max = std::max(10000.0, g_car.curve_max_rpm * 1.1);
  const DynoResult stock = RunDyno(StockSpec(*e), live.rpm_max);
  int32_t parts[kNumSlots];
  EffectiveParts(g_car.parts, parts);
  const DynoResult built = RunDyno(BuildSpec(g_cat, *e, parts), live.rpm_max);
  const double scale = g_car.fm4_upgrade_scale > 0.05 ? g_car.fm4_upgrade_scale : 1.0;
  // Below about 600 rpm the stock torque is tiny and the ratio meaningless: hold the first usable ratio.
  int first = 0;
  while (first < LiveBuild::kPoints - 1 && (stock.rpm_at(first) < 600.0 || stock.torque_nm[first] < 0.05 * stock.peak_torque_nm)) ++first;
  for (int i = 0; i < LiveBuild::kPoints; ++i) {
    const int j = std::max(i, first);
    const double st = stock.torque_nm[j];
    double r = st > 1e-3 ? built.torque_nm[j] / st : 1.0;
    if (!std::isfinite(r)) r = 1.0;
    live.ratio[i] = std::clamp(float(r / scale), kRatioMin, kRatioMax);
  }
  // Rev limit: move the FM4 redline by as much as the build's safe rpm exceeds it, within the stock curve's range.
  const double cap = g_car.curve_max_rpm > 1000.0 ? g_car.curve_max_rpm - 150.0 : 0.0;
  const double want = std::min(built.safe_rpm - 100.0, cap);
  if (g_car.fitted_redline > 1000.0 && want > g_car.fitted_redline + 50.0) live.limiter_rpm = float(want - g_car.fitted_redline);
  live.active = true;
  g_live = live;
  REXLOG_INFO("Engine builder: garage {} engine {} build active, stock {:.0f} Nm @ {:.0f} / {:.0f} kW @ {:.0f} -> "
              "{:.0f} Nm @ {:.0f} / {:.0f} kW @ {:.0f}, safe {:.0f} rpm, redline +{:.0f}, FM4 upgrade scale {:.3f}{}",
              g_car.garage_id, g_car.engine_id, stock.peak_torque_nm, stock.peak_torque_rpm, stock.peak_power_kw,
              stock.peak_power_rpm, built.peak_torque_nm, built.peak_torque_rpm, built.peak_power_kw, built.peak_power_rpm,
              built.safe_rpm, live.limiter_rpm, scale, g_car.fm4_cam_upgraded ? " (FM4 cam upgrade fitted: its shape stays)" : "");
  if (g_car.fm4_fi_upgraded && g_car.parts[kBlowerSlot] > 0)
    REXLOG_INFO("Engine builder: garage {}: FM4 shop forced induction fitted, the builder blower is ignored", g_car.garage_id);
}

// ---- database reads (game thread callbacks) -------------------------------------------------------------------------
constexpr const char* kInfoCols[] = {"EID", "CAR", "YR", "SRED", "FRED", "TCMAX", "SC", "CAMUP"};
constexpr int kNumInfoCols = 8;
constexpr const char* kBuildCols[] = {"S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9", "S10", "S11"};
static_assert(sizeof(kBuildCols) / sizeof(kBuildCols[0]) == kNumSlots);

void OnInfo(const int32_t* v, int n) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_car.garage_id != g_pending_garage) return;
  if (!v || n < kNumInfoCols) {
    g_car.info_ok = false;
    REXLOG_WARN("Engine builder: garage {}: engine row not read", g_car.garage_id);
    RebuildLocked();
    return;
  }
  g_car.engine_id = v[0];
  g_car.car_id = v[1];
  g_car.year = v[2];
  g_car.stock_redline = v[3];
  g_car.fitted_redline = v[4];
  g_car.curve_max_rpm = v[5];
  g_car.fm4_upgrade_scale = v[6] > 0 ? v[6] / 1000000.0 : 1.0;
  g_car.fm4_cam_upgraded = v[7] != 0;
  g_car.info_ok = true;
  const StockEngine* e = g_cat.FindEngine(g_car.engine_id);
  REXLOG_INFO("Engine builder: garage {} car {} ({}) engine {} {}; redline {} / {} rpm, curve to {} rpm, FM4 upgrades x{:.3f}",
              g_car.garage_id, g_car.car_id, g_car.year, g_car.engine_id, e ? e->name : std::string("(not in the catalogue)"),
              v[3], v[4], v[5], g_car.fm4_upgrade_scale);
  RebuildLocked();
}

void OnBuild(const int32_t* v, int n) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_car.garage_id != g_pending_garage) return;
  for (int s = 0; s < kNumSlots; ++s) g_car.parts[s] = (v && n == kNumSlots && v[s] > 0) ? v[s] : 0;
  g_car.build_ok = true;  // no row = no build, which is a valid answer
  RebuildLocked();
}

// FM4 shop forced induction (non-stock turbo or supercharger rows). A failed read (column names differ) leaves it off.
void OnForced(const int32_t* v, int n) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_car.garage_id != g_pending_garage) return;
  if (!v || n < 1) {
    REXLOG_WARN("Engine builder: garage {}: FM4 forced-induction row not read (assumed none)", g_car.garage_id);
    return;
  }
  g_car.fm4_fi_upgraded = v[0] != 0;
  if (g_car.fm4_fi_upgraded) REXLOG_INFO("Engine builder: garage {}: FM4 shop turbo / supercharger fitted", g_car.garage_id);
  RebuildLocked();
}

void OnEventCheck(const int32_t* v, int n) {
  g_career = (v && n > 0 && v[0] > 0) ? 1 : 0;
  REXLOG_INFO("Engine builder: race is {} (build {})", g_career.load() ? "a career event" : "not a career event",
              (g_career.load() && !REXCVAR_GET(fm4_engine_builder_career)) ? "off: career switch is off" : "applies");
}

void QueueReads(int garage) {
  const std::string gid = std::to_string(garage);
  EvoDbQueue("CREATE TABLE IF NOT EXISTS {P}Evolution_EngineBuild (GarageId INTEGER, Slot INTEGER, PartId INTEGER, "
             "PRIMARY KEY (GarageId, Slot))");
  // Product of the fitted FM4 engine upgrade TorqueScales (x 1e6), and the camshaft peak ratio fitted / stock.
  const std::string info =
      "SELECT u.EngineID AS EID, g.CarId AS CAR, COALESCE(dc.Year,0) AS YR, "
      "CAST(COALESCE(st.RedlineRPM,0) AS INTEGER) AS SRED, CAST(COALESCE(cs.RedlineRPM,st.RedlineRPM,0) AS INTEGER) AS FRED, "
      "CAST(COALESCE(cs.TorqueCurveMaxRPM,st.TorqueCurveMaxRPM,0) AS INTEGER) AS TCMAX, "
      "CAST(1000000.0*COALESCE(v.TorqueScale,1)*COALESCE(d.TorqueScale,1)*COALESCE(p.TorqueScale,1)*COALESCE(f.TorqueScale,1)"
      "*COALESCE(i.TorqueScale,1)*COALESCE(x.TorqueScale,1)*COALESCE(n.TorqueScale,1)*COALESCE(m.TorqueScale,1)"
      "*COALESCE(tcf.TorqueScale/NULLIF(tcs.TorqueScale,0),1) AS INTEGER) AS SC, "
      "CASE WHEN cs.Id IS NOT NULL AND st.Id IS NOT NULL AND cs.Id<>st.Id THEN 1 ELSE 0 END AS CAMUP "
      "FROM {P}Career_Garage g JOIN List_UpgradeEngine u ON u.Id=g.Engine "
      "LEFT JOIN Data_Car dc ON dc.Id=g.CarId "
      "LEFT JOIN List_UpgradeEngineCamshaft st ON st.EngineID=u.EngineID AND st.IsStock=1 "
      "LEFT JOIN List_UpgradeEngineCamshaft cs ON cs.Id=g.Camshaft "
      "LEFT JOIN List_TorqueCurve tcs ON tcs.TorqueCurveID=st.TorqueCurveFullThrottleID "
      "LEFT JOIN List_TorqueCurve tcf ON tcf.TorqueCurveID=cs.TorqueCurveFullThrottleID "
      "LEFT JOIN List_UpgradeEngineValves v ON v.Id=g.Valves "
      "LEFT JOIN List_UpgradeEngineDisplacement d ON d.Id=g.Displacement "
      "LEFT JOIN List_UpgradeEnginePistonsCompression p ON p.Id=g.PistonsCompression "
      "LEFT JOIN List_UpgradeEngineFuelSystem f ON f.Id=g.FuelSystem "
      "LEFT JOIN List_UpgradeEngineIgnition i ON i.Id=g.Ignition "
      "LEFT JOIN List_UpgradeEngineExhaust x ON x.Id=g.Exhaust "
      "LEFT JOIN List_UpgradeEngineIntake n ON n.Id=g.Intake "
      "LEFT JOIN List_UpgradeEngineManifold m ON m.Id=g.Manifold "
      "WHERE g.Id=" + gid + " LIMIT 1";
  EvoDbRead(info.c_str(), kInfoCols, kNumInfoCols, &OnInfo);
  std::string build = "SELECT ";
  for (int s = 0; s < kNumSlots; ++s)
    build += std::string(s ? ", " : "") + "COALESCE(MAX(CASE WHEN Slot=" + std::to_string(s) + " THEN PartId END),0) AS S" +
             std::to_string(s);
  build += " FROM {P}Evolution_EngineBuild WHERE GarageId=" + gid;
  EvoDbRead(build.c_str(), kBuildCols, kNumSlots, &OnBuild);
  // Career_Garage names its upgrade columns after the List_UpgradeEngine* tables (Camshaft, Valves, ...).
  static constexpr const char* kFiCols[] = {"FI"};
  const std::string fi =
      "SELECT CASE WHEN COALESCE(ts.IsStock,1)=0 OR COALESCE(tt.IsStock,1)=0 OR COALESCE(tq.IsStock,1)=0 "
      "OR COALESCE(ds.IsStock,1)=0 OR COALESCE(cs.IsStock,1)=0 THEN 1 ELSE 0 END AS FI FROM {P}Career_Garage g "
      "LEFT JOIN List_UpgradeEngineTurboSingle ts ON ts.Id=g.TurboSingle "
      "LEFT JOIN List_UpgradeEngineTurboTwin tt ON tt.Id=g.TurboTwin "
      "LEFT JOIN List_UpgradeEngineTurboQuad tq ON tq.Id=g.TurboQuad "
      "LEFT JOIN List_UpgradeEngineDSC ds ON ds.Id=g.DSC "
      "LEFT JOIN List_UpgradeEngineCSC cs ON cs.Id=g.CSC "
      "WHERE g.Id=" + gid + " LIMIT 1";
  EvoDbRead(fi.c_str(), kFiCols, 1, &OnForced);
}

// Game thread, once per frame.
void Frame() {
  if (!EnvOn() || !EvoProfileReady()) return;
  const int want = g_want_garage.load();
  const uint32_t gen = EvoProfileGeneration();
  if (want > 0 && (want != g_loaded_garage || gen != g_loaded_gen)) {
    g_loaded_garage = want;
    g_loaded_gen = gen;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      g_car = CarState();
      g_car.garage_id = want;
      g_live = LiveBuild();
      g_pending_garage = want;
    }
    QueueReads(want);
  }
  if (g_race_started.exchange(false)) {
    const uint32_t ev = LoadBE32(kCurrentEventIdAddr);
    if (ev == 0 || ev > 100000) {
      g_career = 0;
      REXLOG_INFO("Engine builder: race start, no career event id ({})", ev);
    } else {
      g_career = -1;
      static constexpr const char* kCols[] = {"N"};
      EvoDbRead(("SELECT COUNT(*) AS N FROM Events WHERE Id=" + std::to_string(ev)).c_str(), kCols, 1, &OnEventCheck);
    }
  }
}

void EnsureRegistered() {
  if (g_registered) return;
  g_registered = true;
  EvoOnFrame(&Frame);
}

}  // namespace

// ---- catalogue ------------------------------------------------------------------------------------------------------
// Looks for fm4_engine_parts.tsv: FM4_ENGINE_PARTS (a path), the user data root, the working directory, and
// assets/engine_builder/ in the working directory and up to four parents (a source checkout).
void LoadCatalogueOnce(const std::filesystem::path& user_root) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_cat_loaded) return;
  g_cat_loaded = true;
  std::vector<std::filesystem::path> candidates;
  if (const char* p = std::getenv("FM4_ENGINE_PARTS"); p && *p) candidates.emplace_back(p);
  if (!user_root.empty()) candidates.push_back(user_root / "fm4_engine_parts.tsv");
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::current_path(ec);
  candidates.push_back(dir / "fm4_engine_parts.tsv");
  for (int up = 0; up < 5 && !dir.empty(); ++up) {
    candidates.push_back(dir / "assets" / "engine_builder" / "fm4_engine_parts.tsv");
    const auto parent = dir.parent_path();
    if (parent == dir) break;
    dir = parent;
  }
  for (const auto& c : candidates) {
    Catalogue cat;
    if (!LoadCatalogue(c.string(), cat)) continue;
    for (const auto& err : cat.errors) REXLOG_WARN("Engine builder: {}", err);
    REXLOG_INFO("Engine builder: catalogue {} ({} families, {} engines, {} parts, {} bad rows)", c.string(),
                cat.families.size(), cat.engines.size(), cat.parts.size(), cat.errors.size());
    g_cat = std::move(cat);
    g_cat_source = c.string();
    return;
  }
  g_cat_source = "fm4_engine_parts.tsv not found (copy assets/engine_builder/fm4_engine_parts.tsv next to fm4.exe)";
  REXLOG_WARN("Engine builder: {}", g_cat_source);
}

const Catalogue* GetCatalogue() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_cat_loaded && !g_cat.engines.empty() ? &g_cat : nullptr;  // never modified after loading
}
std::string CatalogueSource() { std::lock_guard<std::mutex> lock(g_mu); return g_cat_source; }
CarState Snapshot() { std::lock_guard<std::mutex> lock(g_mu); return g_car; }
bool BuilderEnabled() { return EnvOn() && REXCVAR_GET(fm4_engine_builder); }
bool CareerAllowed() { return REXCVAR_GET(fm4_engine_builder_career); }
void SetCareerAllowed(bool on) { REXCVAR_SET(fm4_engine_builder_career, on); }
bool LastRaceWasCareer() { return g_career.load() != 0; }
int DecadeYearLimit() { return EnvOn() ? EvoNewestUnlockedDecadeYear() + 9 : 0; }

void FitPart(int slot, int32_t part_id) {
  if (slot < 0 || slot >= kNumSlots) return;
  int garage = 0;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_car.build_ok || g_car.garage_id <= 0) return;
    garage = g_car.garage_id;
    g_car.parts[slot] = part_id > 0 ? part_id : 0;
    RebuildLocked();
  }
  const std::string gid = std::to_string(garage), s = std::to_string(slot);
  if (part_id > 0)
    EvoDbQueue(("INSERT OR REPLACE INTO {P}Evolution_EngineBuild (GarageId, Slot, PartId) VALUES (" + gid + ", " + s +
                ", " + std::to_string(part_id) + ")").c_str());
  else
    EvoDbQueue(("DELETE FROM {P}Evolution_EngineBuild WHERE GarageId=" + gid + " AND Slot=" + s).c_str());
  REXLOG_INFO("Engine builder: garage {} slot {} -> part {}", garage, SlotKey(slot), part_id);
}

void ClearBuild() {
  int garage = 0;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_car.build_ok || g_car.garage_id <= 0) return;
    garage = g_car.garage_id;
    for (auto& p : g_car.parts) p = 0;
    RebuildLocked();
  }
  EvoDbQueue(("DELETE FROM {P}Evolution_EngineBuild WHERE GarageId=" + std::to_string(garage)).c_str());
  REXLOG_INFO("Engine builder: garage {} build cleared", garage);
}

DynoResult RunFor(const StockEngine& e, const int32_t (*parts)[kNumSlots]) {
  std::lock_guard<std::mutex> lock(g_mu);
  const double rpm_max = std::max(10000.0, g_car.curve_max_rpm * 1.1);
  int32_t eff[kNumSlots];
  EffectiveParts(parts ? *parts : g_car.parts, eff);
  return RunDyno(BuildSpec(g_cat, e, eff), rpm_max);
}

}  // namespace eb

// ---- entry points ---------------------------------------------------------------------------------------------------
void EngineBuilderSetGarage(int garage_id) {
  if (!eb::EnvOn()) return;
  eb::EnsureRegistered();
  eb::g_want_garage = garage_id;
}

void EngineBuilderSetPlayerCar(uint32_t car_phys) {
  if (!eb::EnvOn()) return;
  eb::EnsureRegistered();
  eb::g_player = car_phys;
  eb::g_race_started = true;
}

// True when the player car has a build that applies in the current event (same gate as the torque ratio).
bool EngineBuilderActive(uint32_t car_phys) {
  if (car_phys != eb::g_player.load() || !eb::BuilderEnabled()) return false;
  const int career = eb::g_career.load();
  if (career < 0 || (career > 0 && !eb::CareerAllowed())) return false;
  std::lock_guard<std::mutex> lock(eb::g_mu);
  return eb::g_live.active && eb::g_live.rpm_max > 0.0;
}

float EngineBuilderTorqueRatio(uint32_t car_phys, float rpm) {
  if (!EngineBuilderActive(car_phys)) return 1.0f;
  std::lock_guard<std::mutex> lock(eb::g_mu);
  const eb::LiveBuild& b = eb::g_live;
  if (!b.active || !(b.rpm_max > 0.0)) return 1.0f;
  const float x = std::clamp(rpm / float(b.rpm_max), 0.0f, 1.0f) * (eb::LiveBuild::kPoints - 1);
  const int i = std::min(int(x), eb::LiveBuild::kPoints - 2);
  const float t = x - float(i);
  return b.ratio[i] + (b.ratio[i + 1] - b.ratio[i]) * t;
}

float EngineBuilderLimiter(uint32_t car_phys, float current_rad_s) {
  if (car_phys != eb::g_player.load()) return 0.0f;
  // The limiter we see is the game's own unless it is still the value we wrote last time.
  if (car_phys != eb::g_lim_car || current_rad_s != eb::g_lim_written) {
    eb::g_lim_car = car_phys;
    eb::g_lim_stock = current_rad_s;
  }
  float raise_rpm = 0.0f;
  if (eb::BuilderEnabled()) {
    const int career = eb::g_career.load();
    if (career == 0 || (career > 0 && eb::CareerAllowed())) {
      std::lock_guard<std::mutex> lock(eb::g_mu);
      if (eb::g_live.active) raise_rpm = eb::g_live.limiter_rpm;
    }
  }
  if (!(eb::g_lim_stock > 0.0f)) return 0.0f;
  if (!(raise_rpm > 0.0f)) {
    // Nothing to raise. If we raised it before (build removed, switch turned off), put the game's value back once.
    if (eb::g_lim_written == 0.0f) return 0.0f;
    eb::g_lim_written = 0.0f;
    return eb::g_lim_stock;
  }
  eb::g_lim_written = eb::g_lim_stock + raise_rpm * eb::kRadPerRpm;
  return eb::g_lim_written;
}
