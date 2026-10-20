// ---------------------------------------------------------------------------
// Part tuning character and engine wear (Forza-4-Evolution).
// Design: evolution-research/performance-parts-tune-design-2026-10-20.md,
//         performance-parts-wear-addendum-2026-10-20.md,
//         performance-parts-provider-addendum-2026-10-20.md
//
// NEW FILE, NOT YET COMPILED OR BUILT. Wiring still to do (other owners' files):
//   * overheat.cpp (Reliability thread): at its substep hook multiply the engine power
//     multiplier by PartTuneTorqueMult(car_phys, rpm) and call PartTuneRegister() once at startup.
//   * mods_menu.cpp (garage thread): two rows, see the declarations at the end of this file.
//   * fm4/CMakeLists.txt and release/source-files.txt: one line each.
// No hook address is used here: this file only exposes functions.
//
// Database access: this file does not own a database layer. The caller supplies one query function
// with PartTuneSetQuery() (a row reader returning up to 16 doubles per row); until then the player
// car gets a neutral tune and zero wear. Side tables (all optional, missing table = neutral):
//   Evolution_PartTune(PartEnum, PartId, Size, LowBias, MidBias, HighBias, PeakShiftRpm)
//   NewProfile_Evolution_CarWear(GarageId, EngineWear, RebuildCount, LastRebuildKm, FaultCount)
// ---------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

using EngineReliabilityProvider = bool (*)(uint32_t car_phys, float& cooling_tier, float& wear);
void EngineReliabilitySetProvider(EngineReliabilityProvider fn);

// Row reader: runs `sql`, fills up to max_cols doubles per row (NULL -> 0), returns the number of rows read
// (at most max_rows). The caller owns the database; it must run on the game thread. Returns -1 on error.
using PartTuneQueryFn = int (*)(const char* sql, double* out, int max_rows, int max_cols);

