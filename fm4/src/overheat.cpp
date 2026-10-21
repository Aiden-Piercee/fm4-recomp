// ---------------------------------------------------------------------------
// Engine overheating / oil pressure failure (Forza-4-Evolution).
// Research: claude/engine-overheat-oil-failure-research-2026-10-12.md
//
// One midasm hook, OverheatEngineStep at 0x82304964 (inside sub_82304940,
// the per-car engine substep, right after sub_82304548 ran the stock
// over-rev damage and sputter code). r31 = car physics object E,
// f31 = substep dt (about 1/359 s). Runs for every car on track.
//
// Car physics object fields (big-endian floats unless noted):
//   E+2724 engine speed (rad/s)      E+3636 rev limiter (rad/s)
//   E+2732 engine damage 0..1        E+2736 engine power multiplier
//   E+2800 engine power (W)          E+3684 / E+3688 Engine ModZero / ModFull
//   E+5608 damage mode (u32, 2 = simulation, inferred)
//   E+5402 / E+5403 flags (u8, meaning unknown; logged for the probe)
// The game turns damage into power loss (mult), misfires (damage > 0.3),
// smoke, sound, AI pitting and repair cost by itself. This file only raises
// E+2732 and recomputes E+2736 the same way the game does.
//
// FM4_OVERHEAT (default 1):
//   0  off
//   1  temperatures simulated and shown on the HUD, probe log every 2 s
//      ("Overheat:" lines in fm4.log), F12 keys. Heat never damages the
//      engine; no gameplay change unless F12 is pressed.
//   2  as 1, and overheating / low oil pressure damage the engine (only when
//      damage mode is 2, unless FM4_OVERHEAT_ANYMODE=1).
// FM4_FAULTS  random mechanical faults (needs FM4_OVERHEAT=2 + simulation
//   damage): 0/off (default), 1/realistic, 2/gritty (x10), 3/chaos (x50).
//   A chance per second of running, raised by stress (hot water/oil, over-revs,
//   oil surge, existing engine damage), so short races rarely see one and long
//   races sometimes do. One fault per car per race. Faults: coolant leak,
//   oil leak, misfire, fuel pump, boost leak (turbo/supercharged cars only),
//   blown engine (rare).
// Realism (claude/engine-heat-realism-plan-2026-10-13.md):
//   track air temperature (FM4_OVERHEAT_AMBIENT=<C> overrides), drafting
//   behind another car (FM4_OVERHEAT_DRAFT=0 disables), radiator damage from
//   frontal hits (FM4_OVERHEAT_IMPACT_G, default 5 g).
// FM4_OVERHEAT_HEAT  heat-input multiplier for testing (default 1.0;
//                    2.0 makes engines overheat at race pace).
// Keys (game window focused):
//   F12        force engine damage on the HUD car, cycling
//              0.35 (misfires) -> 1.0 (blown) -> 0 (repaired) -> 0.35 ...
//   Shift+F12  the same on every car
//   Ctrl+F12   switch the HUD to the next car (player detection is a guess)
//   Ctrl+Shift+F12  give the HUD car the next fault in the list (test; works
//              with faults off too)
// HUD: a small readout above the speedometer (bottom right) while racing:
//   coolant, oil temperature, oil pressure, engine damage. FM4_OVERHEAT_HUD=0
//   hides it; FM4_OVERHEAT_HUD_POS="0.83,0.62" moves it (fractions of the
//   window, top-left corner of the box).
// ---------------------------------------------------------------------------

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma comment(lib, "user32.lib")
#endif

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>

#include <rex/ppc.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <cstring>
#include <string>
#include <mutex>
#include <random>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <vector>

using EngineReliabilityProvider = bool (*)(uint32_t car_phys, float& cooling_tier, float& wear);
void EngineReliabilitySetEnabled(bool on);
bool EngineReliabilityEnabled();
void EngineReliabilitySetProvider(EngineReliabilityProvider fn);
// part_tune.cpp (Performance Parts thread)
void PartTuneRegister();
float PartTuneTorqueMult(uint32_t car_phys, float rpm);
void PartTuneSetPlayerCar(uint32_t car_phys);
void PartTuneForgetCar(uint32_t car_phys);
// engine_build/engine_builder.cpp: parts-based engine builds (docs/ENGINE_BUILDER.md).
void EngineBuilderSetPlayerCar(uint32_t car_phys);
float EngineBuilderTorqueRatio(uint32_t car_phys, float rpm);
float EngineBuilderLimiter(uint32_t car_phys, float current_rad_s);

