// ---------------------------------------------------------------------------
// Rotating used car lot: the listings (Forza-4-Evolution).
// Design: /mnt/project-files/evolution-research/upgrade-shop-implementation-plan-2026-10-20.md
//         and used-dealership-screen-side-proposal-2026-10-20.md (the screen side is garage_menu.cpp).
//
// This file owns WHAT is for sale; garage_menu.cpp owns how the buy screens show it.
//   * A roll fills the profile table {P}Evolution_UsedLot with 3 / 8 / 16 listings (sparse / normal /
//     flooded day): car, advertised price, mileage and a hidden build kind (untouched, street, upgraded,
//     wild, swapped down, broken). Older cars, longer mileage and bad builds go together; broken and
//     swapped-down builds are cheap but only revealed after the purchase.
//   * The lot re-rolls when the profile loads and after every completed event (not an abandoned one):
//     EvoResultSerial() rises on each recorded result, EvoLastResult() place 1..16 = event complete.
//   * All state is in the profile database through EvoDbQueue / EvoDbRead (dev_keys.cpp). Reads return
//     the first row only, so the listings are loaded one slot at a time. No text files.
//   * The roll is plain SQL (tested on a copy of gamedb.slt: evo-parts-patch/used_lot_roll.sql).
//
// Not done yet: applying the hidden build (swapped-down engine, wear, broken parts) to the bought car
// (UsedLotOnPurchase only records it in {P}Evolution_UsedBought), hiding PI and class on the list, and the
// listing thumbnail (thumb_car_id is the car itself for now).
//
// Interface for garage_menu.cpp (call on the game thread, no struct shared, so the two files cannot drift):
//   int      UsedLotCount();
//   bool     UsedLotGet(int index, uint32_t* car_id, uint32_t* price, uint32_t* mileage_km,
//                       bool* condition_unknown, uint32_t* thumb_car_id);
//   uint32_t UsedLotVersion();            // changes whenever the loaded listings change
//   void     UsedLotOnPurchase(uint32_t car_id);
//   void     UsedLotInit();               // optional: any call above also registers the frame tick
// Mods page toggle: UsedLotRotationUserEnabled() / UsedLotSetRotationUserEnabled(bool).
//
// Off (fm4_used_lot_rotation false) = no rolls, no listings (UsedLotCount() is 0), garage_menu.cpp falls
// back to the plain older-cars list. FM4_USED_LOT=0 in the environment also turns it off.
// ---------------------------------------------------------------------------
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

// F4 overlay: Forza Motorsport 4 > Evolution. Also the Mods page Features tab.
REXCVAR_DEFINE_BOOL(fm4_used_lot_rotation, true, "Forza Motorsport 4/Evolution",
                    "Rotating used car lot (off = the plain older-cars list). The lot re-rolls when the profile "
                    "loads and after each completed event");

// dev_keys.cpp (Evolution database helper and frame hooks).
void EvoDbQueue(const char* sql);
void EvoDbRead(const char* sql, const char* const* column_names, int count, void (*cb)(const int32_t* values, int count));
bool EvoProfileReady();
uint32_t EvoProfileGeneration();
void EvoOnFrame(void (*cb)());
uint32_t EvoResultSerial();
void EvoLastResult(uint32_t* event_id, int* place);
int EvoNewestUnlockedDecadeYear();
// garage_menu.cpp: true while the buy screens were opened from USED CAR DEALERSHIP.
bool GarageUsedLotActive();