namespace {

std::atomic<bool> g_tune_on{true};
std::atomic<bool> g_wear_on{true};
std::atomic<PartTuneQueryFn> g_query{nullptr};
std::mutex g_mu;

// Per-car cached tune, filled once when the car is first seen in a race.
struct CarTune {
  float low = 1.0f, mid = 1.0f, high = 1.0f;  // part shape multipliers over the low/mid/high third of the rev range
  float peak_shift = 0.0f;                    // fraction of the range the torque peak moves (+ up, - down)
  float mismatch = 0.0f;                      // 0 matched .. 1 badly mismatched (both over- and under-sized)
  float oversize = 0.0f;                      // signed: >0 parts too big for the engine, <0 too small
  float wear = 0.0f;                          // 0 fresh .. 1 worn
  float cooling = 0.0f;
  float redline_rpm = 7000.0f;
};
std::unordered_map<uint32_t, CarTune> g_cars;
uint32_t g_player_car = 0;
int g_garage_id = 0;                  // Career_Garage.Id of the player's car (0 = unknown -> neutral)
char g_profile_prefix[32] = "";      // profile table prefix as the caller's Db class uses it (e.g. "" or "NewProfile_")

float Clamp(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }
float Smooth(float t) { t = Clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

// Wear 0..1 from the game's own per-car counters (units of distance still unverified: km assumed).
float WearFrom(double dist_km, double races, double owners, double repairs) {
  double x = dist_km / 150000.0 + races / 400.0 + 0.05 * std::max(0.0, owners - 1.0) + 0.03 * repairs;
  return Clamp(static_cast<float>(1.0 - std::exp(-x)), 0.0f, 1.0f);
}

// Mismatch rule (design section 2): a smooth function of the size ratio of the fitted parts to the engine.
// ratio = part_size / engine_size; 1.0 is ideal, band +-25 percent costs nothing.
void ApplyMismatch(CarTune& t, float ratio) {
  float over = Smooth((ratio - 1.25f) / 0.75f);   // oversize: low and mid drop, small top gain
  float under = Smooth((0.80f - ratio) / 0.50f);  // undersize: top chokes, peak moves down
  t.oversize = (ratio > 1.0f) ? over : -under;
  t.low *= 1.0f - 0.55f * over;                   // capped: a bad tune, never a broken car
  t.mid *= 1.0f - 0.20f * over;
  t.high *= 1.0f + 0.06f * over - 0.30f * under;
  t.peak_shift -= 0.12f * under;
  t.mismatch = std::max(over, under);
  if (ratio > 0.9f && ratio < 1.15f) { t.mid *= 1.02f; t.high *= 1.02f; }  // small matched-kit bonus
}

// Provider registered with overheat.cpp. Player car only; AI cars return false (defaults).
bool Provider(uint32_t car_phys, float& cooling_tier, float& wear) {
  PartTuneQueryFn q = g_query.load();
  if (!q || g_player_car != car_phys) return false;
  double row[16] = {};
  // One row for the player's garage car: cooling tier, wear inputs, then the part shape columns.
  // Column order (index): 0 cooling Level, 1 DistanceDriven (km assumed), 2 CurOwnerNumRaces, 3 NumOwners,
  // 4 TotalRepairs, 5 size ratio, 6..8 low/mid/high bias, 9 redline rpm. g_garage_id is the Career_Garage.Id;
  // whether Main/CareerCar is a garage Id or a car Id is unverified, see PartTuneSetPlayerGarage.
  // Size ratio and biases come from Evolution_PartTune joined on the fitted camshaft and fuel system rows;
  // a missing table or row gives NULL -> 0 -> neutral (the code above treats 0 as 1.0).
  char sql[1024];
  std::snprintf(sql, sizeof sql,
      "SELECT COALESCE(o.Level,0), g.DistanceDriven, g.CurOwnerNumRaces, g.NumOwners, g.TotalRepairs, "
      "COALESCE(f.Size,0) / MAX(COALESCE(ca.Size,0),0.0001), "
      "COALESCE(f.LowBias,0), COALESCE(f.MidBias,0), COALESCE(f.HighBias,0), cam.RedlineRPM "
      "FROM %sCareer_Garage g "
      "LEFT JOIN List_UpgradeEngineOilCooling o ON o.Id = g.OilCooling "
      "LEFT JOIN List_UpgradeEngineCamshaft cam ON cam.Id = g.Camshaft "
      "LEFT JOIN Evolution_PartTune f ON f.PartEnum = 16 AND f.PartId = g.FuelSystem "
      "LEFT JOIN Evolution_PartTune ca ON ca.PartEnum = 12 AND ca.PartId = g.Camshaft "
      "WHERE g.Id = %d",
      g_profile_prefix, g_garage_id);
  if (g_garage_id <= 0 || q(sql, row, 1, 16) < 1) return false;
  CarTune t;
  t.cooling = static_cast<float>(row[0]);
  t.wear = WearFrom(row[1], row[2], row[3], row[4]);
  float ratio = row[5] > 0.0 ? static_cast<float>(row[5]) : 1.0f;
  ApplyMismatch(t, ratio);
  t.low *= row[6] > 0.0 ? static_cast<float>(row[6]) : 1.0f;
  t.mid *= row[7] > 0.0 ? static_cast<float>(row[7]) : 1.0f;
  t.high *= row[8] > 0.0 ? static_cast<float>(row[8]) : 1.0f;
  if (row[9] > 1000.0) t.redline_rpm = static_cast<float>(row[9]);
  { std::lock_guard<std::mutex> lk(g_mu); g_cars[car_phys] = t; }
  cooling_tier = t.cooling;
  wear = g_wear_on.load() ? t.wear : 0.0f;
  return true;
}

}  // namespace

// ---- exposed API ----------------------------------------------------------------------------------------

void PartTuneSetQuery(PartTuneQueryFn fn) { g_query.store(fn); }
void PartTuneSetPlayerCar(uint32_t car_phys) { g_player_car = car_phys; }
// The caller (the code that knows Main/CareerCar) supplies the garage id and the profile table prefix.
void PartTuneSetPlayerGarage(int garage_id, const char* profile_prefix) {
  g_garage_id = garage_id;
  std::snprintf(g_profile_prefix, sizeof g_profile_prefix, "%s", profile_prefix ? profile_prefix : "");
}
void PartTuneRegister() { EngineReliabilitySetProvider(&Provider); }

// Multiplier for the engine power at this rpm. 1.0 when the feature is off or the car has no cached tune.
// Cheap: one map lookup and a few multiplications; safe to call every substep.
float PartTuneTorqueMult(uint32_t car_phys, float rpm) {
  if (!g_tune_on.load() && !g_wear_on.load()) return 1.0f;
  CarTune t;
  { std::lock_guard<std::mutex> lk(g_mu); auto it = g_cars.find(car_phys); if (it == g_cars.end()) return 1.0f; t = it->second; }
  float x = Clamp(rpm / std::max(1000.0f, t.redline_rpm) - t.peak_shift, 0.0f, 1.2f);  // position in the rev range
  float m = 1.0f;
  if (g_tune_on.load()) {
    float wl = Smooth(1.0f - x / 0.45f), wh = Smooth((x - 0.55f) / 0.45f), wm = 1.0f - wl - wh;  // low/mid/high blend
    m = std::max(0.0f, wl) * t.low + std::max(0.0f, wm) * t.mid + std::max(0.0f, wh) * t.high;
  }
  if (g_wear_on.load()) {
    // wear: a few percent off the peak, more at the top, plus rough low rpm
    m *= 1.0f - t.wear * (0.03f + 0.08f * Smooth((x - 0.5f) / 0.5f) + 0.04f * Smooth(1.0f - x / 0.2f));
  }
  return Clamp(m, 0.2f, 1.25f);
}

void PartTuneForgetCar(uint32_t car_phys) { std::lock_guard<std::mutex> lk(g_mu); g_cars.erase(car_phys); }

// ---- Options / F4 toggles (mods_menu.cpp declares these) -----------------------------------------------
// Rows for the Features tab (garage thread wires them):
//   "Part tuning character"  bool PartTuneEnabled(); void PartTuneSetEnabled(bool);   default on
//   "Engine wear"            bool PartWearEnabled(); void PartWearSetEnabled(bool);   default on
bool PartTuneEnabled() { return g_tune_on.load(); }
void PartTuneSetEnabled(bool on) { g_tune_on.store(on); }
bool PartWearEnabled() { return g_wear_on.load(); }
void PartWearSetEnabled(bool on) { g_wear_on.store(on); }
