// ---------------------------------------------------------------------------
// Upgrade shop filter: era-locked parts, rare parts, stock that varies after events (Forza-4-Evolution).
// Design: /mnt/project-files/evolution-research/upgrade-shop-implementation-plan-2026-10-20.md
//
// The shop lists, for the car being looked at, the rows of each List_Upgrade* table whose key column names that
// car (View_UpgradeParts, "... WHERE Ordinal = <car>"). There is no other filter, so a row whose key is made
// negative is not offered. This file does that, through the same game database helper the decade unlock uses
// (EvoDbQueue, which runs UPDATEs on the game's own tables, like EvoTick does on Events.Level):
//   * Era lock: a part tier above what the newest unlocked decade allows is hidden. Whole part types can wait
//     for a decade (turbos, intercoolers, wings).
//   * Rare parts: about one in six non-stock parts of tier 2 and up is "rare" and only on sale some of the time.
//     Each finished event (not an abandoned one) rolls a new day: sparse, normal or flooded.
//   * Never hidden: stock rows (IsStock=1), parts already bought (Career_PurchasedParts), engine swaps
//     (List_UpgradeEngine) and the car body / motor tables, so installed parts keep their physics.
// Every pass first puts back all hidden rows, then hides again from the current state, so the result only depends
// on the state (decade, roll, toggles) and nothing is stored in a text file. The game's database file on disk is not
// saved to, so a restart starts clean.
//
// Toggles (Mods page / F4 overlay, rows via garage thread): ShopEraLockEnabled / ShopRareEnabled / ShopStockVaryEnabled.
// Off = rows are put back at the next pass (the next frame after the change). FM4_SHOP_FILTER=0 turns all off.
// Entry point: ShopFilterInit() once, early (it registers the per-frame tick). Not in the build lists until the
// garage thread adds it.
//
// Needs one in-game test: that a negative key hides a part cleanly in the shop and nothing else reads those rows.
// ---------------------------------------------------------------------------
#include <rex/cvar.h>
#include <rex/logging.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>

REXCVAR_DEFINE_BOOL(fm4_shop_era_lock, true, "Forza Motorsport 4/Evolution",
                    "Upgrade shop: part tiers and types are held back until their decade (applies at the next pass)");
REXCVAR_DEFINE_BOOL(fm4_shop_rare_parts, true, "Forza Motorsport 4/Evolution",
                    "Upgrade shop: rare parts are only on sale some of the time");
REXCVAR_DEFINE_BOOL(fm4_shop_stock_vary, true, "Forza Motorsport 4/Evolution",
                    "Upgrade shop: which rare parts are on sale re-rolls after each finished event");

// dev_keys.cpp (Evolution database helper and frame hooks).
void EvoDbQueue(const char* sql);
void EvoDbRead(const char* sql, const char* const* column_names, int count, void (*cb)(const int32_t* values, int count));
bool EvoProfileReady();
uint32_t EvoProfileGeneration();
void EvoOnFrame(void (*cb)());
uint32_t EvoResultSerial();
void EvoLastResult(uint32_t* event_id, int* place);
int EvoNewestUnlockedDecadeYear();