namespace {

constexpr uint32_t kOmega = 2724, kDamage = 2732, kMult = 2736, kPower = 2800;
constexpr uint32_t kLimiter = 3636, kModZero = 3684, kModFull = 3688;
constexpr uint32_t kFlagA = 5402, kFlagB = 5403, kMode = 5608;
// Body state (found 2026-10-13 in the stability-management code, sub_82303A30):
// E+80 = velocity vector x,y,z (m/s, world), E+64 = car forward axis.
constexpr uint32_t kVelocity = 80, kForward = 64;
// Stock sputter state (sub_82304330): setting the flag and clearing the timer
// makes the game cut the engine for 0.08 s, the same as a damage misfire.
constexpr uint32_t kSputterTimer = 2816, kSputterFlag = 2832;
// Effective throttle 0..1 (sub_82303628: engine torque = lerp(closed, open,
// clamp01(E+6128)); the stock sputter zeroes it). Used as the engine load.
// E+2800 turned out not to be engine power (only a few kW in the log).
constexpr uint32_t kThrottle = 6128;
// Forced induction (sub_823057C8 turbo, sub_82305738 supercharger, used for
// torque in sub_82303378). Turbo: E+4512 present, E+2764 boost, mapped onto
// the torque scale between E+4536 (min boost) and E+4540 (max boost).
// Supercharger: E+4560 present, E+2772 boost, E+4576 = closed-throttle value,
// E+4580 / E+4584 = boost at low / high rpm.
// Units (from the 2026-10-13 log): absolute pressure in psi. Every car's floor
// is 3.675 (0.25 atm, closed-throttle vacuum); maxima 23.2-35.3 (8.5-20.6 psi
// of boost). Gauge boost = value - 14.696 psi; the carattribs Boost gauges
// (e.g. Veyron -12.3..18.1) use the same gauge psi.
constexpr float kAtmPsi = 14.696f, kPsiPerBar = 14.5038f;
// Car world position (vector x,y,z): first item of the car-state snapshot
// sub_82303960 (E+16, then orientation rows E+32..E+79, velocity E+80).
constexpr uint32_t kPosition = 16;
// Current race setup: sub_822DE6F0(x) returns x+444 for x = [0x82DC383C];
// track id = u32 at x+448, time of day = u8 at x+452 (as used by
// sub_82568BB0's EnvironmentTimeOfDay query).
constexpr uint32_t kSetupGlobal = 0x82DC383C, kSetupTrack = 448, kSetupTod = 452;

// Generated 2026-10-13 from extracted\Media\db\gamedb.slt (base game).
// Tracks.id -> EnvironmentId
constexpr uint16_t kTrackEnv[][2] = {
  {0, 7}, {1, 23}, {2, 23}, {3, 15}, {4, 19}, {5, 19}, {6, 19}, {7, 19},
  {8, 19}, {9, 19}, {16, 16}, {17, 16}, {18, 18}, {19, 18}, {20, 18}, {31, 13},
  {33, 35}, {34, 35}, {35, 11}, {36, 11}, {37, 20}, {38, 20}, {39, 20}, {40, 17},
  {41, 17}, {42, 17}, {43, 10}, {44, 10}, {45, 10}, {46, 10}, {67, 9}, {68, 9},
  {69, 9}, {70, 9}, {72, 13}, {73, 13}, {74, 13}, {75, 13}, {78, 6}, {79, 6},
  {80, 6}, {81, 6}, {82, 6}, {83, 6}, {84, 6}, {85, 6}, {86, 6}, {87, 6},
  {88, 6}, {89, 6}, {90, 6}, {91, 6}, {100, 8}, {101, 8}, {110, 3}, {111, 3},
  {112, 3}, {121, 32}, {127, 32}, {140, 1}, {141, 1}, {142, 1}, {143, 1}, {144, 1},
  {145, 1}, {146, 1}, {147, 1}, {148, 1}, {149, 1}, {160, 4}, {161, 4}, {162, 4},
  {163, 4}, {164, 28}, {165, 28}, {166, 28}, {167, 29}, {168, 29}, {169, 29}, {171, 4},
  {172, 4}, {173, 4}, {174, 28}, {175, 28}, {176, 28}, {177, 29}, {178, 29}, {179, 29},
  {196, 5}, {197, 5}, {200, 2}, {201, 45}, {205, 21}, {220, 44}, {221, 44}, {222, 44},
  {223, 44}, {224, 44}, {225, 44}, {226, 44}, {228, 44}, {229, 44}, {230, 46}, {231, 46},
  {240, 47}, {241, 47}, {242, 47}, {243, 47}, {244, 47}, {245, 47}, {250, 48}, {251, 48},
  {252, 48}, {253, 48}, {260, 49}, {261, 49}, {262, 49}, {263, 49}, {290, 44}, {302, 27},
  {303, 30}, {308, 37}, {309, 38}, {310, 39}, {313, 42}, {319, 54}, {326, 61},
};
// EnvironmentTimeOfDay: EnvironmentId, TimeOfDayIndex, Temperature (deg F)
constexpr int16_t kEnvTemp[][3] = {
  {0, 0, 72}, {1, 0, 94}, {3, 0, 70}, {4, 0, 76}, {4, 1, 64}, {6, 0, 68},
  {7, 0, 74}, {8, 0, 76}, {9, 0, 56}, {10, 0, 68}, {11, 0, 72}, {13, 0, 67},
  {14, 0, 72}, {15, 0, 89}, {16, 0, 80}, {17, 0, 78}, {18, 0, 70}, {19, 0, 82},
  {20, 0, 70}, {20, 1, 61}, {23, 0, 71}, {28, 0, 73}, {29, 0, 77}, {32, 0, 70},
  {35, 0, 72}, {44, 0, 60}, {44, 1, 56}, {46, 0, 85}, {46, 1, 60}, {47, 0, 24},
  {48, 0, 76}, {48, 1, 62}, {49, 0, 74}, {49, 1, 67}, {62, 0, 60}, {62, 1, 56},
};

// Air temperature in C for a track / time of day; NaN if the table has no row.
float TableAirC(uint32_t track, uint32_t tod) {
  int env = -1;
  for (const auto& t : kTrackEnv) if (t[0] == track) { env = t[1]; break; }
  if (env < 0) return NAN;
  for (const auto& r : kEnvTemp)
    if (r[0] == env && r[1] == static_cast<int>(tod)) return (r[2] - 32.0f) / 1.8f;
  for (const auto& r : kEnvTemp)  // other time of day of the same environment
    if (r[0] == env) return (r[2] - 32.0f) / 1.8f;
  return NAN;
}

constexpr uint32_t kTurboOn = 4512, kTurboBoost = 2764, kTurboMin = 4536, kTurboMax = 4540;
constexpr uint32_t kScOn = 4560, kScBoost = 2772, kScClosed = 4576, kScLow = 4580, kScHigh = 4584;

enum Fault : int { kNone = 0, kCoolantLeak, kOilLeak, kMisfire, kFuelPump, kBoostLeak, kBlown, kFaultCount };
const char* FaultName(int f) {
  switch (f) {
    case kCoolantLeak: return "COOLANT LEAK";
    case kOilLeak: return "OIL LEAK";
    case kMisfire: return "MISFIRE";
    case kFuelPump: return "FUEL PUMP";
    case kBoostLeak: return "BOOST LEAK";
    case kBlown: return "ENGINE BLOWN";
    default: return "";
  }
}

// Fault rate multiplier: 0 = off, 1 = realistic (about 1% per race hour), 10, 50.
float FaultRate() {
  static const float r = [] {
    const char* v = std::getenv("FM4_FAULTS");
    if (!v || !*v) return 1.0f;  // default: realistic faults (they still need Simulation damage)
    std::string t(v);
    for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (t == "1" || t == "realistic") return 1.0f;
    if (t == "2" || t == "gritty") return 10.0f;
    if (t == "3" || t == "chaos") return 50.0f;
    return 0.0f;
  }();
  return r;
}

int Mode() {
  static const int mode = [] {
    const char* v = std::getenv("FM4_OVERHEAT");
    if (!v || !*v) return 2;  // default: heat model on; it only damages engines in Simulation damage mode
    return std::clamp(std::atoi(v), 0, 2);
  }();
  return mode;
}
bool AnyMode() {
  static const bool any = [] { const char* v = std::getenv("FM4_OVERHEAT_ANYMODE"); return v && v[0] == '1'; }();
  return any;
}
float EnvFloat(const char* name, float def, float lo, float hi) {
  const char* v = std::getenv(name);
  if (!v || !*v) return def;
  const float f = static_cast<float>(std::atof(v));
  return (f >= lo && f <= hi) ? f : def;
}
float AmbientOverride() { static const float a = EnvFloat("FM4_OVERHEAT_AMBIENT", NAN, -40.0f, 60.0f); return a; }
bool DraftEnabled() { static const bool on = [] { const char* v = std::getenv("FM4_OVERHEAT_DRAFT"); return !(v && v[0] == '0'); }(); return on; }
float ImpactG() { static const float g = EnvFloat("FM4_OVERHEAT_IMPACT_G", 5.0f, 2.0f, 50.0f); return g; }

float HeatScale() {
  static const float s = [] {
    const char* v = std::getenv("FM4_OVERHEAT_HEAT");
    const float f = v ? static_cast<float>(std::atof(v)) : 1.0f;
    return (f > 0.0f && f < 20.0f) ? f : 1.0f;
  }();
  return s;
}

uint32_t Ld32(uint32_t a) {
  const uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(a);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
void St32(uint32_t a, uint32_t v) {
  uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(a);
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
float LdF(uint32_t a) { return std::bit_cast<float>(Ld32(a)); }
void StF(uint32_t a, float f) { St32(a, std::bit_cast<uint32_t>(f)); }
uint8_t Ld8(uint32_t a) { return *REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(a); }

using Clock = std::chrono::steady_clock;

struct EngineState {
  float tw = 85.0f;      // coolant, C
  float to = 80.0f;      // oil, C
  float p = 3.0f;        // oil pressure, bar
  float pmax = 40000.0f; // running max of engine power, W
  float last_written = -1.0f;
  uint32_t force_gen = 0;
  uint64_t order = 0;    // when this car was first seen (grid registration order)
  uint8_t flag_b = 0;
  bool damaging = false;  // heat model is allowed to damage this engine
  float speed = 0.0f;     // m/s
  float g = 0.0f;         // horizontal acceleration in g, smoothed
  float starve = 0.0f;    // seconds of sustained high g
  float vx = 0, vy = 0, vz = 0;
  bool have_v = false;
  int fault = kNone;
  uint32_t fault_gen = 0;
  float fault_t = 0.0f;       // seconds since the fault started
  bool fault_forced = false;  // from the test key (acts even without heat damage)
  float overrev_t = 0.0f;
  float px = 0, py = 0, pz = 0;   // world position (E+16)
  float fx = 0, fy = 0, fz = 1;   // forward axis (E+64), normalised
  bool have_p = false;
  float draft = 0.0f;             // wake strength 0..1 from the car ahead
  float ahead = 0.0f;             // distance to that car, m (0 = none)
  float rad_health = 1.0f;        // radiator after frontal hits
  float vlong = 0.0f, along = 0.0f, hit_peak = 0.0f;  // impact detection
  bool have_long = false;
  int fi = 0;  // forced induction: 0 none, 1 turbo, 2 supercharger
  float boost = 0.0f, boost_min = 0.0f, boost_max = 0.0f;
  float rpm = 0.0f, damage = 0.0f;
  float base_mult = 0.0f, tuned_mult = 0.0f;  // power multiplier before / after the part tune (see below)
  float cool = 0.0f;   // fitted cooling tier (0 = stock), from the provider
  float wear = 0.0f;   // engine wear 0 fresh .. 1 worn, from the provider
  float build = 0.0f;  // how hard the engine is built 0..1 (redline and boost)
  Clock::time_point last_seen{};
  Clock::time_point last_log{};
};

std::mutex g_mutex;
std::unordered_map<uint32_t, EngineState> g_cars;
std::atomic<uint32_t> g_force_gen{0};
bool g_force_all = true;
uint32_t g_force_target = 0;    // car to force when !g_force_all
uint32_t g_hud_car = 0;         // car shown on the HUD (0 = automatic)
uint64_t g_order_counter = 0;
float g_air_c = 25.0f;           // current track air temperature
uint32_t g_trk = 0xFFFFFFFF, g_tod = 0;
bool g_air_from_table = false;
Clock::time_point g_air_read{};
std::atomic<uint32_t> g_fault_gen{0};
int g_fault_next = kCoolantLeak;  // test key cycles through the faults
int g_fault_pick = kCoolantLeak;
std::mt19937 g_rng{std::random_device{}()};
float Uniform() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(g_rng); }
std::atomic<bool> g_reliability_on{true};  // Options toggle; off = temperatures only
std::atomic<EngineReliabilityProvider> g_provider{nullptr};
std::atomic<int> g_force_step{0};  // 0 -> 0.35, 1 -> 1.0, 2 -> 0 (repair)
float g_force_value = 0.0f;

bool ActiveLocked(const EngineState& c, Clock::time_point now) {
  return now - c.last_seen < std::chrono::seconds(1);
}

// The car the HUD shows: the one picked with Ctrl+F12 if still on track;
// otherwise a guess at the player: the only active car with flag E+5403 == 0
// (the stock over-rev damage skips cars with it set, likely the AI), else the
// first car registered for this race.
uint32_t HudCarLocked() {
  const auto now = Clock::now();
  if (g_hud_car) {
    auto it = g_cars.find(g_hud_car);
    if (it != g_cars.end() && ActiveLocked(it->second, now)) return g_hud_car;
  }
  uint32_t first = 0, unique_clear = 0;
  uint64_t first_order = UINT64_MAX;
  int clear_count = 0;
  for (const auto& [e, c] : g_cars) {
    if (!ActiveLocked(c, now)) continue;
    if (c.order < first_order) { first_order = c.order; first = e; }
    if (c.flag_b == 0) { ++clear_count; unique_clear = e; }
  }
  return clear_count == 1 ? unique_clear : first;
}

uint32_t NextHudCarLocked() {
  const auto now = Clock::now();
  std::vector<std::pair<uint64_t, uint32_t>> cars;
  for (const auto& [e, c] : g_cars)
    if (ActiveLocked(c, now)) cars.emplace_back(c.order, e);
  if (cars.empty()) return 0;
  std::sort(cars.begin(), cars.end());
  const uint32_t cur = HudCarLocked();
  for (size_t i = 0; i < cars.size(); ++i)
    if (cars[i].second == cur) return cars[(i + 1) % cars.size()].second;
  return cars[0].second;
}

bool GameHasFocus() {
#ifdef _WIN32
  HWND fg = GetForegroundWindow();
  DWORD pid = 0;
  if (fg) GetWindowThreadProcessId(fg, &pid);
  return pid == GetCurrentProcessId();
#else
  return false;
#endif
}

// Poll F12 at most every 50 ms (the hook runs thousands of times a second).
void PollKey() {
#ifdef _WIN32
  static std::atomic<int64_t> next_poll{0};
  static bool was_down = false;  // only touched by the thread that wins the poll slot
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
  int64_t due = next_poll.load(std::memory_order_relaxed);
  if (now < due) return;
  if (!next_poll.compare_exchange_strong(due, now + 50)) return;
  const bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0 && GameHasFocus();
  if (down && !was_down) {
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && shift) {
      int f;
      uint32_t target;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        f = g_fault_pick = g_fault_next;
        g_fault_next = g_fault_next % (kFaultCount - 1) + 1;
        g_force_target = target = HudCarLocked();
      }
      g_fault_gen.fetch_add(1);
      REXLOG_WARN("Overheat: Ctrl+Shift+F12 -> fault {} on car {:08X}", FaultName(f), target);
    } else if (ctrl) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_hud_car = NextHudCarLocked();
      REXLOG_WARN("Overheat: HUD now shows car {:08X}", g_hud_car);
    } else {
      const int step = g_force_step.load();
      const float value = step == 0 ? 0.35f : (step == 1 ? 1.0f : 0.0f);
      uint32_t target = 0;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_force_value = value;
        g_force_all = shift;
        g_force_target = target = HudCarLocked();
      }
      g_force_step.store((step + 1) % 3);
      const uint32_t gen = g_force_gen.fetch_add(1) + 1;
      if (shift) REXLOG_WARN("Overheat: Shift+F12 -> engine damage {:.2f} on every car (gen {})", value, gen);
      else REXLOG_WARN("Overheat: F12 -> engine damage {:.2f} on car {:08X} (gen {})", value, target, gen);
    }
  }
  was_down = down;
