// Scoped FM4 startup-media hooks.

#include <rex/ppc.h>

void EnableTurn10MovieSkip(PPCRegister& r29) {
  // The following store writes descriptor + 0x28.
  r29.u64 = 1;
}

void RestoreTurn10MovieMode(PPCRegister& r29) {
  // Do not change the mode used by later movie descriptors.
  r29.u64 = 2;
}

void EnableClarksonVoiceoverSkip(PPCRegister& r3) {
  // The previous instruction loaded the allowskipvoiceover byte into r3.
  r3.u64 = 1;
}

// ---------------------------------------------------------------------------
// Optional serializer trace (for reverse engineering .carbin model files).
// Disabled unless the environment variable FM4_SERIAL_TRACE names an output
// file. Logs entries of the archive primitives used by model/carbin loading,
// so the exact field order and the code reading each field can be recovered.
//   P <kind> caller arch ptr a5   primitive entry (caller = return address)
//   R arch n                      raw stream read of n bytes (vtable +0x30)
//   B caller arch maxver name     versioned container begin
//   M this arch                   CarModel::Serialize entry
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace {
std::mutex g_trace_mutex;
FILE* g_trace = nullptr;
int g_trace_state = 0;  // 0 = unknown, 1 = on, -1 = off
unsigned g_trace_lines = 0;

bool TraceOn() {
  if (g_trace_state == 0) {
    const char* path = std::getenv("FM4_SERIAL_TRACE");
    g_trace = (path && *path) ? std::fopen(path, "w") : nullptr;
    g_trace_state = g_trace ? 1 : -1;
  }
  return g_trace_state == 1;
}

void TraceLine(const char* text) {
  std::fputs(text, g_trace);
  if (++g_trace_lines % 64 == 0) std::fflush(g_trace);
}

const char* GuestString(uint32_t address, char* out, size_t size) {
  out[0] = 0;
  if (!address) return out;
  auto* p = REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(address);
  size_t i = 0;
  for (; i + 1 < size && p[i] >= 32 && p[i] < 127; ++i) out[i] = p[i];
  out[i] = 0;
  return out;
}

void TracePrim(const char* kind, uint32_t caller, uint32_t arch, uint32_t ptr, uint32_t a5) {
  std::lock_guard<std::mutex> lock(g_trace_mutex);
  if (!TraceOn()) return;
  char line[128];
  std::snprintf(line, sizeof(line), "P %s %08X %08X %08X %08X\n", kind, caller, arch, ptr, a5);
  TraceLine(line);
}
}  // namespace