namespace {

struct ShopTable {
  const char* name;
  const char* key;      // the column that names the car / engine / body / drivetrain
  int max_level;        // highest Level in the base game
  int min_decade;       // 0 = 1960s ... 6 = 2020s; non-stock rows are all hidden before this decade
};

constexpr ShopTable kTables[] = {
    {"List_UpgradeAntiSwayFront", "Ordinal", 3, 0},
    {"List_UpgradeAntiSwayRear", "Ordinal", 3, 0},
    {"List_UpgradeBrakes", "Ordinal", 3, 0},
    {"List_UpgradeCarBodyChassisStiffness", "CarbodyId", 3, 1},
    {"List_UpgradeCarBodyFrontBumper", "CarBodyID", 4, 0},
    {"List_UpgradeCarBodyHood", "CarBodyID", 1, 0},
    {"List_UpgradeCarBodyRearBumper", "CarBodyID", 4, 0},
    {"List_UpgradeCarBodySideSkirt", "CarBodyID", 1, 0},
    {"List_UpgradeCarBodyTireWidthFront", "CarBodyId", 9, 0},
    {"List_UpgradeCarBodyTireWidthRear", "CarBodyId", 9, 0},
    {"List_UpgradeCarBodyWeight", "CarBodyId", 3, 0},
    {"List_UpgradeDrivetrain", "Ordinal", 2, 0},
    {"List_UpgradeDrivetrainClutch", "DrivetrainID", 3, 0},
    {"List_UpgradeDrivetrainDifferential", "DrivetrainID", 3, 0},
    {"List_UpgradeDrivetrainDriveline", "DrivetrainId", 3, 0},
    {"List_UpgradeDrivetrainTransmission", "DrivetrainID", 3, 0},
    {"List_UpgradeEngineCamshaft", "EngineID", 3, 0},
    {"List_UpgradeEngineDisplacement", "EngineID", 3, 0},
    {"List_UpgradeEngineExhaust", "EngineID", 3, 0},
    {"List_UpgradeEngineFlywheel", "EngineID", 3, 0},
    {"List_UpgradeEngineFuelSystem", "EngineID", 3, 1},
    {"List_UpgradeEngineIgnition", "EngineID", 3, 0},
    {"List_UpgradeEngineIntake", "EngineID", 3, 0},
    {"List_UpgradeEngineIntercooler", "EngineID", 3, 2},
    {"List_UpgradeEngineManifold", "EngineID", 3, 0},
    {"List_UpgradeEngineOilCooling", "EngineID", 3, 0},
    {"List_UpgradeEnginePistonsCompression", "EngineID", 3, 0},
    {"List_UpgradeEngineRestrictorPlate", "EngineID", 2, 0},
    {"List_UpgradeEngineTurboSingle", "EngineID", 3, 1},
    {"List_UpgradeEngineTurboTwin", "EngineID", 3, 3},
    {"List_UpgradeEngineValves", "EngineID", 3, 0},
    {"List_UpgradeRearWing", "Ordinal", 4, 1},
    {"List_UpgradeRimSizeFront", "Ordinal", 7, 0},
    {"List_UpgradeRimSizeRear", "Ordinal", 7, 0},
    {"List_UpgradeSpringDamper", "Ordinal", 3, 0},
    {"List_UpgradeTireCompound", "Ordinal", 4, 0},
};

constexpr int kDecades = 7;

bool g_registered = false;
bool g_have_state = false;
uint32_t g_gen = 0;
uint32_t g_serial = 0;
bool g_have_serial = false;
uint32_t g_roll = 0;
int g_rare_pct = 60;           // share of rare parts on sale today
int g_applied_decade = -2;
int g_applied_roll = -1;
int g_applied_flags = -1;
std::atomic<int> g_is_evo{-1};  // -1 not asked yet, 0 no Evolution pack in this game, 1 Evolution pack present

void OnEvoCount(const int32_t* values, int count) { g_is_evo = (count > 0 && values[0] > 0) ? 1 : 0; }

std::mt19937& Rng() {
  static std::mt19937 rng{std::random_device{}()};
  return rng;
}

bool EnvOn() {
  static const bool on = [] {
    const char* v = std::getenv("FM4_SHOP_FILTER");
    return !(v && v[0] == '0');
  }();
  return on;
}

int Flags() {
  if (!EnvOn()) return 0;
  return (REXCVAR_GET(fm4_shop_era_lock) ? 1 : 0) | (REXCVAR_GET(fm4_shop_rare_parts) ? 2 : 0);
}

// 25% sparse day, 50% normal, 25% flooded.
void NewDay() {
  ++g_roll;
  const int r = int(Rng()() % 100);
  g_rare_pct = r < 25 ? 30 : (r < 75 ? 60 : 90);
  REXLOG_INFO("Shop filter: new day {} (rare parts on sale {}%)", g_roll, g_rare_pct);
}

// Highest tier on sale in decade d (0..6) for a table whose base game goes up to max_level.
int AllowedLevel(int max_level, int d) {
  const int v = (max_level * (d + 1) + 3) / kDecades;
  return v < 1 ? 1 : v;
}

void Apply(int decade, int flags) {
  const std::string owned = "(SELECT PartId FROM {P}Career_PurchasedParts)";
  for (const ShopTable& t : kTables) {
    const std::string tbl = t.name, key = t.key;
    // Put back everything hidden by an earlier pass.
    EvoDbQueue(("UPDATE " + tbl + " SET " + key + "=-" + key + " WHERE " + key + "<0").c_str());
    std::string cond;
    if (flags & 1) {
      if (decade < t.min_decade) cond = "1";
      else cond = "Level>" + std::to_string(AllowedLevel(t.max_level, decade));
    }
    if (flags & 2) {
      // Rare: tier 2 and up, one id in six. On sale when the day's roll lets it through.
      const std::string rare = "(Level>=2 AND Id%6=0 AND ((Id/6)*7+" + std::to_string(g_roll * 13) + ")%100>=" +
                               std::to_string(g_rare_pct) + ")";
      cond = cond.empty() ? rare : "(" + cond + " OR " + rare + ")";
    }
    if (cond.empty()) continue;
    EvoDbQueue(("UPDATE " + tbl + " SET " + key + "=-" + key + " WHERE " + key + ">0 AND IsStock=0 AND Id NOT IN " + owned +
                " AND " + cond)
                   .c_str());
  }
}

void Tick() {
  if (!EvoProfileReady()) return;
  const uint32_t gen = EvoProfileGeneration();
  bool day_changed = false;
  if (!g_have_state || gen != g_gen) {
    g_have_state = true;
    g_gen = gen;
    g_roll = 0;
    NewDay();
    g_serial = EvoResultSerial();
    g_have_serial = true;
    g_applied_decade = -2;  // force a pass
    day_changed = true;
    g_is_evo = -1;
    static const char* const kCols[] = {"c"};
    EvoDbRead("SELECT COUNT(*) AS c FROM Events WHERE Id=2000", kCols, 1, OnEvoCount);
  } else {
    const uint32_t serial = EvoResultSerial();
    if (!g_have_serial) {
      g_serial = serial;
      g_have_serial = true;
    } else if (serial != g_serial) {
      g_serial = serial;
      uint32_t event_id = 0;
      int place = 0;
      EvoLastResult(&event_id, &place);
      if (place >= 1 && place <= 16 && REXCVAR_GET(fm4_shop_stock_vary)) {  // a finished event, not an abandoned one
        NewDay();
        day_changed = true;
      }
    }
  }
  // Only with the Evolution pack's events present; any other career is left alone (and any hidden rows put back).
  const int evo = g_is_evo.load();
  if (evo < 0) return;
  int decade = (EvoNewestUnlockedDecadeYear() - 1960) / 10;
  if (decade < 0) decade = 0;
  if (decade >= kDecades) decade = kDecades - 1;
  const int flags = evo ? Flags() : 0;
  if (!day_changed && decade == g_applied_decade && int(g_roll) == g_applied_roll && flags == g_applied_flags) return;
  g_applied_decade = decade;
  g_applied_roll = int(g_roll);
  g_applied_flags = flags;
  Apply(decade, flags);
}

}  // namespace

void ShopFilterInit() {
  if (g_registered) return;
  g_registered = true;
  EvoOnFrame(Tick);
}

bool ShopEraLockEnabled() { return REXCVAR_GET(fm4_shop_era_lock); }
void ShopEraLockSetEnabled(bool on) { REXCVAR_SET(fm4_shop_era_lock, on); }
bool ShopRareEnabled() { return REXCVAR_GET(fm4_shop_rare_parts); }
void ShopRareSetEnabled(bool on) { REXCVAR_SET(fm4_shop_rare_parts, on); }
bool ShopStockVaryEnabled() { return REXCVAR_GET(fm4_shop_stock_vary); }
void ShopStockVarySetEnabled(bool on) { REXCVAR_SET(fm4_shop_stock_vary, on); }