#endif
}

void WriteDamage(uint32_t e, EngineState& s, float d) {
  d = std::clamp(d, 0.0f, 1.0f);
  StF(e + kDamage, d);
  const float m0 = LdF(e + kModZero), m1 = LdF(e + kModFull);
  StF(e + kMult, (m0 + (m1 - m0) * d) * (s.fault == kMisfire ? 0.88f : 1.0f));
  s.last_written = d;
}

// Heat/oil model for one substep. Returns the damage to add.
float Model(EngineState& s, float omega, float limiter, float throttle, float speed, float dt) {
  constexpr float kThermostat = 90.0f;
  // Air reaching the radiator: track air, warmed by the car ahead when drafting.
  const float kAmbient = g_air_c + 15.0f * s.draft;
  const float load = std::clamp(throttle, 0.0f, 1.0f);
  const float rpm_n = limiter > 1.0f ? std::clamp(omega / limiter, 0.0f, 1.2f) : 0.0f;
  // Radiator airflow: the fan gives 55% when stopped; full ram air from 35 m/s (126 km/h).
  // Drafting: turbulent, slow air behind another car cuts airflow by up to 45%.
  const float air = (0.55f + 0.45f * std::clamp(speed / 35.0f, 0.0f, 1.0f)) * (1.0f - 0.45f * s.draft);

  // Coolant: heat in from load and rpm; the radiator only opens above the thermostat.
  // A hard-built engine (high redline, big boost) makes more heat.
  const float heat_in = 1.6f * HeatScale() * (1.0f + 0.5f * s.build) * (0.15f + 0.85f * load) * (0.3f + 0.7f * rpm_n);
  const float open = std::clamp((s.tw - (kThermostat - 2.0f)) / 4.0f, 0.0f, 1.0f);
  // Coolant leak: the radiator loses efficiency over about 4 minutes (down to 25%).
  const float rad = s.fault == kCoolantLeak ? std::max(0.25f, 1.0f - s.fault_t / 240.0f) : 1.0f;
  // Cooling part: +12% heat rejection per tier; a worn engine (fouled radiator) loses up to 25%.
  const float cooling = (1.0f + 0.12f * s.cool) * (1.0f - 0.25f * s.wear);
  const float heat_out = 0.0213f * air * rad * s.rad_health * cooling * open * std::max(s.tw - kAmbient, 0.0f);
  s.tw += (heat_in - heat_out) * dt;
  s.tw = std::clamp(s.tw, std::min(kAmbient, 85.0f), 160.0f);

  // Oil runs hotter than the coolant: about 10 C at idle, plus friction heat
  // that grows with rpm and load (race pace about 110-120 C, flat out 125-130).
  const float to_target = s.tw + 10.0f - 3.0f * s.cool + 25.0f * (1.0f + 0.3f * s.build) * rpm_n * rpm_n * (0.5f + 0.5f * load);
  s.to += (to_target - s.to) / 30.0f * dt;

  // Pressure: about 1.2 bar at idle, up to a 5 bar relief valve, thinner when hot.
  const float thin = std::clamp(1.0f - 0.01f * std::max(s.to - 110.0f, 0.0f) - 0.15f * s.wear, 0.3f, 1.0f);
  s.p = omega < 10.0f ? 0.0f : std::min(5.0f, 1.2f + 4.5f * rpm_n) * thin;

  // Oil leak: the level, and with it the pressure, falls over about 5 minutes.
  if (s.fault == kOilLeak) s.p *= std::max(0.1f, 1.0f - s.fault_t / 300.0f);

  // Oil surge: long, hard cornering/braking (above 1.3 g for over 3 s) pushes
  // the oil away from the pickup; pressure collapses until the load eases.
  if (s.g > 1.3f) s.starve += dt; else s.starve = std::max(0.0f, s.starve - 2.0f * dt);
  if (s.starve > 3.0f) s.p *= std::clamp(1.0f - (s.starve - 3.0f) / 1.5f, 0.15f, 1.0f);

  float dd = 0.0f;
  if (s.tw > 115.0f) dd += 0.002f * (s.tw - 115.0f) * dt;  // head gasket
  if (s.to > 140.0f) dd += 0.002f * (s.to - 140.0f) * dt;  // oil breakdown
  if (s.p < 0.7f + 0.15f * s.wear && rpm_n > 0.5f) dd += 0.25f * dt;  // bearings (worn ones sooner)
  return dd;
}

}  // namespace