namespace {

struct Listing {
  uint32_t car_id = 0;
  uint32_t price = 0;
  uint32_t mileage_km = 0;
  int kind = 0;  // hidden build kind, see used_lot_roll.sql
};

std::vector<Listing> g_lot;       // what the garage screen sees
std::vector<Listing> g_loading;   // being read slot by slot
std::atomic<uint32_t> g_version{1};
bool g_registered = false;
uint32_t g_gen = 0;               // profile generation the tables were prepared for
bool g_gen_valid = false;
bool g_have_serial = false;
uint32_t g_serial = 0;
uint32_t g_roll_no = 0;
int g_expected = 0;
uint32_t g_load_gen = 0;          // generation the running load belongs to
std::mt19937& Rng() {
  static std::mt19937 rng{std::random_device{}()};
  return rng;
}

bool Enabled() {
  static const bool env_on = [] {
    const char* v = std::getenv("FM4_USED_LOT");
    return !(v && v[0] == '0');
  }();
  return env_on && REXCVAR_GET(fm4_used_lot_rotation);
}

uint32_t Load32(uint32_t a) {
  const uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(a);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

std::string Replace(std::string s, const std::string& from, const std::string& to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  return s;
}

constexpr const char* kCreateLot =
    "CREATE TABLE IF NOT EXISTS {P}Evolution_UsedLot (Slot INTEGER PRIMARY KEY, CarId INTEGER, PriceCr INTEGER, "
    "MileageKm INTEGER, BuildKind INTEGER, RollNo INTEGER)";
constexpr const char* kCreateBought =
    "CREATE TABLE IF NOT EXISTS {P}Evolution_UsedBought (CarId INTEGER, MileageKm INTEGER, BuildKind INTEGER, "
    "BoughtAt INTEGER)";

constexpr const char* kRollDelete = "DELETE FROM {P}Evolution_UsedLot";
// Same statement as evo-parts-patch/used_lot_roll.sql (tested there). @DECADE / @COUNT / @ROLL are filled in below.
constexpr const char* kRollInsert =
    "INSERT INTO {P}Evolution_UsedLot (CarId, PriceCr, MileageKm, BuildKind, RollNo) "
    "SELECT c.Id, MAX(1500, CAST(ROUND(c.BaseCost * MAX(0.12, 0.85 - (@DECADE - c.Year) * 0.012 - c.km / 900000.0) "
    "* k.PriceMod / 100.0) * 100 AS INTEGER)), c.km, k.Kind, @ROLL "
    "FROM (SELECT d.Id, d.Year, d.BaseCost, (@DECADE - d.Year) * (4000 + ABS(RANDOM()) % 9000) AS km, "
    "ABS(RANDOM()) % 100 AS r_good, ABS(RANDOM()) % 100 AS r_bad, ABS(RANDOM()) % 100 AS r_down "
    "FROM Data_Car d WHERE d.Year < @DECADE AND d.Year >= 1930 AND d.BaseCost > 0 AND d.IsUnicorn = 0 "
    "AND d.IsDrivable = 1 ORDER BY (ABS(RANDOM()) % 1000) * (1.0 + COALESCE(d.BaseRarity, 0) * 0.5) LIMIT @COUNT) AS c "
    "JOIN (SELECT 0 AS Kind, 1.0 AS PriceMod UNION ALL SELECT 1, 1.05 UNION ALL SELECT 2, 1.15 UNION ALL "
    "SELECT 3, 1.3 UNION ALL SELECT 4, 0.55 UNION ALL SELECT 5, 0.4) AS k ON k.Kind = CASE "
    "WHEN c.r_bad < MIN(45, 5 + c.km / 12000) THEN CASE WHEN c.r_down < 65 THEN 4 ELSE 5 END "
    "WHEN c.r_good < 35 THEN 0 WHEN c.r_good < 75 THEN 1 WHEN c.r_good < 93 THEN 2 ELSE 3 END";

void Tick();
void LoadSlot(int slot);

// Applies the hidden build to the car just bought (newest garage row of that model). The wear model (part_tune.cpp)
// reads DistanceDriven (km), NumOwners and TotalRepairs, so mileage and history feed it directly. Kinds 4 (swapped
// down) and 5 (broken) replace the engine with a weaker one from another car's stock engine row and scale the cached
// PI; the game recomputes PI properly the next time the car is changed in the shop. [T] untested in game.
void ApplyBuild(uint32_t car_id, const Listing& l) {
  const std::string id = std::to_string(car_id);
  const std::string row = "(SELECT MAX(Id) FROM {P}Career_Garage WHERE CarId=" + id + ")";
  const int owners = 1 + int(l.mileage_km / 60000) + (l.kind >= 4 ? 1 : 0);
  const int repairs = l.kind == 5 ? 4 : (l.kind == 4 ? 2 : 0);
  EvoDbQueue(("UPDATE {P}Career_Garage SET DistanceDriven=" + std::to_string(l.mileage_km) + ", NumOwners=" +
              std::to_string(owners) + ", TotalRepairs=" + std::to_string(repairs) + " WHERE Id=" + row).c_str());
  if (l.kind < 4) return;
  const std::string limit = l.kind == 5 ? "0.35" : "0.65";  // donor power below this share of the car's own
  EvoDbQueue(("UPDATE {P}Career_Garage SET Engine=COALESCE((SELECT u.Id FROM List_UpgradeEngine u JOIN Data_Car dc "
              "ON dc.Id=u.Ordinal WHERE u.IsStock=1 AND dc.SimPeakPower>0 AND dc.SimPeakPower < " + limit +
              " * (SELECT SimPeakPower FROM Data_Car WHERE Id=" + id + ") ORDER BY RANDOM() LIMIT 1), Engine) WHERE Id=" +
              row).c_str());
  EvoDbQueue(("UPDATE {P}Career_Garage SET PerformanceIndex=PerformanceIndex * MIN(1.0, 0.4 + 0.6 * COALESCE(("
              "SELECT dc.SimPeakPower FROM List_UpgradeEngine u JOIN Data_Car dc ON dc.Id=u.Ordinal WHERE u.Id="
              "{P}Career_Garage.Engine) / (SELECT SimPeakPower FROM Data_Car WHERE Id=" + id + "), 1.0)) WHERE Id=" +
              row + " AND Engine<>" + std::to_string(car_id * 1000u)).c_str());
}

// ---- loading the listings back, one slot per read ------------------------------------------------------------
void OnSlot(const int32_t* v, int n) {
  if (g_load_gen != EvoProfileGeneration()) return;  // profile switched while reading: drop it
  if (!v || n < 5) { g_expected = 0; }
  else {
    Listing l;
    l.car_id = uint32_t(v[0]);
    l.price = uint32_t(v[1]);
    l.mileage_km = uint32_t(v[2]);
    l.kind = v[3];
    if (uint32_t(v[4]) > g_roll_no) g_roll_no = uint32_t(v[4]);
    g_loading.push_back(l);
  }
  if (int(g_loading.size()) < g_expected) { LoadSlot(int(g_loading.size()) + 1); return; }
  g_lot = g_loading;
  ++g_version;
  REXLOG_INFO("Used lot: {} listings loaded (roll {})", g_lot.size(), g_roll_no);
}

void LoadSlot(int slot) {
  static const char* const kCols[] = {"C", "P", "M", "B", "R"};
  const std::string sql = "SELECT CarId AS C, PriceCr AS P, MileageKm AS M, BuildKind AS B, RollNo AS R FROM "
                          "{P}Evolution_UsedLot WHERE Slot=" + std::to_string(slot);
  EvoDbRead(sql.c_str(), kCols, 5, OnSlot);
}

void OnCount(const int32_t* v, int n) {
  if (g_load_gen != EvoProfileGeneration()) return;
  g_expected = (v && n >= 1) ? v[0] : 0;
  g_loading.clear();
  if (g_expected <= 0) { g_lot.clear(); ++g_version; return; }
  LoadSlot(1);
}

void StartLoad() {
  static const char* const kCols[] = {"N"};
  g_load_gen = EvoProfileGeneration();
  EvoDbRead("SELECT COUNT(*) AS N FROM {P}Evolution_UsedLot", kCols, 1, OnCount);
}

// ---- rolling -----------------------------------------------------------------------------------------------------
// Day mood: 25% sparse (3 listings), 50% normal (8), 25% flooded (16).
int PickCount() {
  const int r = int(Rng()() % 100);
  return r < 25 ? 3 : (r < 75 ? 8 : 16);
}

void Roll() {
  const int decade = EvoNewestUnlockedDecadeYear();  // cars older than this decade's start year
  const int count = PickCount();
  ++g_roll_no;
  std::string ins = kRollInsert;
  ins = Replace(ins, "@DECADE", std::to_string(decade));
  ins = Replace(ins, "@COUNT", std::to_string(count));
  ins = Replace(ins, "@ROLL", std::to_string(g_roll_no));
  EvoDbQueue(kRollDelete);
  EvoDbQueue(ins.c_str());
  REXLOG_INFO("Used lot: roll {} (older than {}, up to {} listings)", g_roll_no, decade, count);
  StartLoad();  // queued after the insert, so it reads the new rows
}

// ---- once per frame (game thread) --------------------------------------------------------------------------------
void Tick() {
  if (!Enabled() || !EvoProfileReady()) return;
  const uint32_t gen = EvoProfileGeneration();
  if (!g_gen_valid || gen != g_gen) {
    g_gen = gen;
    g_gen_valid = true;
    g_lot.clear();
    g_loading.clear();
    g_roll_no = 0;
    ++g_version;
    EvoDbQueue(kCreateLot);
    EvoDbQueue(kCreateBought);
    Roll();  // a new session / profile gets a fresh lot
    g_serial = EvoResultSerial();
    g_have_serial = true;
    return;
  }
  const uint32_t serial = EvoResultSerial();
  if (!g_have_serial) { g_serial = serial; g_have_serial = true; return; }
  if (serial == g_serial) return;
  g_serial = serial;
  uint32_t event_id = 0;
  int place = 0;
  EvoLastResult(&event_id, &place);
  if (place >= 1 && place <= 16) {  // the event is complete (17 = races still pending)
    REXLOG_INFO("Used lot: event {} complete", event_id);
    Roll();
  }
}

}  // namespace

// ---- interface for garage_menu.cpp -------------------------------------------------------------------------------
void UsedLotInit() {
  if (g_registered) return;
  g_registered = true;
  EvoOnFrame(Tick);
}

int UsedLotCount() {
  UsedLotInit();
  return Enabled() ? int(g_lot.size()) : 0;
}

bool UsedLotGet(int index, uint32_t* car_id, uint32_t* price, uint32_t* mileage_km, bool* condition_unknown,
                uint32_t* thumb_car_id) {
  UsedLotInit();
  if (!Enabled() || index < 0 || index >= int(g_lot.size())) return false;
  const Listing& l = g_lot[size_t(index)];
  if (car_id) *car_id = l.car_id;
  if (price) *price = l.price;
  if (mileage_km) *mileage_km = l.mileage_km;
  if (condition_unknown) *condition_unknown = true;  // PI, class and the real build stay hidden until bought
  if (thumb_car_id) *thumb_car_id = l.car_id;
  return true;
}

// Build kind of a listed car (0 untouched, 1 street, 2 upgraded, 3 wild, 4 swapped down, 5 broken), -1 when it is not listed.
// For the status box (used_lot_overlay.cpp); it only shows it when the reveal switch is on.
int UsedLotKindForCar(uint32_t car_id) {
  for (const Listing& l : g_lot)
    if (l.car_id == car_id) return l.kind;
  return -1;
}

uint32_t UsedLotVersion() { return g_version.load(); }

// The hidden build is only recorded for now; applying it to the new garage car is the next step.
void UsedLotOnPurchase(uint32_t car_id) {
  UsedLotInit();
  for (size_t i = 0; i < g_lot.size(); ++i) {
    if (g_lot[i].car_id != car_id) continue;
    const std::string id = std::to_string(car_id);
    EvoDbQueue(("INSERT INTO {P}Evolution_UsedBought (CarId, MileageKm, BuildKind, BoughtAt) SELECT CarId, MileageKm, "
                "BuildKind, CAST(strftime('%s','now') AS INTEGER) FROM {P}Evolution_UsedLot WHERE CarId=" + id).c_str());
    EvoDbQueue(("DELETE FROM {P}Evolution_UsedLot WHERE CarId=" + id).c_str());
    ApplyBuild(car_id, g_lot[i]);
    REXLOG_INFO("Used lot: car {} bought (build kind {} recorded)", car_id, g_lot[i].kind);
    g_lot.erase(g_lot.begin() + long(i));
    ++g_version;
    return;
  }
}

bool UsedLotRotationUserEnabled() { return REXCVAR_GET(fm4_used_lot_rotation); }
void UsedLotSetRotationUserEnabled(bool on) { REXCVAR_SET(fm4_used_lot_rotation, on); }

// Hook UsedLotBought, 0x824332D8 in the purchase function sub_82433230: the car has just been added to the garage
// (credits were checked and not yet taken). r31 = the bought car id. Only a purchase made from USED CAR DEALERSHIP
// (car-list flow 11) takes the listing out of the lot.
void UsedLotBought(PPCRegister& r31) {
  if (!Enabled() || !GarageUsedLotActive()) return;
  const uint32_t g = Load32(0x82DC3828);  // G; G+16 = car-list flow (11 = buy new)
  if (g < 0x10000 || Load32(g + 16) != 11) return;
  UsedLotOnPurchase(r31.u32);
}