void TraceSerU32(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("u32", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerU16(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("u16", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerU8(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("u8", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerU64(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("u64", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerBytes(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("bytes", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerBool(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("bool", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerVec4(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("vec4", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerVec2(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("vec2", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerMatrix(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("mtx", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerString(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("str", r12.u32, r3.u32, r4.u32, r5.u32); }
void TraceSerWString(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) { TracePrim("wstr", r12.u32, r3.u32, r4.u32, r5.u32); }

void TraceSerRead(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5) {
  std::lock_guard<std::mutex> lock(g_trace_mutex);
  if (!TraceOn()) return;
  char line[64];
  std::snprintf(line, sizeof(line), "R %08X %08X\n", r3.u32, r5.u32);
  TraceLine(line);
}

void TraceSerBegin(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12) {
  std::lock_guard<std::mutex> lock(g_trace_mutex);
  if (!TraceOn()) return;
  char name[96];
  char line[192];
  std::snprintf(line, sizeof(line), "B %08X %08X %u %s\n", r12.u32, r3.u32, r5.u32, GuestString(r4.u32, name, sizeof(name)));
  TraceLine(line);
  std::fflush(g_trace);
}

void TraceSerCarModel(PPCRegister& r3, PPCRegister& r4) {
  std::lock_guard<std::mutex> lock(g_trace_mutex);
  if (!TraceOn()) return;
  char line[64];
  std::snprintf(line, sizeof(line), "M %08X %08X\n", r3.u32, r4.u32);
  TraceLine(line);
  std::fflush(g_trace);
}

// ---------------------------------------------------------------------------
// Event List grid scrolling (Forza-4-Evolution: custom career events).
// The Event List (scene 242_c_cal_grid) is a ForzaUI::CGridControl with
// 10 rows and a pool of 310 tile nodes, authored as a non-scrolling grid
// (BOOLEAN_ANIMATED = 0). A non-scrolling grid only ever shows its pool, so
// event 311+ can never be reached. A scrolling grid needs a pool of
// (visibleColumns + 1) x rows tiles, so for this grid we set
// visibleColumns = pool / rows - 1 (31 - 1 = 30; the spare column is recycled
// while scrolling) and turn scrolling on.
//   1. EnableEventGridScrolling (0x826464B4, after the pool is filled):
//      recognise the grid (rows 10, pool 310), set NUM_COLS, remember it.
//      BOOLEAN_ANIMATED is left 0 here so setup still reads LONG_GROUP_SPACING.
//   2. EnableEventGridScrollingLate (0x82646540, end of that setup step):
//      set BOOLEAN_ANIMATED = 1.
//   3. KeepEventGridSectionGaps (0x82642C28, layout loop): the layout adds the
//      section gap only when BOOLEAN_ANIMATED is 0, so report 0 there.
// CGridControl fields: +320 rows, +324 visible columns, +340 group spacing,
// +344 BOOLEAN_ANIMATED, +672/+676 tile pool vector (begin/end).
// Set FM4_EVENT_GRID_SCROLL=0 to disable all three.
// ---------------------------------------------------------------------------
#include <atomic>
namespace {
std::atomic<uint32_t> g_event_grid{0};

bool EventGridScrollEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("FM4_EVENT_GRID_SCROLL");
    return !(v && v[0] == '0');
  }();
  return enabled;
}
uint32_t LoadBE32(uint32_t address) {
  auto* p = REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(address);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
void StoreBE32(uint32_t address, uint32_t value) {
  auto* p = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address);
  p[0] = uint8_t(value >> 24); p[1] = uint8_t(value >> 16); p[2] = uint8_t(value >> 8); p[3] = uint8_t(value);
}
}  // namespace

void EnableEventGridScrolling(PPCRegister& r31) {
  if (!EventGridScrollEnabled()) return;
  const uint32_t grid = r31.u32;
  const uint32_t rows = LoadBE32(grid + 320);
  const uint32_t pool_begin = LoadBE32(grid + 672);
  const uint32_t pool_end = LoadBE32(grid + 676);
  const bool is_event_grid = rows == 10 && pool_end >= pool_begin && (pool_end - pool_begin) / 4 == 310;
  if (!is_event_grid) {
    uint32_t expected = grid;  // a different grid now lives at this address
    g_event_grid.compare_exchange_strong(expected, 0);
    return;
  }
  StoreBE32(grid + 324, (pool_end - pool_begin) / 4 / rows - 1);  // 30 visible columns + 1 spare
  g_event_grid = grid;
}

void EnableEventGridScrollingLate(PPCRegister& r31) {
  if (g_event_grid.load() != r31.u32 || r31.u32 == 0) return;
  *REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(r31.u32 + 344) = 1;  // BOOLEAN_ANIMATED
}

void KeepEventGridSectionGaps(PPCRegister& r10, PPCRegister& r31) {
  if (g_event_grid.load() == r31.u32 && r31.u32 != 0) r10.u64 = 0;
}

// ---------------------------------------------------------------------------
// Event prize cars (Forza-4-Evolution).
// Finishing every race of a career event for the first time earns a prize car,
// offered through the game's own level-up "choose a reward car" popup.
//
// How the stock level-up gift works (see claude/prize-car-rewards-2026-10-12.md):
//   0x824495E8 GetRewardCars(out, lastRewardedLevel, level) queries
//              Rewards_DriverLevelv3 for DriverLevel in (last, level].
//   0x8245C488 turns those rows into queued rewards (one item per level:
//              a choice popup when the level has several cars, a direct gift
//              when it has one). Called from 0x8249D3C8 and 0x8258C8F8, only
//              when Main/Level > Main/LastRewardedLevel.
//   0x8250D230 the popup: SELECT Data_Car.* ... WHERE DriverLevel=%d LIMIT %d.
//   0x82454400 flushes finished rewards and writes Main/LastRewardedLevel.
//
// We give each completed event a fake driver level, kPrizeLevelBase + EventId
// (real levels stop at 50), and:
//   * remember newly completed events (0x8248C708 records race/event results;
//     an event's Place is 17 until all its races have a result);
//   * let the reward queue be built while events are pending;
//   * add each pending event's cars to the queue query, and serve them to the
//     popup. Each event gives ONE random car from its pool, through the game's
//     single-car gift screen (CGiftCarReward). Pool: normal events = the
//     event's EventParticipants cars, top half by PerformanceIndex; one-make
//     events = cars of the same make, same class or one higher. All done in
//     SQL, so DLC and ported events work too;
//   * show the event name as the popup title;
//   * never let a fake level reach Main/LastRewardedLevel.
// Pending events are kept in fm4_prize_cars_pending.txt next to the exe.
// Set FM4_PRIZE_CARS=0 to disable.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <cstring>
#include <filesystem>
#include <rex/filesystem.h>
#include <rex/logging.h>

namespace {
constexpr uint32_t kPrizeLevelBase = 10000;
constexpr size_t kPrizeMaxEventsPerQuery = 8;
constexpr uint32_t kPrizePopupMaxCars = 6;
constexpr uint32_t kQueueQueryBufferSize = 16384;
constexpr uint32_t kPopupQueryBufferSize = 8192;

std::recursive_mutex g_prize_mutex;
bool g_prize_loaded = false;
std::map<uint32_t, uint32_t> g_prize_pending;  // EventId -> seed, waiting for their prize
std::set<uint32_t> g_prize_seen;       // fake levels seen while building the queue
bool g_prize_building = false;
uint32_t g_prize_range_lo = 0, g_prize_range_hi = 0;
bool g_prize_range_ok = false;
uint32_t g_prize_flush_prev = 0;
bool g_prize_title_swap = false;
uint32_t g_prize_queue_buf = 0, g_prize_popup_buf = 0;
uint32_t g_prize_title_fmt = 0, g_prize_title_table = 0;

bool PrizeCarsEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("FM4_PRIZE_CARS");
    return !(v && v[0] == '0');
  }();
  return enabled;
}

bool IsPrizeLevel(uint32_t level) { return level >= kPrizeLevelBase && level < kPrizeLevelBase + 100000; }

std::filesystem::path PrizePendingPath() {
  return rex::filesystem::GetExecutableFolder() / "fm4_prize_cars_pending.txt";
}

void PrizeLoad() {
  if (g_prize_loaded) return;
  g_prize_loaded = true;
  std::ifstream in(PrizePendingPath());
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    uint32_t id = 0, seed = 1;
    if (!(fields >> id)) continue;
    fields >> seed;
    g_prize_pending[id] = seed ? seed : 1;
  }
  if (!g_prize_pending.empty()) REXLOG_INFO("Prize cars: {} event(s) pending", g_prize_pending.size());
}

void PrizeSave() {
  std::ofstream out(PrizePendingPath(), std::ios::trunc);
  for (const auto& [id, seed] : g_prize_pending) out << id << ' ' << seed << '\n';
}

uint32_t GuestAlloc(uint32_t size) { return REX_KERNEL_MEMORY()->SystemHeapAlloc(size); }

void GuestWriteString(uint32_t address, const std::string& text, uint32_t capacity) {
  auto* p = REX_KERNEL_MEMORY()->TranslateVirtual<char*>(address);
  const size_t n = std::min<size_t>(text.size(), capacity - 1);
  std::memcpy(p, text.data(), n);
  p[n] = 0;
}

uint32_t GuestConstString(uint32_t& slot, const char* text) {
  if (!slot) {
    const uint32_t size = uint32_t(std::strlen(text)) + 1;
    slot = GuestAlloc(size);
    if (slot) GuestWriteString(slot, text, size);
  }
  return slot;
}

// SQL returning the prize car Ids (one column, Id) for event `ev`:
//  * normal event (2+ distinct participant cars): the participants whose
//    PerformanceIndex is in the top half;
//  * one-make event (1 participant car): one car of the same make, in the
//    car's class or one class higher (not the car itself, no unicorns or
//    undrivable cars), picked by `seed` so every query returns the same car;
//    if the make has none, the event's own car.
// Only installed and owned cars are returned.
std::string PrizeIdsSelect(uint32_t ev, uint32_t seed) {
  const std::string e = std::to_string(ev);
  const std::string count = "(SELECT COUNT(DISTINCT Ordinal) FROM EventParticipants WHERE EventID=" + e + ")";
  const std::string event_car = "(SELECT MIN(Ordinal) FROM EventParticipants WHERE EventID=" + e + ")";
  const std::string same_make =
      "o.MakeID=ev.MakeID AND o.ClassID IN (ev.ClassID, ev.ClassID+1) AND o.Id<>ev.Id AND o.IsUnicorn=0"
      " AND o.IsDrivable=1 AND IsCarInstalledAndPurchased(o.Id)";
  return
      // normal event: top half of the participants
      "SELECT pc.Id AS Id FROM Data_Car AS pc WHERE " + count + ">1 AND pc.Id IN (SELECT Ordinal FROM"
      " EventParticipants WHERE EventID=" + e + ") AND 2*(SELECT COUNT(DISTINCT p.Ordinal) FROM"
      " EventParticipants AS p INNER JOIN Data_Car AS d ON d.Id=p.Ordinal WHERE p.EventID=" + e +
      " AND d.PerformanceIndex>pc.PerformanceIndex)<" + count + " AND IsCarInstalledAndPurchased(pc.Id)"
      // one-make event: one same-make car, same class or one higher
      " UNION ALL SELECT Id FROM (SELECT o.Id AS Id FROM Data_Car AS o, Data_Car AS ev WHERE ev.Id=" +
      event_car + " AND " + count + "=1 AND " + same_make + " ORDER BY (o.Id*" + std::to_string(seed) +
      ")%100003 LIMIT 1)"
      // one-make event whose make has no such car: the event's own car
      " UNION ALL SELECT ev.Id AS Id FROM Data_Car AS ev WHERE ev.Id=" + event_car + " AND " + count +
      "=1 AND IsCarInstalledAndPurchased(ev.Id) AND NOT EXISTS (SELECT 1 FROM Data_Car AS o WHERE " +
      same_make + ")";
}

void PrizeEventCompleted(uint32_t event_id) {
  if (!PrizeCarsEnabled()) return;
  std::lock_guard<std::recursive_mutex> lock(g_prize_mutex);
  PrizeLoad();
  if (!g_prize_pending.count(event_id)) {
    static std::mt19937 rng{std::random_device{}()};
    g_prize_pending[event_id] = std::uniform_int_distribution<uint32_t>(1, 100002)(rng);
    PrizeSave();
    REXLOG_INFO("Prize cars: event {} completed, prize pending", event_id);
  }
}

bool PrizeHasPending() {
  if (!PrizeCarsEnabled()) return false;
  std::lock_guard<std::recursive_mutex> lock(g_prize_mutex);
  PrizeLoad();
  return !g_prize_pending.empty();
}
}  // namespace

// 0x8248CA30 (cmpw cr6,r25,r3): existing event row. r3 = stored place,
// r25 = new place (17 = not every race has a result), [r30+8] = EventId.
void PrizeCarEventPlaceUpdate(PPCRegister& r3, PPCRegister& r25, PPCRegister& r30) {
  if (r3.s32 > 16 && r25.s32 >= 1 && r25.s32 <= 16) PrizeEventCompleted(LoadBE32(r30.u32 + 8));
}

// 0x8248CAB0: first result row for the event (single-race events finish here).
void PrizeCarEventPlaceInsert(PPCRegister& r25, PPCRegister& r30) {
  if (r25.s32 >= 1 && r25.s32 <= 16) PrizeEventCompleted(LoadBE32(r30.u32 + 8));
}

// 0x8249D460 / 0x8258C95C: "if (Level > LastRewardedLevel) build reward queue".
// Returning true jumps into the build even without a level-up.
bool PrizeCarQueueGuardA(PPCRegister& r31) { return PrizeHasPending(); }
bool PrizeCarQueueGuardB(PPCRegister& r30) { return PrizeHasPending(); }

// 0x82449610 in GetRewardCars: r5 = lastRewardedLevel, r6 = level,
// r12 = return address. Only the queue builder (returns to 0x8245C4C4) gets
// the prize rows.
void PrizeCarCaptureRange(PPCRegister& r5, PPCRegister& r6, PPCRegister& r12) {
  g_prize_range_ok = PrizeCarsEnabled() && r12.u32 == 0x8245C4C4;
  g_prize_range_lo = r5.u32;
  g_prize_range_hi = r6.u32;
}

// 0x82449630: r5 = the formatted query (a 384-byte stack buffer). Point it at
// our own query: the stock rows plus each pending event's pool.
void PrizeCarSwapQuery(PPCRegister& r5) {
  if (!g_prize_range_ok) return;
  g_prize_range_ok = false;
  std::lock_guard<std::recursive_mutex> lock(g_prize_mutex);
  PrizeLoad();
  if (g_prize_pending.empty()) return;
  if (!g_prize_queue_buf) g_prize_queue_buf = GuestAlloc(kQueueQueryBufferSize);
  if (!g_prize_queue_buf) return;
  std::string q =
      "SELECT RewardCarId, Thumbnail, DriverLevel FROM (SELECT RewardCarId, DriverLevel, SortOrder"
      " FROM Rewards_DriverLevelV3 WHERE DriverLevel>" + std::to_string(g_prize_range_lo) +
      " AND DriverLevel<=" + std::to_string(g_prize_range_hi);
  size_t n = 0;
  for (const auto& [ev, seed] : g_prize_pending) {
    if (n++ == kPrizeMaxEventsPerQuery) break;
    // Exactly one row per event: one car picked at random (by the event's
    // stored seed, so every rebuild of the queue picks the same car) from the
    // pool. One row -> CGiftCarReward, the game's single-car gift screen.
    // (Two or more rows would give CCarChoiceReward, the "choose a car"
    // popup; PrizeCarPopupQuery still serves that if it ever happens.)
    q += " UNION ALL SELECT P.Id AS RewardCarId, " + std::to_string(kPrizeLevelBase + ev) +
         " AS DriverLevel, 0 AS SortOrder FROM (SELECT Id FROM (" + PrizeIdsSelect(ev, seed) +
         ") ORDER BY (Id*" + std::to_string(seed) + ")%100003 LIMIT 1) AS P";
  }
  q += ") AS R INNER JOIN Data_Car ON (Data_Car.ID=R.RewardCarId)"
       " WHERE IsCarInstalledAndPurchased(R.RewardCarId) ORDER BY DriverLevel, SortOrder";
  if (q.size() >= kQueueQueryBufferSize) return;
  GuestWriteString(g_prize_queue_buf, q, kQueueQueryBufferSize);
  r5.u64 = g_prize_queue_buf;
  g_prize_building = true;
  g_prize_seen.clear();
}

// 0x8245C52C: queue builder read a row's DriverLevel (r29).
void PrizeCarSeenLevel(PPCRegister& r29) {
  if (g_prize_building && IsPrizeLevel(r29.u32)) g_prize_seen.insert(r29.u32);
}

// 0x8245C7EC: queue builder done. A pending event that produced no row has no
// prize available (no participant cars, or none installed/owned): drop it.
void PrizeCarQueueDone(PPCRegister& r3) {
  if (!g_prize_building) return;
  g_prize_building = false;
  std::lock_guard<std::recursive_mutex> lock(g_prize_mutex);
  bool changed = false;
  size_t n = 0;
  for (auto it = g_prize_pending.begin(); it != g_prize_pending.end();) {
    if (n++ >= kPrizeMaxEventsPerQuery) break;  // not part of this query
    if (!g_prize_seen.count(kPrizeLevelBase + it->first)) {
      REXLOG_INFO("Prize cars: event {} has no prize cars available, skipped", it->first);
      it = g_prize_pending.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (changed) PrizeSave();
}

// 0x8250D320 (popup): r5 = level for "SELECT MaxGiftCars FROM PlayerLevelDef
// WHERE ID = %u". Prize levels have no row; ask for row 0 instead.
void PrizeCarPopupMaxGift(PPCRegister& r5) {
  if (IsPrizeLevel(r5.u32)) r5.u64 = 0;
}

// 0x8250D3A8 (popup): r5 = level. The stock query is formatted into a
// 1024-byte stack buffer, too small for the prize query, so build the whole
// query in our own buffer here and hand it over at 0x8250D790.
bool g_prize_popup_pending = false;

void PrizeCarPopupQuery(PPCRegister& r5) {
  g_prize_popup_pending = false;
  if (!IsPrizeLevel(r5.u32)) return;
  std::lock_guard<std::recursive_mutex> lock(g_prize_mutex);
  if (!g_prize_popup_buf) g_prize_popup_buf = GuestAlloc(kPopupQueryBufferSize);
  if (!g_prize_popup_buf) return;
  const uint32_t ev = r5.u32 - kPrizeLevelBase;
  PrizeLoad();
  const auto found = g_prize_pending.find(ev);
  const uint32_t seed = found != g_prize_pending.end() ? found->second : 1;
  const std::string q =
      "SELECT Data_Car.*, Data_Car.BaseCost AS CarValue FROM Data_Car WHERE Data_Car.Id IN (" +
      PrizeIdsSelect(ev, seed) +
      ") AND IsCarInstalledAndPurchased(ModelId) ORDER BY Data_Car.PerformanceIndex DESC LIMIT " +
      std::to_string(kPrizePopupMaxCars);
  if (q.size() >= kPopupQueryBufferSize) return;
  GuestWriteString(g_prize_popup_buf, q, kPopupQueryBufferSize);
  g_prize_popup_pending = true;
}

// 0x8250D790 (popup): r4 = the formatted car query, about to be given to the
// popup's data source. Use the prize query instead.
void PrizeCarPopupSetQuery(PPCRegister& r4) {
  if (!g_prize_popup_pending) return;
  g_prize_popup_pending = false;
  r4.u64 = g_prize_popup_buf;
}

// 0x82449454 / 0x82449470: reward title = string "IDS_GiftCarTitle_<level>"
// from table "GiftCarTitles". For a prize level use the event's name
// ("IDS_Name_<EventId>" in table "Events").
void PrizeCarTitleFormat(PPCRegister& r4, PPCRegister& r5) {
  g_prize_title_swap = false;
  if (!IsPrizeLevel(r5.u32)) return;
  const uint32_t fmt = GuestConstString(g_prize_title_fmt, "IDS_Name_%d");
  if (!fmt || !GuestConstString(g_prize_title_table, "Events")) return;
  r4.u64 = fmt;
  r5.u64 = r5.u32 - kPrizeLevelBase;
  g_prize_title_swap = true;
}

void PrizeCarTitleTable(PPCRegister& r4) {
  if (!g_prize_title_swap) return;
  g_prize_title_swap = false;
  r4.u64 = g_prize_title_table;
}

// 0x82454440 / 0x824544E0: flushing finished rewards. r31 becomes the level
// written to Main/LastRewardedLevel. Skip prize levels (and mark that event's
// prize as given) so normal level-up cars keep working.
void PrizeCarFlushStart(PPCRegister& r31) { g_prize_flush_prev = r31.u32; }

void PrizeCarFlushItem(PPCRegister& r31) {
  if (!IsPrizeLevel(r31.u32)) {
    g_prize_flush_prev = r31.u32;
    return;
  }
  const uint32_t ev = r31.u32 - kPrizeLevelBase;
  r31.u64 = g_prize_flush_prev;
  std::lock_guard<std::recursive_mutex> lock(g_prize_mutex);
  PrizeLoad();
  if (g_prize_pending.erase(ev)) {
    PrizeSave();
    REXLOG_INFO("Prize cars: prize for event {} given", ev);
  }
}

// 0x824494CC: reward card text. Levels above 50 are shown as a credits reward
// (level*2500 - 22500). r11 = the reward's level; report 0 for prize levels so
// no bogus credit amount is shown.
void PrizeCarCreditsText(PPCRegister& r11) {
  if (IsPrizeLevel(r11.u32)) r11.u64 = 0;
}

// ---------------------------------------------------------------------------
// Real AI driver pool (pack: tools/dlc/make_ai_drivers_pack.py). The career
// grid takes one AI per slot with "SELECT AIPlayers.Id, Name, Rank FROM
// AIPlayers INNER JOIN List_Country ... WHERE RegionID=%d AND Rank=%d"
// (string 0x82021508) and uses the first row, so only one driver per region
// and rank could ever race. The pack adds several drivers per region and rank;
// this picks one of them at random. Same columns, shorter than the stock
// string, so the sprintf stack buffer is not at risk. FM4_AI_DRIVER_POOL=0
// disables.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kAiRankQueryStock = 0x82021508;
uint32_t g_ai_rank_query = 0;
std::once_flag g_ai_rank_query_once;

bool AiDriverPoolEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("FM4_AI_DRIVER_POOL");
    return !(v && v[0] == '0');
  }();
  return enabled;
}
}  // namespace

// 0x8249136C: bl 0x824781A8 (sprintf into the stack buffer at r1+896),
// r4 = the format string.
void AiDriverPoolQuery(PPCRegister& r4) {
  if (!AiDriverPoolEnabled() || r4.u32 != kAiRankQueryStock) return;
  std::call_once(g_ai_rank_query_once, [] {
    GuestConstString(g_ai_rank_query,
                     "SELECT A.Id, Name, Rank FROM AIPlayers A JOIN List_Country C"
                     " ON A.CountryID=C.CountryID WHERE RegionID=%d AND Rank=%d"
                     " ORDER BY RANDOM()");
  });
  if (g_ai_rank_query) r4.u64 = g_ai_rank_query;
}

// ---------------------------------------------------------------------------
// Upgrade-dependent engine audio (claude/upgrade-audio-framework-2026-10-12.md).
// Every engine's _ET.xml has per-level sound slots, <EngineIntake><Upgrade
// L0..L3/> and <Exhaust><Upgrade L0..L3/>, and the audio loader picks the slot
// for the car's level. The stock game hard-codes that level to 3 when it builds
// the car audio descriptor (li r9,3 at 0x823D4784: desc+84 intake, desc+92
// exhaust), so the race slot is always used. These hooks give it the car's
// real intake/exhaust part levels and fall back to a usable slot when the
// chosen one is empty, "NA.xml" or malformed. FM4_UPGRADE_AUDIO=0 disables.
// ---------------------------------------------------------------------------
#include <cctype>
#include <string>
namespace {
bool UpgradeAudioEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("FM4_UPGRADE_AUDIO");
    return !(v && v[0] == '0');
  }();
  return enabled;
}

// std::string as laid out in this build: +0 inline buffer (or heap pointer
// once capacity >= 16), +16 size, +20 capacity. ET slots are 28 bytes apart.
std::string GuestStdString(uint32_t obj) {
  if (!obj) return {};
  const uint32_t size = LoadBE32(obj + 16);
  const uint32_t cap = LoadBE32(obj + 20);
  if (size == 0 || size > 260) return {};
  const uint32_t data = cap >= 16 ? LoadBE32(obj) : obj;
  if (!data) return {};
  auto* p = REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(data);
  return std::string(p, size);
}

bool UsableHtSlot(const std::string& s) {
  std::string low;
  for (char c : s) low += char(std::tolower(static_cast<unsigned char>(c)));
  if (low.size() < 8 || low == "na.xml") return false;
  return low.compare(low.size() - 7, 7, "_ht.xml") == 0;
}
}  // namespace

// 0x823D47A0 (lwz r9,13192(r30)), right after the hard-coded stores.
// r30 = the car's upgrade state: +13236 intake level, +13232 exhaust level
// (the same fields the stock code subtracts the ISStock level from).
// r31 = audio descriptor: +40 engine MediaName, +84 intake, +92 exhaust.
void UpgradeAudioLevels(PPCRegister& r30, PPCRegister& r31) {
  if (!UpgradeAudioEnabled()) return;
  const uint32_t intake = LoadBE32(r30.u32 + 13236);
  const uint32_t exhaust = LoadBE32(r30.u32 + 13232);
  const std::string engine = GuestStdString(r31.u32 + 40);
  if (intake > 3 || exhaust > 3) {
    REXLOG_WARN("Upgrade audio: {} has out-of-range levels intake={} exhaust={}, keeping L3", engine, intake, exhaust);
    return;
  }
  StoreBE32(r31.u32 + 84, intake);
  StoreBE32(r31.u32 + 92, exhaust);
  REXLOG_INFO("Upgrade audio: {} intake L{} exhaust L{}", engine, intake, exhaust);
}

// 0x82A8AD1C (lwz r11,14744(r30)) in the ET parser, just before it copies
// Intake[obj+14744] and Exhaust[obj+14748] into the slots the loader uses.
// r30 = car audio object: intake slots at +17400, exhaust slots at +17512.
void UpgradeAudioSlotGuard(PPCRegister& r30) {
  if (!UpgradeAudioEnabled()) return;
  struct Group { const char* name; uint32_t level_off; uint32_t slots_off; };
  static const Group groups[] = {{"intake", 14744, 17400}, {"exhaust", 14748, 17512}};
  for (const Group& g : groups) {
    const uint32_t want = LoadBE32(r30.u32 + g.level_off);
    if (want > 3) continue;
    // The asked level first, then the higher levels (L3 is what the stock game
    // always used), then the lower ones.
    int order[4], n = 0;
    for (int l = int(want); l <= 3; ++l) order[n++] = l;
    for (int l = int(want) - 1; l >= 0; --l) order[n++] = l;
    for (int i = 0; i < n; ++i) {
      const std::string slot = GuestStdString(r30.u32 + g.slots_off + 28u * uint32_t(order[i]));
      if (!UsableHtSlot(slot)) continue;
      if (uint32_t(order[i]) != want) {
        StoreBE32(r30.u32 + g.level_off, uint32_t(order[i]));
        REXLOG_INFO("Upgrade audio: {} L{} slot unusable, using L{} ({})", g.name, want, order[i], slot);
      } else {
        REXLOG_INFO("Upgrade audio: {} L{} -> {}", g.name, want, slot);
      }
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Bigger grids (claude/car-limit-on-track-research-2026-10-12.md).
// Opt-in: set FM4_MAX_CARS=<17..48> (e.g. 32; "Launch FM4 (32 cars).bat").
// Stock limits: the options object built by sub_823EA4A0 defaults maxcars=16
// (+1308), maxaicars=11 (+1312), maxcarstoloadunified=16 (+1316) and the
// Free Play AI count NumQuickRaceAICars=11 (+1716, capped to maxcars-1 in
// sub_8248DF10); sub_825959D0 clamps maxaicars to 15; the AI driver query asks
// for Rank = slot+1 and only ranks 1..16 exist. This raises all of them.
// The grid needs matching StartGridPositions rows (tools/dlc/make_grid32_pack.py);
// extra cars beyond the rows wrap onto used slots. Not verified: per-car arrays
// in physics/audio/render/replay. FM4_MAX_CARS unset = stock behaviour.
// ---------------------------------------------------------------------------
namespace {
int MaxCarsOverride() {
  static const int n = [] {
    const char* v = std::getenv("FM4_MAX_CARS");
    if (!v || !v[0]) return 0;
    int x = std::atoi(v);
    if (x <= 16) return 0;
    if (x > 48) x = 48;
    return x;
  }();
  return n;
}
// FM4_MAX_CARS_SKIP=<comma list> turns single parts off for bisecting a crash:
// maxcars, maxaicars, unified, quick (Free Play AI count), clamp, rank.
bool MaxCarsSkip(const char* part) {
  static const std::string skip = [] {
    const char* v = std::getenv("FM4_MAX_CARS_SKIP");
    return std::string(v ? v : "");
  }();
  return !skip.empty() && skip.find(part) != std::string::npos;
}
}  // namespace

// ---- runtime fix switches (no rebuild, no relaunch for most): fm4_cars.cfg in the working directory,
// or the file named by FM4_CARS_CFG. Re-read at most every 500 ms when its timestamp changes.
//   off=queue,box1,box2,peer,listcap,heappad,dtorskip,probes     (fixes to disable; default all on)
//   heappad=N        allocation tail room = size >> N (0 = +100%, 1 = +50%, 2 = +25%)
// queue/box1/box2 relocate tables on first use, so flip those before launching the game.
#include <chrono>
#include <filesystem>
#include <atomic>
namespace {
const char* const kFixNames[] = {"queue", "box1", "box2", "peer", "listcap", "heappad", "dtorskip", "probes", "lookupwait"};
std::atomic<uint32_t> g_off_mask{0};
std::atomic<int> g_pad_shift{0};
std::atomic<long long> g_next_check_ms{0};
std::mutex g_cfg_mu;
std::string g_cfg_path;
std::filesystem::file_time_type g_cfg_mtime{};
bool g_cfg_seen = false;
long long NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void CfgReload() {
  std::lock_guard<std::mutex> lock(g_cfg_mu);
  if (g_cfg_path.empty()) { const char* e = std::getenv("FM4_CARS_CFG"); g_cfg_path = (e && e[0]) ? e : "fm4_cars.cfg"; }
  std::error_code ec;
  auto mt = std::filesystem::last_write_time(g_cfg_path, ec);
  if (ec) { if (g_cfg_seen) { g_cfg_seen = false; g_off_mask = 0; g_pad_shift = 0; REXLOG_INFO("[maxcars] config {} gone, defaults", g_cfg_path); } return; }
  if (g_cfg_seen && mt == g_cfg_mtime) return;
  g_cfg_seen = true; g_cfg_mtime = mt;
  uint32_t mask = 0; int shift = 0;
  std::ifstream in(g_cfg_path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), val = line.substr(eq + 1);
    while (!val.empty() && (val.back() == '\r' || val.back() == ' ')) val.pop_back();
    if (key == "off") {
      for (size_t i = 0; i < sizeof(kFixNames) / sizeof(kFixNames[0]); ++i)
        if (val.find(kFixNames[i]) != std::string::npos) mask |= 1u << i;
    } else if (key == "heappad") {
      shift = std::atoi(val.c_str());
    }
  }
  g_off_mask = mask; g_pad_shift = shift;
  REXLOG_INFO("[maxcars] config {} loaded: off-mask={:#x} heappad-shift={}", g_cfg_path, mask, shift);
}
bool FixOn(const char* name) {
  if (!MaxCarsOverride() || MaxCarsSkip(name)) return false;
  const long long now = NowMs();
  if (now >= g_next_check_ms.load(std::memory_order_relaxed)) { g_next_check_ms = now + 500; CfgReload(); }
  const uint32_t mask = g_off_mask.load(std::memory_order_relaxed);
  if (!mask) return true;
  for (size_t i = 0; i < sizeof(kFixNames) / sizeof(kFixNames[0]); ++i)
    if (std::strcmp(name, kFixNames[i]) == 0) return !(mask & (1u << i));
  return true;
}
}  // namespace

// 0x823EA5E0 (after "stw r30,1724(r31)", end of the options-object constructor).
void MaxCarsOptions(PPCRegister& r31) {
  const int n = MaxCarsOverride();
  if (!n || !r31.u32) return;
  if (!MaxCarsSkip("maxcars")) StoreBE32(r31.u32 + 1308, uint32_t(n));      // maxcars
  if (!MaxCarsSkip("maxaicars")) StoreBE32(r31.u32 + 1312, uint32_t(n - 1));  // maxaicars
  if (!MaxCarsSkip("unified")) StoreBE32(r31.u32 + 1316, uint32_t(n));      // maxcarstoloadunified
  if (!MaxCarsSkip("quick")) StoreBE32(r31.u32 + 1716, uint32_t(n - 1));  // NumQuickRaceAICars (Free Play)
  REXLOG_INFO("Max cars override: {} cars ({} AI), skipping: {}", n, n - 1,
              std::getenv("FM4_MAX_CARS_SKIP") ? std::getenv("FM4_MAX_CARS_SKIP") : "-");
}

// 0x825972F4 (after "li r10,15"): the maxaicars clamp. r11 = maxaicars; the
// next compare keeps r11 below 15 and otherwise uses r10, so give it r11.
void MaxCarsAiClamp(PPCRegister& r10, PPCRegister& r11) {
  if (!MaxCarsOverride() || MaxCarsSkip("clamp")) return;
  if (int32_t(r11.u32) > 15) r10.u64 = r11.u32;
}

// 0x82491364 (after "or r6,r28,r28"): Rank argument of the AI driver query.
// Slots past 16 reuse ranks 1..16 (the DB has no rank 17+).
void MaxCarsAiRank(PPCRegister& r6, PPCRegister& r28) {
  if (!MaxCarsOverride() || MaxCarsSkip("rank")) return;
  r6.u64 = uint32_t(((int32_t(r28.u32) - 1) & 15) + 1);
}

// ---------------------------------------------------------------------------
// Probes for the >16-car crashes (only while FM4_MAX_CARS is set). They only
// read guest memory and log; see claude/lemans-32-car-grid-2026-10-12.md.
// ---------------------------------------------------------------------------
// Entry+1 of sub_8284E8B8 (race-setup serializer; r3 = object+4, r12 = caller).
// Logs the 7 pointer slots at r3+52..+76, then +80/+84 and the object's vtable.
void MaxCarsProbeSerialize(PPCRegister& r3, PPCRegister& r12) {
  if (!MaxCarsOverride() || !r3.u32) return;
  const uint32_t t = r3.u32;
  REXLOG_INFO("[carprobe] serialize this={:08X} lr={:08X} vt={:08X} slots {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} | +80={:08X} +84={:08X} +88={:08X} +96={:08X}",
              t, r12.u32, LoadBE32(t - 4), LoadBE32(t + 52), LoadBE32(t + 56), LoadBE32(t + 60), LoadBE32(t + 64),
              LoadBE32(t + 68), LoadBE32(t + 72), LoadBE32(t + 76), LoadBE32(t + 80), LoadBE32(t + 84),
              LoadBE32(t + 88), LoadBE32(t + 96));
}

// Entry of sub_826B1168 (game-flow state transition): r3 = this, r4 = new state.
void MaxCarsProbeFlow(PPCRegister& r3, PPCRegister& r4) {
  if (!MaxCarsOverride()) return;
  REXLOG_INFO("[carprobe] flow this={:08X} state-arg={} singleton={:08X} cur-state={}",
              r3.u32, int32_t(r4.u32), LoadBE32(0x82DC383C),
              r3.u32 ? LoadBE32(r3.u32 + 40) : 0xFFFFFFFFu);
}

// ---------------------------------------------------------------------------
// Attempt 1 at surviving the 17th car: the crashes are calls through garbage
// pointers in the 7-slot release/serialize loops of the class handled by
// sub_8284E708 (release) and sub_8284E8B8 (serialize). These guards skip a slot
// whose object/vtable/method pointer is not plausible instead of calling it
// (a skipped release leaks; a skipped serialize drops that slot). They hide the
// symptom only: whatever overwrote the pointer is still there.
// FM4_MAX_CARS_SKIP=guard turns them off.
// ---------------------------------------------------------------------------
namespace {
bool PlausibleHeapPtr(uint32_t p) { return p >= 0x10000000u && p < 0x80000000u && (p & 3u) == 0; }
bool PlausibleCode(uint32_t a) { return a >= 0x82010000u && a < 0x82B00000u && (a & 3u) == 0; }
// obj -> vtable -> slot function; true when the call chain looks sane.
bool PlausibleVirtual(uint32_t obj, uint32_t vtable_slot_offset) {
  if (!PlausibleHeapPtr(obj)) return false;
  const uint32_t vt = LoadBE32(obj);
  if (vt < 0x82000000u || vt >= 0x82F00000u || (vt & 3u)) return false;
  return PlausibleCode(LoadBE32(vt + vtable_slot_offset));
}
int g_guard_hits = 0;
bool GuardActive() { return MaxCarsOverride() && !MaxCarsSkip("guard"); }
void GuardLog(const char* where, uint32_t slot_obj) {
  if (++g_guard_hits <= 40)
    REXLOG_WARN("[carprobe] guard skipped bad slot at {} obj={:08X} (hit {})", where, slot_obj, g_guard_hits);
}
}  // namespace

// 0x8284E774 / 0x8284E7A0: destructor slot release ("lwz r11,0(r3); lwz r11,8(r11); bctrl").
bool MaxCarsGuardRelease1(PPCRegister& r3) {
  if (!GuardActive() || PlausibleVirtual(r3.u32, 8)) return false;
  GuardLog("release1", r3.u32); return true;
}
bool MaxCarsGuardRelease2(PPCRegister& r3) {
  if (!GuardActive() || PlausibleVirtual(r3.u32, 8)) return false;
  GuardLog("release2", r3.u32); return true;
}

// 0x8284F008 (save) / 0x8284EB4C (load): "lwz r11,4(r11); lwz r11,0(r11); bctrl", r11 = slot object.
bool MaxCarsGuardSerSave(PPCRegister& r11) {
  if (!GuardActive()) return false;
  if (PlausibleHeapPtr(r11.u32) && PlausibleVirtual(LoadBE32(r11.u32 + 4), 0)) return false;
  GuardLog("ser-save", r11.u32); return true;
}
bool MaxCarsGuardSerLoad(PPCRegister& r11) {
  if (!GuardActive()) return false;
  if (PlausibleHeapPtr(r11.u32) && PlausibleVirtual(LoadBE32(r11.u32 + 4), 0)) return false;
  GuardLog("ser-load", r11.u32); return true;
}

// ---------------------------------------------------------------------------
// Attempt 2: relocate the 16-entry global task queue (array 0x82E115B0, count 0x82E115F0).
// Entry index = owner+28 of the object registered at 0x82AA46xx, so car #17 wrote past the array
// into the count word and the worker (0x82AA3E40) then called through the count as a pointer.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kQueueEntries = 64;
uint32_t QueueBase() {
  static const uint32_t base = [] {
    uint32_t p = REX_KERNEL_MEMORY()->SystemHeapAlloc(kQueueEntries * 4 + 16);
    if (p) for (uint32_t i = 0; i < kQueueEntries; ++i) StoreBE32(p + i * 4, 0);
    REXLOG_INFO("[maxcars] relocated task queue to {:08X}", p);
    return p;
  }();
  return base;
}
void QueueRetarget(PPCRegister& r) {
  if (!FixOn("queue")) return;
  const uint32_t b = QueueBase();
  if (b) r.u64 = b;
}
}  // namespace
void MaxCarsQueueBaseClear(PPCRegister& r9) {
  if (!FixOn("queue")) return;
  const uint32_t b = QueueBase();
  if (!b) return;
  for (uint32_t i = 0; i < kQueueEntries; ++i) StoreBE32(b + i * 4, 0);
  r9.u64 = b;
}
void MaxCarsQueueBaseWorker(PPCRegister& r28) { QueueRetarget(r28); }
void MaxCarsQueueBaseAdd(PPCRegister& r7) { QueueRetarget(r7); }
void MaxCarsQueueBaseDtor(PPCRegister& r30) { QueueRetarget(r30); }

// ---------------------------------------------------------------------------
// Attempt 3 probe: dump the node that holds a bad slot in the sub_8284E708 destructor loop.
// ---------------------------------------------------------------------------
namespace {
int g_dtor_dumps = 0;
void DumpWords(const char* tag, uint32_t addr, uint32_t nwords) {
  for (uint32_t i = 0; i < nwords; i += 8) {
    std::string line;
    for (uint32_t j = 0; j < 8 && i + j < nwords; ++j) { char b[16]; std::snprintf(b, sizeof b, " %08X", LoadBE32(addr + (i + j) * 4)); line += b; }
    REXLOG_WARN("[dtorprobe] {} {:08X}:{}", tag, addr + i * 4, line);
  }
}
bool DtorBad(uint32_t this_, uint32_t slotobj, uint32_t idx, const char* where) {
  if (!FixOn("dtorskip") || PlausibleVirtual(slotobj, 8)) return false;
  if (FixOn("probes") && ++g_dtor_dumps <= 6) {
    const uint32_t vt = PlausibleHeapPtr(slotobj) ? LoadBE32(slotobj) : 0;
    REXLOG_WARN("[dtorprobe] {} bad slot: this={:08X} idx={} slot={:08X} vt={:08X}", where, this_, idx, slotobj, vt);
    DumpWords("before-node", this_ - 0x80, 32);
    DumpWords("node", this_, 68);
    DumpWords("after-node", this_ + 0x110, 16);
  }
  return true;
}
}  // namespace
bool MaxCarsDtorProbe1(PPCRegister& r3, PPCRegister& r29, PPCRegister& r31) { return DtorBad(r31.u32, r3.u32, r29.u32, "loop1"); }
bool MaxCarsDtorProbe2(PPCRegister& r3, PPCRegister& r29, PPCRegister& r31) { return DtorBad(r31.u32, r3.u32, r29.u32, "loop2"); }

// ---------------------------------------------------------------------------
// Attempt 3b: the lazily built cache of boxed small integers (sub_82377330: 17 objects at
// 0x82DDDB48, init flag directly after it at 0x82DDDB8C). Index 17 (a 17th car) read the init flag
// (value 1) as an object pointer. Relocate to a 256-entry array and build all of it.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kBoxEntries = 256;
uint32_t BoxBase() {
  static const uint32_t base = [] {
    uint32_t p = REX_KERNEL_MEMORY()->SystemHeapAlloc(kBoxEntries * 4 + 16);
    if (p) for (uint32_t i = 0; i < kBoxEntries + 4; ++i) StoreBE32(p + i * 4, 0);
    REXLOG_INFO("[maxcars] relocated boxed-int cache to {:08X}", p);
    return p;
  }();
  return base;
}
}  // namespace
void MaxCarsBoxBase(PPCRegister& r31) {
  if (!FixOn("box1")) return;
  const uint32_t b = BoxBase();
  if (b) r31.u64 = b;
}
bool MaxCarsBoxLoop(PPCRegister& r30) {
  return FixOn("box1") && r30.u32 >= 17 && r30.u32 < kBoxEntries;
}

// Attempt 3c probe: validate the per-slot pointer array (+664, 16 entries) before sub_822C5918 uses it.
namespace { int g_clear_dumps = 0; }
void MaxCarsClearProbe(PPCRegister& r3) {
  if (!FixOn("probes") || g_clear_dumps >= 3) return;
  const uint32_t self = r3.u32;
  if (!PlausibleHeapPtr(self)) return;
  const uint32_t owner = LoadBE32(self + 32);
  const uint32_t idx = PlausibleHeapPtr(owner) ? LoadBE32(owner + 28) : 0xFFFFFFFFu;
  bool bad = false;
  for (uint32_t i = 0; i < 16; ++i) {
    const uint32_t p = LoadBE32(self + 664 + 4 * i);
    if (!p) continue;
    const uint32_t eff = p + ((idx << 5) & ~31u);
    if (!PlausibleHeapPtr(eff + 180 - (180 & 3u)) || !PlausibleHeapPtr(p)) {
      if (!bad) REXLOG_WARN("[clearprobe] BAD this={:08X} owner={:08X} owner[28]={:08X}", self, owner, idx);
      bad = true;
      REXLOG_WARN("[clearprobe]   ptr[{}]={:08X} (+664+4*{}) eff={:08X}", i, p, i, eff);
    }
  }
  if (bad) { ++g_clear_dumps; DumpWords("obj+0", self, 8); DumpWords("obj+640", self + 640, 40); DumpWords("owner", owner, 16); }
}

// ---------------------------------------------------------------------------
// Attempt 3d: per-peer table inside a big per-car object: Entry[16] x 32 bytes at +152, then
// ptr[16] at +664, both indexed by the other car's index (owner+28). Index 16 (the 17th car)
// overlaps ptr[0..7]. Skip registering / clearing entries for index >= 16 instead of corrupting.
// ---------------------------------------------------------------------------
bool MaxCarsPeerRegSkip(PPCRegister& r11) { return FixOn("peer") && r11.u32 >= 16 && r11.u32 < 4096; }
bool MaxCarsPeerClearSkip(PPCRegister& r10) { return FixOn("peer") && r10.u32 >= 512; }

// Probe for the early "this = -1" crash with >= 24 cars: who calls sub_826B1168 / sub_826B0058 with r3 = -1.
namespace {
int g_flow_logs = 0;
void FlowLog(const char* which, uint32_t r1, uint32_t r3, uint32_t lr) {
  if (!FixOn("probes")) return;
  const bool bad = (r3 == 0xFFFFFFFFu);
  if (!bad && g_flow_logs >= 6) return;
  if (g_flow_logs >= 40) return;
  ++g_flow_logs;
  std::string chain;
  for (uint32_t i = 0; i < 160; ++i) {
    const uint32_t v = LoadBE32(r1 + 4 * i);
    if (v >= 0x82010000u && v < 0x82B00000u && (v & 3u) == 0) { char b[16]; std::snprintf(b, sizeof b, " %08X", v); chain += b; }
  }
  REXLOG_WARN("[flowprobe] {} this={:08X} lr={:08X}{} stack-code-ptrs:{}", which, r3, lr, bad ? " <<< BAD (-1)" : "", chain);
}
}  // namespace
void MaxCarsFlowProbeA(PPCRegister& r1, PPCRegister& r3, PPCRegister& r12) { FlowLog("sub_826B1168", r1.u32, r3.u32, r12.u32); }
void MaxCarsFlowProbeB(PPCRegister& r1, PPCRegister& r3, PPCRegister& r12) { FlowLog("sub_826B0058", r1.u32, r3.u32, r12.u32); }

// Attempt 3e: second lazy 16-entry cache (sub_824C4BC0, base 0x82DDDB00, init flag at 0x82DDDB40,
// 1-based index). Index 17 read the init flag as an object pointer (crash when quitting a 17-car race).
namespace {
constexpr uint32_t kBox2Entries = 64;
uint32_t Box2Base() {
  static const uint32_t base = [] {
    uint32_t p = REX_KERNEL_MEMORY()->SystemHeapAlloc(kBox2Entries * 4 + 16);
    if (p) for (uint32_t i = 0; i < kBox2Entries + 4; ++i) StoreBE32(p + i * 4, 0);
    REXLOG_INFO("[maxcars] relocated indexed cache 2 to {:08X}", p);
    return p;
  }();
  return base;
}
}  // namespace
void MaxCarsBox2Base(PPCRegister& r29) {
  if (!FixOn("box2")) return;
  const uint32_t b = Box2Base();
  if (b) r29.u64 = b;
}
bool MaxCarsBox2Loop(PPCRegister& r31) {
  return FixOn("box2") && r31.u32 >= 16 && r31.u32 < kBox2Entries;
}

// ---------------------------------------------------------------------------
// Dynamic AI (claude/dynamic-ai-2026-10-13.md).
//
// 1. Race-day form. Every AI car's skill and temperament are written through
//    two tiny setters: sub_82436A30 (stw r4,696(r3): AISkills id, 1 best ..
//    20 worst) and sub_82436A38 (stw r4,700(r3): AITemperaments id, 1 most
//    aggressive .. 10 calmest). The callers have already added the driver's
//    SkillModifier/AggroModifier and clamped to 0..20 / 0..10. Here each car
//    gets a random "form" for the race (good day / bad day) and a random mood,
//    and the result is clamped to the valid rows 1..20 / 1..10. Special ids
//    (21, 22, 100+, 150) are left alone. A car keeps the same form for the
//    whole race: rolls are cached per settings object and the cache is reset
//    when no setter has been called for 45 s (i.e. at the next race load).
//    FM4_AI_FORM=0 disables.
//
// 2. Random AI upgrade levels. sub_823C4BE8 "fakes" AI upgrades by scaling
//    torque/mass/grip until the car reaches the target PI passed in f1 (the
//    class/event limit), and does nothing if the car is already above it.
//    The call at 0x823CB870 gets a lowered target per car, so some cars run
//    fully upgraded, some partly and some stock. The target is only ever
//    lowered, so the event's PI limit can't be exceeded.
//    FM4_AI_RANDOM_UPGRADES=0 disables.
// ---------------------------------------------------------------------------
#include <chrono>
#include <unordered_map>
namespace {
bool EnvOn(const char* name) {
  const char* v = std::getenv(name);
  return !(v && v[0] == '0');
}
bool AiFormEnabled() { static const bool on = EnvOn("FM4_AI_FORM"); return on; }
bool AiRandomUpgradesEnabled() { static const bool on = EnvOn("FM4_AI_RANDOM_UPGRADES"); return on; }

std::mt19937& AiRng() {
  static std::mt19937 rng(static_cast<uint32_t>(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  return rng;
}

struct AiFormRoll { int skill; int temper; };
std::mutex g_ai_form_mutex;
std::unordered_map<uint32_t, AiFormRoll> g_ai_form;
std::chrono::steady_clock::time_point g_ai_form_last{};

// Skill form: lower id = faster. Mostly small swings, sometimes a great or
// awful day.
int RollSkillForm(std::mt19937& rng) {
  const int r = static_cast<int>(rng() % 100);
  if (r < 6) return -3;    // flying
  if (r < 18) return -2;
  if (r < 36) return -1;
  if (r < 64) return 0;
  if (r < 82) return 1;
  if (r < 92) return 2;
  return 4;                // off day
}
int RollTemperForm(std::mt19937& rng) {
  const int r = static_cast<int>(rng() % 100);
  if (r < 15) return -2;   // red mist: more aggressive
  if (r < 35) return -1;
  if (r < 70) return 0;
  if (r < 88) return 1;
  return 2;                // cautious
}

AiFormRoll& AiFormFor(uint32_t obj) {
  const auto now = std::chrono::steady_clock::now();
  if (now - g_ai_form_last > std::chrono::seconds(45)) g_ai_form.clear();
  g_ai_form_last = now;
  auto it = g_ai_form.find(obj);
  if (it == g_ai_form.end()) {
    auto& rng = AiRng();
    AiFormRoll roll{RollSkillForm(rng), RollTemperForm(rng)};
    it = g_ai_form.emplace(obj, roll).first;
  }
  return it->second;
}
}  // namespace

// sub_82436A30 entry: r3 = driver settings, r4 = AISkills id.
void AiFormSkill(PPCRegister& r3, PPCRegister& r4) {
  if (!AiFormEnabled()) return;
  const int base = static_cast<int32_t>(r4.u32);
  if (base > 20) return;  // special rows
  std::lock_guard<std::mutex> lock(g_ai_form_mutex);
  const int v = std::clamp(std::max(base, 1) + AiFormFor(r3.u32).skill, 1, 20);
  REXLOG_INFO("Dynamic AI: settings {:08X} skill {} -> {}", r3.u32, base, v);
  r4.u64 = static_cast<uint64_t>(v);
}

// sub_82436A38 entry: r3 = driver settings, r4 = AITemperaments id.
void AiFormTemper(PPCRegister& r3, PPCRegister& r4) {
  if (!AiFormEnabled()) return;
  const int base = static_cast<int32_t>(r4.u32);
  if (base > 10) return;
  std::lock_guard<std::mutex> lock(g_ai_form_mutex);
  const int v = std::clamp(std::max(base, 1) + AiFormFor(r3.u32).temper, 1, 10);
  r4.u64 = static_cast<uint64_t>(v);
}

// 0x823CB870: bl 0x823C4BE8, f1 = target PI for this AI car.
void AiRandomUpgradeTarget(PPCRegister& f1) {
  if (!AiRandomUpgradesEnabled()) return;
  const double target = f1.f64;
  if (!(target > 1.0) || target > 2000.0) return;
  auto& rng = AiRng();
  const int r = static_cast<int>(rng() % 100);
  double drop;
  if (r < 35) drop = 0.0;                                       // maxed out
  else if (r < 75) drop = 5.0 + static_cast<double>(rng() % 36);   // 5..40 PI down
  else if (r < 92) drop = 40.0 + static_cast<double>(rng() % 61);  // 40..100 PI down
  else drop = 1000.0;                                           // stock
  const double v = std::max(1.0, target - drop);
  REXLOG_INFO("Dynamic AI: upgrade target PI {:.0f} -> {:.0f}", target, v);
  f1.f64 = v;
}

// Probe: where does r31 become -1 inside sub_826B0058 (>= 24 cars)? Log r31 after each call in it,
// and the frame of sub_82699448 (the likely culprit) at its epilogue.
namespace { int g_r31_logs = 0; }
void MaxCarsR31Probe(PPCRegister& r31, PPCRegister& r1) {
  if (!FixOn("probes") || g_r31_logs >= 60) return;
  ++g_r31_logs;
  REXLOG_WARN("[r31probe] tag? r31={:08X} r1={:08X}", r31.u32, r1.u32);
}
void MaxCarsR31ProbeA(PPCRegister& r31, PPCRegister& r1) { if (FixOn("probes") && g_r31_logs < 60) { ++g_r31_logs; REXLOG_WARN("[r31probe] 826B00C4 r31={:08X} r1={:08X}", r31.u32, r1.u32); } }
void MaxCarsR31ProbeB(PPCRegister& r31, PPCRegister& r1) { if (FixOn("probes") && g_r31_logs < 60) { ++g_r31_logs; REXLOG_WARN("[r31probe] 826B00D0 r31={:08X} r1={:08X}", r31.u32, r1.u32); } }
void MaxCarsR31ProbeC(PPCRegister& r31, PPCRegister& r1) { if (FixOn("probes") && g_r31_logs < 60) { ++g_r31_logs; REXLOG_WARN("[r31probe] 826B00D8 r31={:08X} r1={:08X}", r31.u32, r1.u32); } }
void MaxCarsR31ProbeD(PPCRegister& r1, PPCRegister& r28) {
  if (!FixOn("probes") || g_r31_logs >= 60) return;
  ++g_r31_logs;
  REXLOG_WARN("[r31probe] 826994E8 epilogue this={:08X} r1={:08X}", r28.u32, r1.u32);
  DumpWords("frame", r1.u32 + 80, 24);
}

// ---------------------------------------------------------------------------
// Attempt 3f: sub_826A5948 copies a list into a caller buffer with no bound. The caller
// sub_826A5F48 has room for only 16 8-byte entries (buf r1+84, count r1+212); with >= 24 cars the
// list is longer, overruns the frame and the saved r31 becomes -1 ("this = -1" crash).
// Cap at 16 entries, only for the caller whose count slot is exactly 128 bytes after its buffer.
// ---------------------------------------------------------------------------
static int g_listcap_logs = 0;
bool MaxCarsListCap(PPCRegister& r28, PPCRegister& r29, PPCRegister& r30) {
  if (!FixOn("listcap") || r30.u32 < 16) return false;
  const uint32_t buf0 = r29.u32 - 8u * r30.u32;
  if (r28.u32 - buf0 != 128u) return false;
  if (++g_listcap_logs <= 3) REXLOG_WARN("[listcap] capped list at 16 entries (buf={:08X})", buf0);
  return true;
}

// ---------------------------------------------------------------------------
// Attempt 3g: blunt heap-overrun absorber. sub_82853370 is the game's operator new (size in r3);
// with >16 cars some per-car arrays sized for 16 write past their block and clobber the next object
// (e.g. a node slot's first word). Give every small allocation tail room. FM4_MAX_CARS_SKIP=heappad
// turns it off; FM4_HEAP_PAD_SHIFT=n scales the pad (size >> n added; default 0 = +100%).
// ---------------------------------------------------------------------------
void MaxCarsHeapPad(PPCRegister& r3) {
  if (!FixOn("heappad")) return;
  const uint32_t size = r3.u32;
  if (size == 0 || size > 4096) return;
  int sh = g_pad_shift.load(std::memory_order_relaxed);
  if (sh < 0) sh = 0;
  if (sh > 8) sh = 8;
  uint32_t pad = size >> sh;
  if (pad < 64) pad = 64;
  if (pad > 1024) pad = 1024;
  r3.u64 = size + pad;
}

// Probe for the loader-thread crash after the heap padding: the id->object lookup (sub_822DE990) is
// handed an object whose std::map header (this+984) is null. Dump it once.
static int g_lookup_dumps = 0;
// Also waits (up to ~3 s) for a half-constructed object's map header to appear (cfg fix "lookupwait");
// if it never does, skip the lookup (returns null to the caller) instead of faulting.
bool MaxCarsLookupProbe(PPCRegister& r31, PPCRegister& r30) {
  const uint32_t self = r31.u32;
  if (!MaxCarsOverride() || !PlausibleHeapPtr(self) || LoadBE32(self + 984) != 0) return false;
  if (FixOn("probes") && g_lookup_dumps < 3) {
    ++g_lookup_dumps;
    REXLOG_WARN("[lookupprobe] this={:08X} key={:08X} map-head(+984)=0", self, r30.u32);
    DumpWords("this+0", self, 16);
  }
  if (!FixOn("lookupwait")) return false;
  static std::atomic<uint32_t> s_failed{0};
  if (s_failed.load() == self) return true;  // already timed out once: skip instantly
  const auto t0 = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(200)) {
    std::this_thread::yield();
    if (LoadBE32(self + 984) != 0) {
      REXLOG_WARN("[lookupwait] map header appeared (this={:08X})", self);
      return false;
    }
  }
  s_failed.store(self);
  REXLOG_WARN("[lookupwait] map header still null after 200 ms, skipping lookups for this={:08X}", self);
  return true;
}