// 0x82304964 (sub_82304940, after bl sub_82304548): r31 = E, f31 = dt.
void OverheatEngineStep(PPCRegister& r31, PPCRegister& f31) {
  const int mode = Mode();
  if (mode == 0) return;
  const uint32_t e = r31.u32;
  const float dt = static_cast<float>(f31.f64);
  if (e < 0x10000 || !(dt > 0.0f) || dt > 0.1f) return;

  PollKey();

  const auto now = Clock::now();
  std::lock_guard<std::mutex> lock(g_mutex);
  auto [it, fresh] = g_cars.try_emplace(e);
  EngineState& s = it->second;
  // New car object, or not stepped for 2 s (between races): start warm and clean.
  const bool fresh_car = fresh || now - s.last_seen > std::chrono::seconds(2);
  static const bool tune_registered = (PartTuneRegister(), true);
  (void)tune_registered;
  if (fresh_car) {
    if (Ld8(e + kFlagB) == 0) { PartTuneSetPlayerCar(e); EngineBuilderSetPlayerCar(e); }  // same player guess as the HUD
    s = EngineState{};
    s.force_gen = g_force_gen.load();
    s.fault_gen = g_fault_gen.load();
    s.last_log = now;
    s.order = ++g_order_counter;
  }
  s.last_seen = now;

  const float damage = LdF(e + kDamage);
  // Repaired or reset by the game (damage dropped below what we wrote): cool down again.
  if (s.last_written >= 0.0f && damage + 0.01f < s.last_written) {
    s.tw = 85.0f; s.to = 80.0f; s.last_written = -1.0f;
    s.fault = kNone; s.rad_health = 1.0f;
  }

  // F12 test key.
  const uint32_t gen = g_force_gen.load();
  if (s.force_gen != gen) {
    s.force_gen = gen;
    if (g_force_all || g_force_target == e) {
      WriteDamage(e, s, g_force_value);
      if (g_force_value == 0.0f) { s.tw = 85.0f; s.to = 80.0f; s.last_written = -1.0f; s.fault = kNone; s.rad_health = 1.0f; }
    }
  }

  const uint32_t dmg_mode = Ld32(e + kMode);
  const float omega = LdF(e + kOmega), limiter = LdF(e + kLimiter), power = LdF(e + kPower);
  const float throttle = LdF(e + kThrottle);

  // Temperatures are always simulated so the HUD shows them; they only cause
  // engine damage with FM4_OVERHEAT=2 in simulation damage (mode 2).
  {
    const float vx = LdF(e + kVelocity), vy = LdF(e + kVelocity + 4), vz = LdF(e + kVelocity + 8);
    s.speed = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (s.have_v) {
      // Horizontal acceleration (y is up), smoothed over about 0.2 s.
      const float ax = (vx - s.vx) / dt, az = (vz - s.vz) / dt;
      const float g_now = std::min(std::sqrt(ax * ax + az * az) / 9.81f, 5.0f);
      s.g += (g_now - s.g) * std::min(1.0f, dt / 0.2f);
    }
    s.vx = vx; s.vy = vy; s.vz = vz; s.have_v = true;
  }

  // ---- Track air temperature (read about once a second) ----
  if (now - g_air_read > std::chrono::seconds(1)) {
    g_air_read = now;
    const float over = AmbientOverride();
    const uint32_t base = Ld32(kSetupGlobal);
    if (base >= 0x40000000u && base < 0x90000000u) {
      g_trk = Ld32(base + kSetupTrack);
      g_tod = Ld8(base + kSetupTod);
    }
    const float t = TableAirC(g_trk, g_tod);
    g_air_from_table = !std::isnan(t);
    g_air_c = !std::isnan(over) ? over : (g_air_from_table ? t : 25.0f);
  }

  // ---- Position, forward axis, drafting, frontal impacts ----
  {
    const float px = LdF(e + kPosition), py = LdF(e + kPosition + 4), pz = LdF(e + kPosition + 8);
    float fx = LdF(e + kForward), fy = LdF(e + kForward + 4), fz = LdF(e + kForward + 8);
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl > 0.5f && fl < 2.0f) { fx /= fl; fy /= fl; fz /= fl; } else { fx = 0; fy = 0; fz = 1; }
    // A jump of more than ~5 m in one substep is a reset or rewind, not driving.
    const float jx = px - s.px, jy = py - s.py, jz = pz - s.pz;
    const bool teleport = s.have_p && (jx * jx + jy * jy + jz * jz) > 25.0f;
    s.px = px; s.py = py; s.pz = pz; s.fx = fx; s.fy = fy; s.fz = fz; s.have_p = true;

    // Drafting: strongest wake from a car 1.5-20 m directly ahead.
    float best = 0.0f, best_d = 0.0f;
    if (DraftEnabled()) {
      for (const auto& [oe, o] : g_cars) {
        if (oe == e || !o.have_p || now - o.last_seen > std::chrono::milliseconds(500)) continue;
        const float dx = o.px - px, dy = o.py - py, dz = o.pz - pz;
        const float al = dx * fx + dy * fy + dz * fz;
        if (al < 1.5f || al > 20.0f) continue;
        const float lx = dx - al * fx, ly = dy - al * fy, lz = dz - al * fz;
        const float lat = std::sqrt(lx * lx + ly * ly + lz * lz);
        const float lim = 1.8f + 0.05f * al;
        if (lat >= lim) continue;
        const float w = (1.0f - al / 20.0f) * (1.0f - lat / lim);
        if (w > best) { best = w; best_d = al; }
      }
    }
    // Smooth over about 1 s so passing cars don't flicker it.
    s.draft += (best - s.draft) * std::min(1.0f, dt / 1.0f);
    if (best > 0.0f) s.ahead = best_d; else if (s.draft < 0.05f) s.ahead = 0.0f;

    // Frontal impact: longitudinal deceleration above ImpactG(), averaged
    // over about 10 ms (hard braking peaks at 2-2.5 g; a hit from behind
    // accelerates the car instead).
    const float vl = s.vx * fx + s.vy * fy + s.vz * fz;
    if (s.have_long && !teleport) {
      s.along += ((vl - s.vlong) / dt - s.along) * std::min(1.0f, dt / 0.01f);
      const float decel_g = -s.along / 9.81f;
      if (decel_g > ImpactG()) {
        s.hit_peak = std::max(s.hit_peak, decel_g);
      } else if (s.hit_peak > 0.0f) {
        const float before = s.rad_health;
        s.rad_health = std::max(0.2f, s.rad_health - 0.04f * (s.hit_peak - ImpactG()));
        REXLOG_WARN("Overheat: frontal hit {:.1f} g on car {:08X}, radiator {:.0f}% -> {:.0f}%", s.hit_peak, e,
                    before * 100.0f, s.rad_health * 100.0f);
        if (s.hit_peak > 12.0f && s.damaging && s.fault == kNone) {
          s.fault = kCoolantLeak; s.fault_t = 0.0f; s.fault_forced = false;
          REXLOG_WARN("Overheat: FAULT COOLANT LEAK on car {:08X} (frontal hit {:.1f} g)", e, s.hit_peak);
        }
        s.hit_peak = 0.0f;
      }
    } else {
      s.along = 0.0f; s.hit_peak = 0.0f;
    }
    s.vlong = vl; s.have_long = true;
  }
  s.damaging = mode == 2 && g_reliability_on.load() && (dmg_mode == 2 || AnyMode());

  // ---- Boost (turbo / supercharger) ----
  uint32_t boost_addr = 0;
  if (Ld32(e + kTurboOn) != 0) {
    s.fi = 1; boost_addr = e + kTurboBoost;
    s.boost_min = LdF(e + kTurboMin); s.boost_max = LdF(e + kTurboMax);
  } else if (Ld32(e + kScOn) != 0) {
    s.fi = 2; boost_addr = e + kScBoost;
    s.boost_min = LdF(e + kScClosed); s.boost_max = std::max(LdF(e + kScLow), LdF(e + kScHigh));
  } else {
    s.fi = 0;
  }
  if (boost_addr) s.boost = LdF(boost_addr);

  // ---- Car stats: build strength from the physics object, cooling tier and wear from the provider ----
  {
    const float redline = std::clamp((limiter * 9.5493f - 6500.0f) / 4000.0f, 0.0f, 1.0f);
    const float boost_bar = s.fi ? std::clamp((s.boost_max - kAtmPsi) / kPsiPerBar / 1.5f, 0.0f, 1.0f) : 0.0f;
    s.build = 0.5f * redline + 0.5f * boost_bar;
    if (fresh_car) {
      bool have = false;
      if (auto prov = g_provider.load()) { float c = 0.0f, w = 0.0f; if (prov(e, c, w)) { s.cool = std::clamp(c, 0.0f, 10.0f); s.wear = std::clamp(w, 0.0f, 1.0f); have = true; } }
      // AI opponents have no garage row: give them stand-in stats so they face the same causes as the player.
      // Wear is random up to 50%; cooling roughly follows how hard the engine is built, but may fall short.
      // Cars with E+5403 == 0 are treated as the player (see HudCarLocked) and left alone.
      if (!have && Ld8(e + kFlagB) != 0) {
        s.wear = 0.5f * Uniform();
        s.cool = std::clamp(std::round(3.0f * s.build + (Uniform() - 0.6f) * 2.0f), 0.0f, 3.0f);
      }
    }
  }

  // ---- Random faults ----
  auto start_fault = [&](int f, bool forced) {
    if (f == kBoostLeak && s.fi == 0) {
      REXLOG_WARN("Overheat: car {:08X} has no turbo or supercharger; BOOST LEAK skipped", e);
      return;
    }
    s.fault = f; s.fault_t = 0.0f; s.fault_forced = forced;
    REXLOG_WARN("Overheat: FAULT {} on car {:08X}{} (Tw {:.0f}, To {:.0f}, dmg {:.2f})", FaultName(f), e,
                forced ? " (test key)" : "", s.tw, s.to, LdF(e + kDamage));
    if (f == kBlown) WriteDamage(e, s, 1.0f);
  };
  {
    const uint32_t fgen = g_fault_gen.load();
    if (s.fault_gen != fgen) {
      s.fault_gen = fgen;
      if (g_force_target == e) start_fault(g_fault_pick, true);
    }
  }
  const bool running = omega > 10.0f;
  s.overrev_t = (limiter > 1.0f && omega > limiter + 15.7f) ? s.overrev_t + dt : std::max(0.0f, s.overrev_t - dt);
  if (s.fault == kNone && s.damaging && running && FaultRate() > 0.0f) {
    // About 1% per race hour at "realistic", times stress.
    const float stress = 1.0f + (s.tw > 105.0f ? 2.0f : 0.0f) + (s.to > 125.0f ? 2.0f : 0.0f) +
                         (s.starve > 3.0f ? 3.0f : 0.0f) + (s.overrev_t > 0.0f ? 5.0f : 0.0f) +
                         3.0f * LdF(e + kDamage) + 3.0f * s.wear + 1.5f * s.build;
    const float hazard = (0.01f / 3600.0f) * FaultRate() * stress;
    if (Uniform() < hazard * dt) {
      const float r = Uniform();
      // Blown engines mostly come from stress: weight 5% when healthy, up to 20% when abused.
      const float blown = std::min(0.20f, 0.05f * stress);
      // Shares of the rest: without boost 30/20/30/20, with boost 25/15/25/15/20.
      const float q = (r - blown) / (1.0f - blown);
      int f;
      if (r < blown) f = kBlown;
      else if (s.fi == 0) f = q < 0.30f ? kCoolantLeak : q < 0.50f ? kOilLeak : q < 0.80f ? kMisfire : kFuelPump;
      else f = q < 0.25f ? kCoolantLeak : q < 0.40f ? kOilLeak : q < 0.65f ? kMisfire : q < 0.80f ? kFuelPump : kBoostLeak;
      start_fault(f, false);
    }
  }
  if (s.fault != kNone) {
    s.fault_t += dt;
    const bool may_act = s.damaging || s.fault_forced;
    if (may_act && boost_addr && s.fault == kBoostLeak) {
      // Split hose / failed wastegate: boost capped at 35% of its range, and the
      // engine runs badly (lean/rich) under load: about 1 cut a second.
      // Cap at atmospheric + 35% of the car's positive boost (not of the whole
      // range, which would pull the engine below atmospheric pressure).
      const float cap = kAtmPsi + 0.35f * std::max(0.0f, s.boost_max - kAtmPsi);
      if (LdF(boost_addr) > cap) StF(boost_addr, cap);
      s.boost = LdF(boost_addr);
      if (running && throttle > 0.6f && Ld32(e + kSputterFlag) == 0 && Uniform() < 1.0f * dt) {
        StF(e + kSputterTimer, 0.0f);
        St32(e + kSputterFlag, 1);
      }
    }
    if (may_act && running && (s.fault == kMisfire || s.fault == kFuelPump)) {
      // Misfire: a steady 12% power loss (applied to the power multiplier) and
      // about 1.5 cuts a second. Fuel pump: bursts of cuts under load.
      const float m0 = LdF(e + kModZero), m1 = LdF(e + kModFull), d = LdF(e + kDamage);
      const float want = (m0 + (m1 - m0) * d) * (s.fault == kMisfire ? 0.88f : 1.0f);
      if (LdF(e + kMult) > want) StF(e + kMult, want);
      float cut_rate = 0.0f;
      if (s.fault == kMisfire) cut_rate = 1.5f;
      else if (throttle > 0.6f) cut_rate = (std::fmod(s.fault_t, 6.0f) < 2.0f) ? 8.0f : 0.5f;
      if (cut_rate > 0.0f && Ld32(e + kSputterFlag) == 0 && Uniform() < cut_rate * dt) {
        StF(e + kSputterTimer, 0.0f);
        St32(e + kSputterFlag, 1);
      }
    }
  }

  // Part tune (part_tune.cpp): rpm-dependent multiplier on the engine power multiplier. If the game did
  // not recompute E+2736 since our last write (value unchanged) start from the stored base, so it never compounds.
  if (running) {
    const float cur = LdF(e + kMult);
    const float base = (s.tuned_mult > 0.0f && cur == s.tuned_mult) ? s.base_mult : cur;
    const float tuned = base * PartTuneTorqueMult(e, omega * 9.5493f) * EngineBuilderTorqueRatio(e, omega * 9.5493f);
    if (tuned > 0.0f && tuned == tuned) { StF(e + kMult, tuned); s.base_mult = base; s.tuned_mult = LdF(e + kMult); }
  }

  // Engine builder: a build that revs further moves the rev limiter up (0 = leave it).
  if (const float lim = EngineBuilderLimiter(e, limiter); lim > 0.0f) StF(e + kLimiter, lim);

  const float dd = Model(s, omega, limiter, throttle, s.speed, dt);
  if (s.damaging && dd > 0.0f) WriteDamage(e, s, LdF(e + kDamage) + dd);

  s.flag_b = Ld8(e + kFlagB);
  s.rpm = omega * 9.5493f;
  s.damage = LdF(e + kDamage);

  if (now - s.last_log >= std::chrono::seconds(2)) {
    s.last_log = now;
    // Forget cars from earlier races so "cars=" is the current grid.
    for (auto i = g_cars.begin(); i != g_cars.end();) {
      if (i->first != e && now - i->second.last_seen > std::chrono::seconds(5)) { PartTuneForgetCar(i->first); i = g_cars.erase(i); }
      else ++i;
    }
    REXLOG_WARN(
        "Overheat: car {:08X} cars={} dt={:.5f} mode={} flags={}/{} rpm={:.0f} lim={:.0f} "
        "thr={:.2f} e2800={:.1f} fi={} boost={:.3f} [{:.3f}..{:.3f}] speed={:.0f}km/h g={:.2f} dmg={:.3f} mult={:.3f} Tw={:.1f} To={:.1f} P={:.2f} fault={} "
        "air={:.0f} trk={} tod={} tbl={} pos=({:.1f},{:.1f},{:.1f}) draft={:.2f} ahead={:.1f} rad={:.2f}",
        e, g_cars.size(), dt, dmg_mode, Ld8(e + kFlagA), Ld8(e + kFlagB), omega * 9.5493f,
        limiter * 9.5493f, throttle, power / 1000.0f, s.fi, s.boost, s.boost_min, s.boost_max, s.speed * 3.6f, s.g, LdF(e + kDamage), LdF(e + kMult), s.tw, s.to, s.p,
        s.fault ? FaultName(s.fault) : "none", g_air_c, static_cast<int>(g_trk), g_tod, g_air_from_table ? 1 : 0,
        s.px, s.py, s.pz, s.draft, s.ahead, s.rad_health);
  }
}

// ---------------------------------------------------------------------------
// HUD readout (ImGui overlay, drawn by the runtime on the presented frame).
// ---------------------------------------------------------------------------
namespace {

bool HudEnabled() {
  static const bool on = [] { const char* v = std::getenv("FM4_OVERHEAT_HUD"); return !(v && v[0] == '0'); }();
  return on;
}

ImVec2 HudPos() {
  static const ImVec2 pos = [] {
    float x = 0.83f, y = 0.62f;
    if (const char* v = std::getenv("FM4_OVERHEAT_HUD_POS")) {
      float a, b;
      if (std::sscanf(v, "%f,%f", &a, &b) == 2 && a >= 0 && a <= 1 && b >= 0 && b <= 1) { x = a; y = b; }
    }
    return ImVec2(x, y);
  }();
  return pos;
}

ImVec4 Grade(float v, float warn, float bad) {
  if (v >= bad) return ImVec4(1.0f, 0.30f, 0.25f, 1.0f);
  if (v >= warn) return ImVec4(1.0f, 0.75f, 0.20f, 1.0f);
  return ImVec4(0.55f, 0.95f, 0.55f, 1.0f);
}

class OverheatHud : public rex::ui::ImGuiDialog {
 public:
  explicit OverheatHud(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (Mode() == 0 || !HudEnabled()) return;
    EngineState c;
    float air = 25.0f;
    uint32_t car = 0;
    size_t count = 0;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      car = HudCarLocked();
      auto it = g_cars.find(car);
      if (!car || it == g_cars.end() || !ActiveLocked(it->second, Clock::now())) return;  // not racing
      c = it->second;
      air = g_air_c;
      for (const auto& kv : g_cars) count += ActiveLocked(kv.second, Clock::now()) ? 1 : 0;
    }
    const float scale = std::max(0.6f, io.DisplaySize.y / 1080.0f);
    const ImVec2 pos = HudPos();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * pos.x, io.DisplaySize.y * pos.y), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##fm4_overheat_hud", nullptr, flags)) {
      ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f * scale);
      ImGui::TextColored(Grade(c.tw, 105.0f, 115.0f), "WATER %4.0f C", c.tw);
      ImGui::TextColored(Grade(c.to, 125.0f, 140.0f), "OIL   %4.0f C", c.to);
      const ImVec4 pcol = c.p < 0.7f ? ImVec4(1.0f, 0.30f, 0.25f, 1.0f)
                          : c.p < 1.2f ? ImVec4(1.0f, 0.75f, 0.20f, 1.0f)
                                       : ImVec4(0.55f, 0.95f, 0.55f, 1.0f);
      ImGui::TextColored(pcol, "OIL P %4.1f bar", c.p);
      if (c.fi != 0) {
        // Gauge boost in bar (negative = vacuum) and the car's maximum.
        const float bar = (c.boost - kAtmPsi) / kPsiPerBar;
        const float max_bar = (c.boost_max - kAtmPsi) / kPsiPerBar;
        const ImVec4 bcol = c.fault == kBoostLeak ? ImVec4(1.0f, 0.75f, 0.20f, 1.0f) : ImVec4(0.55f, 0.85f, 1.0f, 1.0f);
        ImGui::TextColored(bcol, "%s %+5.2f bar", c.fi == 1 ? "TURBO" : "SUPER", bar);
        ImGui::SameLine();
        ImGui::TextDisabled("/ %.2f", max_bar);
      }
      ImGui::TextColored(Grade(c.damage, 0.10f, 0.30f), "ENGINE %3.0f%%", (1.0f - c.damage) * 100.0f);
      if (c.fault != kNone) {
        const bool bad = c.fault == kBlown || c.fault == kOilLeak;  // boost leak stays amber
        ImGui::TextColored(bad ? ImVec4(1.0f, 0.30f, 0.25f, 1.0f) : ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "%s",
                           FaultName(c.fault));
      }
      if (c.rad_health < 0.995f)
        ImGui::TextColored(Grade(1.0f - c.rad_health, 0.01f, 0.5f), "RADIATOR %3.0f%%", c.rad_health * 100.0f);
      if (c.draft > 0.2f) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "DRAFT  %.0f m", c.ahead);
      ImGui::PopFont();
      if (c.cool > 0.0f || c.wear > 0.0f)
        ImGui::TextDisabled("cooling tier %.0f  wear %.0f%%  build %.0f%%", c.cool, c.wear * 100.0f, c.build * 100.0f);
      ImGui::TextDisabled("%s  %.0f km/h  %.1f g  AIR %.0f C", c.damaging ? "heat damage ON" : "temps only",
                          c.speed * 3.6f, c.g, air);
      ImGui::TextDisabled("car %08X  (%zu on track)", car, count);
    }
    ImGui::End();
  }
};

}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverheatHud(rex::ui::ImGuiDrawer* drawer) {
  // No dialog at all when off, so the ImGui layer stays detached as before.
  if (!drawer || Mode() == 0 || !HudEnabled()) return nullptr;
  return std::make_unique<OverheatHud>(drawer);
}

// Options toggle (mods_menu.cpp): on by default; off = temperatures and HUD only, no damage or faults.
void EngineReliabilitySetEnabled(bool on) { g_reliability_on.store(on); }
bool EngineReliabilityEnabled() { return g_reliability_on.load(); }
// The garage / part-tune code supplies per-car data once when a car is first seen in a race:
// provider(car_physics_addr, cooling_tier, wear 0..1) returns true if it filled both. Not called per substep.
void EngineReliabilitySetProvider(EngineReliabilityProvider fn) { g_provider.store(fn); }
