// ---------------------------------------------------------------------------
// Developer keys (Forza-4-Evolution testing aid).
//
//   F9        win the next race of the highlighted career event that has no
//             1st place yet (press repeatedly to step through the event)
//   Shift+F9  win every remaining race of that event (completes it)
//   F10       reset that event: delete its race and event results
//   F6        +100,000 credits (capped at the game's 999,999,999)
//   F8        driver level +1 (XP raised to that level's threshold; the
//             level-up reward car is offered the next time the career hub
//             checks for rewards)
//   F11       affinity level +1 for the make of the current career car
//
// F1-F12 already used by the runtime: F3 debug overlay, F4 settings,
// F5 checkpoint probe, F7 achievements.
//
// "Highlighted event" = the event last highlighted in the Event List (the
// tile the cursor is on, which is also the event you are in after pressing A).
// If nothing was highlighted this session, the event last opened is used.
//
// Results go through the game's own recorder, 0x8248C708 (the function the
// race-finish path uses), so everything that hooks event completion (the
// prize-car hooks at 0x8248CA30 / 0x8248CAB0) fires exactly as after a real
// race. No credits or XP are given. The Event List shows the new results the
// next time it is opened. The profile keeps them after the game's next save.
//
// Keys are read once per frame in the main game loop (after the XNotify pump,
// 0x822F787C), so all guest calls run on the main game thread between frames.
// The window must have focus. Every action is logged ("Dev keys: ...") and
// beeps: OK sound on success, error sound on failure.
// Set FM4_DEV_KEYS=0 to disable.
//
// Guest addresses used (default.xex, see claude/dev-keys-2026-10-12.md):
//   0x82DC3830  game object; [game+16] = database; [[db]+24] = create query
//               (r3 = &out, r4 = db, r5 = sql). Query: +8 Release,
//               +132 GetInt(key), +168 AtEnd, +172 Next.
//   0x82DC383C  profile manager; 0x822CA070(mgr) = active profile,
//               profile vfunc +72 -> 0x82405E38 builds the profile table
//               prefix string ("<prefix>Career_RaceResults").
//   0x82DC3828  [x]+16 = career selection; +776 = EventId last opened.
//   0x824F4344  Event List "selection changed" handler, right after it read
//               the highlighted row's Id: r3 = EventId.
//   0x823395D8 / 0x82334380  guest string construct / destroy.
// ---------------------------------------------------------------------------

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <Xinput.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "xinput.lib")
#else
constexpr int VK_SHIFT = 0x10, VK_F6 = 0x75, VK_F8 = 0x77, VK_F9 = 0x78, VK_F10 = 0x79, VK_F11 = 0x7A;
#endif

#include <rex/ppc.h>
#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/thread_state.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

// Live switches: F4 > Forza Motorsport 4/Evolution ("Save to config" keeps them in fm4.toml) and the Mods menu FEATURES
// tab (getters/setters at the end of this file). Read every time the code runs, no restart. FM4_EVO_HUD=0 turns the HUD off at startup.
REXCVAR_DEFINE_BOOL(fm4_evo_unlock, true, "Forza Motorsport 4/Evolution",
                    "Evolution: open the next decade's events after a decade's championship and four other events (off = columns stay as they are)");
REXCVAR_DEFINE_BOOL(fm4_evo_hud, true, "Forza Motorsport 4/Evolution",
                    "Evolution: top-left progress readout (XP, level, one line per decade)");

namespace {

constexpr uint32_t kGameGlobal = 0x82DC3830;
constexpr uint32_t kProfileMgrGlobal = 0x82DC383C;
constexpr uint32_t kCareerGlobal = 0x82DC3828;
constexpr uint32_t kFnGetProfile = 0x822CA070;
constexpr uint32_t kFnRecordRaceResult = 0x8248C708;
// Profile settings ("Main/..." keys). data = [profile+156].
constexpr uint32_t kFnSettingGet = 0x82385178;      // (variant_out, data, key) ; value at variant+8
constexpr uint32_t kFnVariantFromU32 = 0x82947D08;  // (variant_out, value)
constexpr uint32_t kFnSettingSet = 0x824058B0;      // (data, key, &variant) ; notifies listeners
constexpr uint32_t kFnVariantDestroy = 0x8234F198;  // (variant)
constexpr uint32_t kFnSetCredits = 0x82405FC0;      // (data, credits) ; the game's own setter
constexpr uint32_t kMaxCredits = 999999999;         // clamp used by 0x82405FC0

// Guest scratch block layout.
constexpr uint32_t kScratchSize = 0x2000;
constexpr uint32_t kOffOut = 0x000;     // query out pointer
constexpr uint32_t kOffKey = 0x020;     // column-name string object (32 bytes)
constexpr uint32_t kOffPrefix = 0x060;  // table-prefix string object (32 bytes)
constexpr uint32_t kOffResult = 0x0A0;  // race result record (48 bytes)
constexpr uint32_t kOffColId = 0x100;   // "Id"
constexpr uint32_t kOffVariant = 0x120; // setting variant (48 bytes)
constexpr uint32_t kOffSettingKey = 0x160;  // setting key C string (64 bytes)
constexpr uint32_t kOffSql = 0x200;     // SQL text
constexpr uint32_t kSqlCapacity = kScratchSize - kOffSql;

std::atomic<uint32_t> g_highlighted_event{0};
// UI widgets remembered when the game refreshes them itself, so a dev key
// can ask for the same refresh after changing a value (see RefreshUi).
uint32_t g_header = 0, g_header_vtable = 0;      // 0x82631D60 RefreshHeader(this)
uint32_t g_affinity = 0, g_affinity_vtable = 0;  // 0x826304C8 RefreshAffinity(this, data)
bool g_faulted = false;  // a guest call unwound; keys stay off for this session
uint32_t g_scratch = 0;
bool g_prev_keys[256] = {};

bool DevKeysEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("FM4_DEV_KEYS");
    return !(v && v[0] == '0');
  }();
  return enabled;
}

uint8_t* Base() { return REX_KERNEL_MEMORY()->virtual_membase(); }
uint32_t Load32(uint32_t a) {
  const uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(a);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
void Store32(uint32_t a, uint32_t v) {
  uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(a);
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
void Store8(uint32_t a, uint8_t v) { *REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(a) = v; }
void WriteCStr(uint32_t a, const std::string& s, uint32_t cap) {
  char* p = REX_KERNEL_MEMORY()->TranslateVirtual<char*>(a);
  const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
  std::memcpy(p, s.data(), n);
  p[n] = 0;
}
// MSVC std::string (guest): +0 inline buffer or pointer, +16 size, +20 capacity.
[[maybe_unused]] std::string ReadGuestString(uint32_t obj) {
  const uint32_t size = Load32(obj + 16);
  const uint32_t cap = Load32(obj + 20);
  if (size > 256) return {};
  const uint32_t data = cap >= 16 ? Load32(obj) : obj;
  const char* p = REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(data);
  return std::string(p, size);
}

// Builds a short guest std::string in place (inline buffer, no heap), the
// layout the game's own code uses: +0 buffer[16], +16 size, +20 capacity (15).
bool WriteShortGuestString(uint32_t obj, const std::string& text) {
  if (text.size() > 15) return false;
  uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(obj);
  std::memset(p, 0, 24);
  std::memcpy(p, text.data(), text.size());
  Store32(obj + 16, uint32_t(text.size()));
  Store32(obj + 20, 15);
  return true;
}

// Calls guest code on the current (main game) thread. The call runs on the
// thread's own PPCContext (kernel exports reach that context through the
// thread's PCR, so a separate context breaks them): the context is saved,
// the stack pointer moved well below the hooked frame, the function called,
// and the whole context restored afterwards.
class Guest {
 public:
  Guest(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr) {
    (void)r13; (void)fpscr;
    auto* ts = rex::runtime::ThreadState::Get();
    ctx_ = ts ? ts->context() : nullptr;
    if (!ctx_ || &ctx_->r1 != &r1) {
      REXLOG_WARN("Dev keys: hook is not running on the thread's own context; aborting");
      ctx_ = nullptr;
      ok_ = false;
      return;
    }
    sp_ = (r1.u32 - 0x400) & ~0xFu;
  }

  bool ok() const { return ok_; }

  // b is 64-bit for the profile flag functions, whose mask argument (r4) is a full 64-bit register.
  uint32_t Call(uint32_t target, uint32_t a = 0, uint64_t b = 0, uint32_t c = 0, uint32_t d = 0) {
    if (!ok_ || !target) { ok_ = false; return 0; }
    static_assert(sizeof(PPCContext) <= sizeof(saved_));
    std::memcpy(saved_, static_cast<void*>(ctx_), sizeof(PPCContext));
    PPCContext& ctx = *ctx_;
    ctx.r1.u64 = sp_;
    ctx.r3.u64 = a; ctx.r4.u64 = b; ctx.r5.u64 = c; ctx.r6.u64 = d;
    // LR 0 marks "return to the dev-key code", like XThread::Execute does.
    ctx.lr = 0;
    REXLOG_WARN("Dev keys: call {:08X}({:08X}, {:08X}, {:08X}, {:08X})", target, a, b, c, d);
    // This runtime returns through guest "reentries": when a function's blr
    // finds an LR different from the one it was entered with (normal for
    // functions that return through the __restgprlr helpers), it throws, and
    // the dispatcher resumes the guest function owning that LR. XThread's top
    // loop normally does that; here we must do it ourselves, or the reentry
    // escapes into the main loop on our temporary stack. Same protocol as
    // XThread::Execute: resume at the reentry address; after a resumed
    // fragment returns, continue at the guest LR; LR 0 = back to us.
    uint32_t next = target;
    bool nonlocal = false;
    bool failed = false;
    for (uint32_t steps = 0;; ++steps) {
      if (steps > 1000000) { failed = true; break; }
      PPCFunc* fn = rex::runtime::ResolveIndirectFunction(next);
      if (!fn) { failed = true; break; }
      try {
        ctx.dispatch_address = next;
        fn(ctx, Base());
        ctx.dispatch_address = 0;
        if (!nonlocal) break;
        next = uint32_t(ctx.lr);
      } catch (...) {
        ctx.dispatch_address = 0;
        next = uint32_t(ctx.lr);  // ReenterGuestFunction(ctx.lr) was the thrower
        nonlocal = true;
      }
      if (next == 0) break;
    }
    const uint32_t result = ctx.r3.u32;
    std::memcpy(static_cast<void*>(ctx_), saved_, sizeof(PPCContext));
    if (failed) {
      g_faulted = true;
      ok_ = false;
      REXLOG_WARN("Dev keys: call {:08X} could not finish (stuck at {:08X}); action aborted", target, next);
      return 0;
    }
    REXLOG_WARN("Dev keys:   {:08X} returned {:08X}", target, result);
    return result;
  }
  // Virtual call: obj->vtable[offset / 4](obj, a, b, c).
  uint32_t VCall(uint32_t obj, uint32_t offset, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) {
    if (!ok_ || !obj) { ok_ = false; return 0; }
    return Call(Load32(Load32(obj) + offset), obj, a, b, c);
  }

 private:
  PPCContext* ctx_ = nullptr;
  uint32_t sp_ = 0;
  bool ok_ = true;
  alignas(64) static inline unsigned char saved_[16384];
};

// Thin wrapper over the game's own database layer.
class Db {
 public:
  explicit Db(Guest& g) : g_(g) {}

  uint32_t Open(const std::string& sql) {
    if (sql.size() >= kSqlCapacity) { REXLOG_WARN("Dev keys: SQL too long"); return 0; }
    WriteCStr(g_scratch + kOffSql, sql, kSqlCapacity);
    const uint32_t game = Load32(kGameGlobal);
    if (!game) return 0;
    const uint32_t db = Load32(game + 16);
    if (!db) return 0;
    Store32(g_scratch + kOffOut, 0);
    g_.Call(Load32(Load32(db) + 24), g_scratch + kOffOut, db, g_scratch + kOffSql);
    return Load32(g_scratch + kOffOut);
  }
  void Close(uint32_t q) { if (q) g_.VCall(q, 8); }
  void Exec(const std::string& sql) { Close(Open(sql)); }
  bool AtEnd(uint32_t q) { return (g_.VCall(q, 168) & 0xFF) != 0; }
  int32_t GetInt(uint32_t q, const std::string& column) {
    WriteShortGuestString(g_scratch + kOffKey, column);
    return int32_t(g_.VCall(q, 132, g_scratch + kOffKey));
  }
  // First row's integer `column`, or -1 when there is no row.
  int32_t QueryInt(const std::string& sql, const std::string& column) {
    const uint32_t q = Open(sql);
    if (!q) return -1;
    int32_t v = -1;
    if (!AtEnd(q)) v = GetInt(q, column);
    Close(q);
    return v;
  }
  int32_t QueryId(const std::string& sql) { return QueryInt(sql, "Id"); }

 private:
  Guest& g_;
};

bool PlausibleEventId(uint32_t id) { return id > 0 && id < 100000; }

// Only act on an event the player has actually highlighted in the Event List
// this session (0 = none: main menu, career hub before opening the list...).
uint32_t TargetEvent() {
  const uint32_t ev = g_highlighted_event.load();
  return PlausibleEventId(ev) ? ev : 0;
}

void Beep(bool ok) {
#ifdef _WIN32
  MessageBeep(ok ? MB_OK : MB_ICONHAND);
#else
  (void)ok;
#endif
}

bool KeyDown(int vk) {
#ifdef _WIN32
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
  (void)vk;
  return false;
#endif
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

// The profile's career table prefix, "Profile<n>_". The game builds it in
// 0x82405E38 as sprintf("%s%d_", "Profile", [[profile+156]+28]), where
// [profile+156] is what profile vfunc +72 (0x824365E0) returns. Computed here
// directly instead of calling that code.
std::string ProfilePrefix(uint32_t profile) {
  const uint32_t data = Load32(profile + 156);
  if (!data) return {};
  const std::string prefix = "Profile" + std::to_string(int32_t(Load32(data + 28))) + "_";
  REXLOG_WARN("Dev keys: profile table prefix '{}'", prefix);
  return prefix;
}

// Win races of `event_id`, in race order, until `max_wins` races were won or
// none is left. Returns the number of races won, or -1 on error.
int WinRaces(Guest& g, uint32_t event_id, int max_wins) {
  Db db(g);
  const uint32_t profile = g.Call(kFnGetProfile, Load32(kProfileMgrGlobal));
  if (!g.ok() || !profile) { REXLOG_WARN("Dev keys: no active profile"); return -1; }
  const std::string prefix = ProfilePrefix(profile);
  if (prefix.empty()) { REXLOG_WARN("Dev keys: profile has no career tables"); return -1; }
  const uint32_t prefix_obj = g_scratch + kOffPrefix;
  WriteShortGuestString(prefix_obj, prefix);
  const std::string ev = std::to_string(event_id);

  int won = 0;
  while (won < max_wins && g.ok()) {
    const int32_t race = db.QueryId(
        "SELECT Races.Id AS Id FROM Races LEFT JOIN " + prefix + "Career_RaceResults AS R"
        " ON Races.Id=R.RaceId WHERE Races.EventId=" + ev +
        " AND COALESCE(R.Place,0)<>1 ORDER BY Races.RaceNumber, Races.Id LIMIT 1");
    REXLOG_WARN("Dev keys: next race to win: {}", race);
    if (race <= 0) break;
    // The race result record 0x8248C708 reads: +4 RaceId, +8 EventId, +25 place.
    const uint32_t rec = g_scratch + kOffResult;
    std::memset(REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(rec), 0, 48);
    Store32(rec + 4, uint32_t(race));
    Store32(rec + 8, event_id);
    Store8(rec + 25, 1);
    g.Call(kFnRecordRaceResult, rec, 0, prefix_obj);
    if (!g.ok()) break;
    REXLOG_INFO("Dev keys: event {} race {} recorded as 1st place", event_id, race);
    ++won;
  }
  if (!g.ok()) return -1;
  return won;
}

bool ResetEvent(Guest& g, uint32_t event_id) {
  Db db(g);
  const uint32_t profile = g.Call(kFnGetProfile, Load32(kProfileMgrGlobal));
  if (!g.ok() || !profile) { REXLOG_WARN("Dev keys: no active profile"); return false; }
  const std::string prefix = ProfilePrefix(profile);
  if (prefix.empty()) return false;
  const std::string ev = std::to_string(event_id);
  db.Exec("DELETE FROM " + prefix + "Career_RaceResults WHERE RaceId IN (SELECT Id FROM Races WHERE EventId=" +
          ev + ")");
  db.Exec("DELETE FROM " + prefix + "Career_EventResults WHERE EventId=" + ev);
  return g.ok();
}

// --- Profile settings ("Main/Credits", "Main/Level", ...) -------------------

// The active profile's settings store, or 0 when no profile is loaded.
uint32_t ProfileData(Guest& g) {
  const uint32_t profile = g.Call(kFnGetProfile, Load32(kProfileMgrGlobal));
  if (!g.ok() || !profile) { REXLOG_WARN("Dev keys: no active profile"); return 0; }
  const uint32_t data = Load32(profile + 156);
  // +24 = settings loaded; reads return 0 before that, so writing would
  // replace real values with 0-based ones.
  if (!data || !*REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(data + 24)) {
    REXLOG_WARN("Dev keys: profile settings not loaded");
    return 0;
  }
  return data;
}

uint32_t GetSetting(Guest& g, uint32_t data, const char* key) {
  WriteCStr(g_scratch + kOffSettingKey, key, 64);
  g.Call(kFnSettingGet, g_scratch + kOffVariant, data, g_scratch + kOffSettingKey);
  const uint32_t v = Load32(g_scratch + kOffVariant + 8);
  g.Call(kFnVariantDestroy, g_scratch + kOffVariant);
  return v;
}

void SetSetting(Guest& g, uint32_t data, const char* key, uint32_t value) {
  WriteCStr(g_scratch + kOffSettingKey, key, 64);
  g.Call(kFnVariantFromU32, g_scratch + kOffVariant, value);
  g.Call(kFnSettingSet, data, g_scratch + kOffSettingKey, g_scratch + kOffVariant);
  g.Call(kFnVariantDestroy, g_scratch + kOffVariant);
}

// F6: +100,000 credits through the game's own setter.
bool AddCredits(Guest& g, uint32_t amount) {
  const uint32_t data = ProfileData(g);
  if (!data) return false;
  const uint32_t before = GetSetting(g, data, "Main/Credits");
  if (!g.ok()) return false;
  const uint64_t sum = uint64_t(before) + amount;
  const uint32_t after = sum > kMaxCredits ? kMaxCredits : uint32_t(sum);
  g.Call(kFnSetCredits, data, after);
  if (!g.ok()) return false;
  REXLOG_WARN("Dev keys: credits {} -> {} (now reads {})", before, after, GetSetting(g, data, "Main/Credits"));
  return g.ok();
}

// F8: driver level +1. The game derives the level from Main/XP using the
// PlayerLevelDef thresholds (Id = level 0..999, XP = threshold; level = the
// highest Id whose XP <= Main/XP, see 0x823E73E0) and stores both Main/XP and Main/Level
// after a race (0x8249D0F4-0x8249D134). Do the same for level + 1.
bool DriverLevelUp(Guest& g) {
  const uint32_t data = ProfileData(g);
  if (!data) return false;
  Db db(g);
  const uint32_t level = GetSetting(g, data, "Main/Level");
  const uint32_t xp = GetSetting(g, data, "Main/XP");
  if (!g.ok()) return false;
  const int32_t next_xp = db.QueryInt(
      "SELECT XP AS V FROM PlayerLevelDef WHERE Id=" + std::to_string(level + 1), "V");
  if (!g.ok()) return false;
  if (next_xp < 0) {
    REXLOG_WARN("Dev keys: driver level {} is already the highest", level);
    return false;
  }
  const uint32_t new_xp = std::max<uint32_t>(xp, uint32_t(next_xp));
  SetSetting(g, data, "Main/XP", new_xp);
  SetSetting(g, data, "Main/Level", level + 1);
  if (!g.ok()) return false;
  REXLOG_WARN("Dev keys: driver level {} -> {} (XP {} -> {}); now reads level {}, XP {}", level, level + 1, xp,
              new_xp, GetSetting(g, data, "Main/Level"), GetSetting(g, data, "Main/XP"));
  return g.ok();
}

// F11: affinity level +1 for the make of the current career car.
// Career_Affinity (GroupId = MakeId, XP, TimeDriven, DistanceDriven);
// AffinityLevelDef (Id, XP, Discount, BonusCredits): the level is the
// highest row whose XP <= the make's XP.
bool AffinityLevelUp(Guest& g) {
  const uint32_t data = ProfileData(g);
  if (!data) return false;
  Db db(g);
  const uint32_t profile = g.Call(kFnGetProfile, Load32(kProfileMgrGlobal));
  const std::string prefix = profile ? ProfilePrefix(profile) : std::string();
  if (!g.ok() || prefix.empty()) return false;
  const uint32_t career_car = GetSetting(g, data, "Main/CareerCar");
  if (!g.ok()) return false;
  const std::string cc = std::to_string(career_car);
  // Main/CareerCar may be a garage Id or a car Id: try the garage first.
  const int32_t make = db.QueryInt("SELECT MakeID AS V FROM Data_Car WHERE Id=COALESCE((SELECT CarId FROM " + prefix +
                                       "Career_Garage WHERE Id=" + cc + ")," + cc + ")",
                                   "V");
  if (!g.ok()) return false;
  if (make <= 0) {
    REXLOG_WARN("Dev keys: no career car (Main/CareerCar={}), cannot pick a make", career_car);
    return false;
  }
  const std::string m = std::to_string(make);
  const std::string table = prefix + "Career_Affinity";
  const int32_t xp = db.QueryInt("SELECT COALESCE((SELECT XP FROM " + table + " WHERE GroupId=" + m + "),0) AS V", "V");
  if (!g.ok() || xp < 0) return false;
  const int32_t next_xp = db.QueryInt(
      "SELECT XP AS V FROM AffinityLevelDef WHERE XP>" + std::to_string(xp) + " ORDER BY XP LIMIT 1", "V");
  if (!g.ok()) return false;
  if (next_xp < 0) {
    REXLOG_WARN("Dev keys: affinity for make {} is already at the highest level", make);
    return false;
  }
  const std::string nx = std::to_string(next_xp);
  db.Exec("UPDATE " + table + " SET XP=" + nx + " WHERE GroupId=" + m);
  db.Exec("INSERT INTO " + table + " (GroupId, XP, TimeDriven, DistanceDriven) SELECT " + m + "," + nx +
          ",0,0 WHERE NOT EXISTS (SELECT 1 FROM " + table + " WHERE GroupId=" + m + ")");
  if (!g.ok()) return false;
  const int32_t level = db.QueryInt(
      "SELECT MAX(Id) AS V FROM AffinityLevelDef WHERE XP<=(SELECT XP FROM " + table + " WHERE GroupId=" + m + ")",
      "V");
  REXLOG_WARN("Dev keys: affinity for make {} (career car {}): XP {} -> {}, level now {}", make, career_car, xp,
              next_xp, level);
  return g.ok();
}

// Re-run the game's own refresh of the career header (driver level, XP bar)
// and of the hub's manufacturer affinity list, on the widgets the game last
// refreshed itself. The vtable check guards against a widget that has since
// been destroyed. Without this, F8/F11 changes only show after re-opening a
// screen. (Credits are already live: SetCredits notifies its listeners.)
void RefreshUi(Guest& g, bool header, bool affinity, uint32_t data) {
  if (header && g_header && Load32(g_header) == g_header_vtable && Load32(g_header + 152)) {
    g.Call(0x82631D60, g_header);
    REXLOG_WARN("Dev keys: refreshed career header {:08X}", g_header);
  }
  if (affinity && g_affinity && data && Load32(g_affinity) == g_affinity_vtable) {
    g.Call(0x826304C8, g_affinity, data);
    REXLOG_WARN("Dev keys: refreshed affinity list {:08X}", g_affinity);
  }
}

bool EnsureScratch() {
  if (g_scratch) return true;
  g_scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(kScratchSize);
  if (!g_scratch) return false;
  WriteCStr(g_scratch + kOffColId, "Id", 8);
  return true;
}

// ---- Evolution mode: progress tracking, decade unlock, T3 XP log, HUD state -------------------------------------
// Decade columns, 1960s-2020s, ten events each: decade d holds Events 2000+200*d .. 2009+200*d, the last one is the
// decade's championship. The 1960s are always open; every later column ships locked (Level 99). Decade d+1 opens
// (Level 0) when decade d is open, its championship is completed and at least four of its other nine events are
// completed in the active profile; otherwise it is set back to 99. EvoTick does this through the game's database
// layer. Events.Level is read by the Event List every time it is built, so the change shows the next time the list
// opens. A decade with no events in the pack yet is skipped (placeholder events can be added later).
constexpr int kEvoDecades = 7, kEvoFirstId = 2000, kEvoIdStride = 200, kEvoEventsPerDecade = 10;
constexpr int kEvoNeedOthers = 4, kEvoLockLevel = 99;
int EvoFirstId(int d) { return kEvoFirstId + kEvoIdStride * d; }
int EvoChampId(int d) { return EvoFirstId(d) + kEvoEventsPerDecade - 1; }

struct EvoHudState {
  bool have = false;           // at least one successful tick
  uint32_t xp = 0, level = 0;  // Main/XP, Main/Level
  int others[kEvoDecades] = {};       // completed non-championship events per decade
  bool champ[kEvoDecades] = {};       // decade championship completed
  int lock_level[kEvoDecades] = {};   // current Events.Level of each decade column (-1 not in the pack)
  int write = 0;               // 0 none, 1 unlock/relock written, 2 write FAILED
  uint32_t last_event = 0;     // last recorded race result
  int last_place = 0;
  long long last_tick_ms = 0;
  std::string last_why;
};
std::mutex g_evo_mu;
EvoHudState g_evo;

std::atomic<long long> g_evo_xp_note_ms{0};
std::atomic<int> g_evo_xp_polls_done{3};
long long g_evo_boot_next_ms = 0;
int g_evo_boot_tries = 0;
bool g_evo_boot_done = false;
long long EvoNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool EvoEnabled() {
  static const bool enabled = [] {
    const char* v = std::getenv("FM4_EVO");
    return !(v && v[0] == '0');
  }();
  return enabled;
}

// One progress check + XP log + lock/unlock of every decade column. Runs on the game thread inside DevKeysFrame.
bool EvoTick(Guest& g, const char* why) {
  const uint32_t data = ProfileData(g);
  if (!data) return false;
  const uint32_t profile = g.Call(kFnGetProfile, Load32(kProfileMgrGlobal));
  if (!g.ok() || !profile) return false;
  const std::string prefix = ProfilePrefix(profile);
  if (prefix.empty()) return false;
  EvoHudState st;
  st.xp = GetSetting(g, data, "Main/XP");
  st.level = GetSetting(g, data, "Main/Level");
  if (!g.ok()) return false;
  Db db(g);
  const std::string tbl = prefix + "Career_EventResults";
  const std::string done = " AND Place BETWEEN 1 AND 16";  // 17 = not every race of the event has a result
  bool prev_open = true;  // the 1960s are always open
  int unlocked_through = 0;
  for (int d = 0; d < kEvoDecades; ++d) {
    const int first = EvoFirstId(d), champ_id = EvoChampId(d);
    st.others[d] = std::max(0, db.QueryInt("SELECT COUNT(*) AS V FROM " + tbl + " WHERE EventId BETWEEN " +
                                           std::to_string(first) + " AND " + std::to_string(champ_id - 1) + done, "V"));
    st.champ[d] = db.QueryInt("SELECT COUNT(*) AS V FROM " + tbl + " WHERE EventId=" + std::to_string(champ_id) + done, "V") > 0;
    const bool want_open = d == 0 || prev_open;
    if (d > 0) {
      const std::string range = " Id BETWEEN " + std::to_string(first) + " AND " + std::to_string(champ_id);
      int cur = db.QueryInt("SELECT MIN(Level) AS V FROM Events WHERE" + range, "V");
      if (!g.ok()) return false;
      if (cur >= 0 && (want_open ? cur != 0 : cur != kEvoLockLevel)) {
        db.Exec("UPDATE Events SET Level=" + std::to_string(want_open ? 0 : kEvoLockLevel) + " WHERE" + range);
        const int after = db.QueryInt("SELECT MIN(Level) AS V FROM Events WHERE" + range, "V");
        st.write = (after == (want_open ? 0 : kEvoLockLevel)) ? 1 : 2;
        cur = after;
        REXLOG_INFO("Evolution: {}s column {} (previous decade: others {}/{} + championship {}), write {}", 1960 + 10 * d,
                    want_open ? "UNLOCKED" : "re-locked", st.others[d - 1], kEvoNeedOthers,
                    st.champ[d - 1] ? "done" : "not done", st.write == 1 ? "ok" : "FAILED");
      }
      st.lock_level[d] = cur;
    } else {
      st.lock_level[d] = 0;
    }
    if (want_open) unlocked_through = d;
    // decade d+1 may open only if decade d is open and qualifies
    prev_open = want_open && st.champ[d] && st.others[d] >= kEvoNeedOthers;
  }
  st.have = true;
  st.last_tick_ms = EvoNowMs();
  st.last_why = why;
  {
    std::lock_guard<std::mutex> lock(g_evo_mu);
    st.last_event = g_evo.last_event;
    st.last_place = g_evo.last_place;
    if (st.write == 0) st.write = g_evo.write;
    g_evo = st;
  }
  REXLOG_INFO("Evolution T3: {}, Main/XP={} Main/Level={}; open through the {}s; 1960s others {}/{} championship {}", why,
              st.xp, st.level, 1960 + 10 * unlocked_through, st.others[0], kEvoNeedOthers, st.champ[0] ? "done" : "no");
  return true;
}

void EvoPoll(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr) {
  if (!EvoEnabled() || !REXCVAR_GET(fm4_evo_unlock) || g_faulted) return;
  const long long now = EvoNowMs();
  const char* why = nullptr;
  bool startup = false;
  if (!g_evo_boot_done && g_evo_boot_tries < 90 && now >= g_evo_boot_next_ms) {
    g_evo_boot_next_ms = now + 10000;
    ++g_evo_boot_tries;
    why = "startup check";
    startup = true;
  }
  static const long long kDelayMs[3] = {2000, 6000, 15000};
  const int polls = g_evo_xp_polls_done.load();
  const long long note = g_evo_xp_note_ms.load();
  if (polls < 3 && note && now - note >= kDelayMs[polls]) {
    g_evo_xp_polls_done = polls + 1;
    why = "after the race result";
    startup = false;
  }
  if (!why) return;
  if (!EnsureScratch()) return;
  Guest g(r1, r13, fpscr);
  const bool ok = EvoTick(g, why);
  if (!g.ok()) { g_faulted = true; return; }
  if (ok && startup) g_evo_boot_done = true;
}

// Small always-on readout, top left, for tracking Evolution state while testing. FM4_EVO_HUD=0 hides it.
class EvolutionHud : public rex::ui::ImGuiDialog {
 public:
  explicit EvolutionHud(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!REXCVAR_GET(fm4_evo_hud)) return;
    EvoHudState s;
    {
      std::lock_guard<std::mutex> lock(g_evo_mu);
      s = g_evo;
    }
    const float scale = std::max(0.6f, io.DisplaySize.y / 1080.0f);
    ImGui::SetNextWindowPos(ImVec2(8.0f * scale, 8.0f * scale), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("##fm4_evo_hud", nullptr, flags)) { ImGui::End(); return; }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f * scale);
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "EVOLUTION  build: AI 15, decades 1960s-2020s");
    if (!s.have) {
      ImGui::TextDisabled("waiting for a profile ...");
    } else {
      ImGui::Text("XP %u   Level %u", s.xp, s.level);
      for (int d = 0; d < kEvoDecades; ++d) {
        const bool qualified = s.champ[d] && s.others[d] >= kEvoNeedOthers;
        const bool open = s.lock_level[d] == 0;
        ImGui::TextColored(open ? (qualified ? ImVec4(0.55f, 0.95f, 0.55f, 1.0f) : ImVec4(1.0f, 0.8f, 0.3f, 1.0f))
                                : ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "%ds: %s  %d/%d + champ %s", 1960 + 10 * d,
                           s.lock_level[d] < 0 ? "no events" : (open ? "OPEN" : "LOCKED"), s.others[d], kEvoNeedOthers,
                           s.champ[d] ? "done" : "-");
      }
      if (s.write == 2) ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.25f, 1.0f), "lock write FAILED (Events not writable?)");
      else if (s.write == 1) ImGui::TextDisabled("lock written this session");
    }
    if (s.last_event) ImGui::Text("last result: event %u, place %d", s.last_event, s.last_place);
    ImGui::PopFont();
    ImGui::End();
  }
};

// ---- Evolution intro popup ----------------------------------------------------------------------------------------
// Shown once per profile, the first time the profile opens the Event List (CCareerEventSelect, where the Evolution
// events live) while the Evolution pack's events (2000+) are in the database. Same mechanism as the stock Porsche
// "FORZA EXPANSION" popup, which CMainMenu (0x8258C8F8) gates on the profile int "Main/Flags" bit 0x10000 and sets on
// dismiss: our own bit there, read and written with the game's own functions, so the flag is saved inside the
// profile and new profiles start without it (0x8242F080 zeroes Main/Flags).
//   0x8251BCD0 ()                   profile flags object (ignores its argument)
//   0x823E5400 (obj, mask) -> bool  Main/Flags & mask != 0
//   0x82405A10 (obj, mask, on)      set or clear mask in Main/Flags
// Bits in use by the game (every constant mask passed to those two and to the show-once helper 0x8251BD30): 0-16,
// 33-37, 40, 42, 56-62; three callers pass a 32-bit variable. Bit 50 is ours.
// The panel is ImGui; the Event List's input handler (CCareerEventSelect vtable 0x82032564 slot 9, 0x824FBED8) is
// skipped while it is open. FM4_EVO_INTRO: 0 = off, force = show on every Event List open, reset = clear the bit once
// per launch (then normal once-per-profile behaviour).
constexpr uint32_t kFnProfileFlagsObj = 0x8251BCD0;
constexpr uint32_t kFnProfileFlagGet = 0x823E5400;
constexpr uint32_t kFnProfileFlagSet = 0x82405A10;
constexpr uint64_t kEvoIntroFlag = 1ull << 50;

enum EvoIntroState : int { kIntroIdle = 0, kIntroCheck = 1, kIntroShowing = 2, kIntroDismissed = 3 };
std::atomic<int> g_intro_state{kIntroIdle};
std::atomic<long long> g_intro_block_until_ms{0};  // keep swallowing Event List input briefly after closing
bool g_intro_reset_done = false;

int EvoIntroMode() {  // 0 off, 1 normal, 2 force, 3 reset
  static const int mode = [] {
    const char* v = std::getenv("FM4_EVO_INTRO");
    if (!EvoEnabled() || (v && v[0] == '0')) return 0;
    if (v && std::strcmp(v, "force") == 0) return 2;
    if (v && std::strcmp(v, "reset") == 0) return 3;
    return 1;
  }();
  return mode;
}

constexpr const char* kIntroTitle = "FORZA EVOLUTION";
constexpr const char* kIntroBody =
    "Welcome to Evolution. Your career starts in 1960 with modest credits and a period car. Every event has an entry "
    "fee and a purse, so a bad result costs you. Win the decade's championship and enough of its events to open the "
    "next decade. Championships award a random car from that era's race cars. Buy, sell and repair carefully: your "
    "garage is your career.";

// Main thread, from DevKeysFrame: decide whether to show the panel, and write the flag after it was dismissed.
void EvoIntroPoll(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr) {
  const int state = g_intro_state.load();
  if (state != kIntroCheck && state != kIntroDismissed) return;
  if (g_faulted || !EnsureScratch()) { g_intro_state = kIntroIdle; return; }
  Guest g(r1, r13, fpscr);
  if (!ProfileData(g)) { g_intro_state = kIntroIdle; return; }  // no profile loaded: try again on the next list open
  const uint32_t flags = g.Call(kFnProfileFlagsObj);
  if (!g.ok() || !flags) { g_intro_state = kIntroIdle; return; }

  if (state == kIntroDismissed) {
    g.Call(kFnProfileFlagSet, flags, kEvoIntroFlag, 1);
    const bool now_set = (g.Call(kFnProfileFlagGet, flags, kEvoIntroFlag) & 0xFF) != 0;
    REXLOG_INFO("Evolution intro: dismissed, profile flag {}", g.ok() && now_set ? "set" : "NOT set");
    g_intro_state = kIntroIdle;
    return;
  }

  if (EvoIntroMode() == 3 && !g_intro_reset_done) {
    g_intro_reset_done = true;
    g.Call(kFnProfileFlagSet, flags, kEvoIntroFlag, 0);
    REXLOG_INFO("Evolution intro: FM4_EVO_INTRO=reset, profile flag cleared");
  }
  Db db(g);
  const int evo_events = db.QueryInt("SELECT COUNT(*) AS V FROM Events WHERE Id BETWEEN " +
                                         std::to_string(kEvoFirstId) + " AND " +
                                         std::to_string(EvoChampId(kEvoDecades - 1)), "V");
  const bool seen = (g.Call(kFnProfileFlagGet, flags, kEvoIntroFlag) & 0xFF) != 0;
  if (!g.ok()) { g_faulted = true; g_intro_state = kIntroIdle; return; }
  if (evo_events <= 0) {
    REXLOG_INFO("Evolution intro: skipped, no Evolution events in the database");
    g_intro_state = kIntroIdle;
  } else if (seen && EvoIntroMode() != 2) {
    REXLOG_INFO("Evolution intro: skipped, already seen on this profile");
    g_intro_state = kIntroIdle;
  } else {
    REXLOG_INFO("Evolution intro: shown{}", seen ? " (FM4_EVO_INTRO=force)" : "");
    g_intro_state = kIntroShowing;
  }
}

class EvolutionIntro : public rex::ui::ImGuiDialog {
 public:
  explicit EvolutionIntro(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (g_intro_state.load() != kIntroShowing) { was_open_ = false; return; }
    if (!was_open_) {  // just opened: the A press that opened the list may still be down
      was_open_ = true;
      prev_buttons_ = 0xFFFF;
      opened_ms_ = EvoNowMs();
    }
    const uint16_t pressed = PollPad();
    const bool armed = EvoNowMs() - opened_ms_ >= 600;  // a held or repeated press must not close it at once
    if (armed && ((pressed & (kPadA | kPadB | kPadStart)) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                  ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Space) ||
                  ImGui::IsKeyPressed(ImGuiKey_Escape))) {
      was_open_ = false;
      g_intro_block_until_ms = EvoNowMs() + 400;  // the closing press must not also pick an event
      g_intro_state = kIntroDismissed;
      return;
    }

    const float scale = std::max(0.6f, io.DisplaySize.y / 1080.0f);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    const ImGuiWindowFlags backdrop = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    ImGui::Begin("##fm4_evo_intro_bg", nullptr, backdrop);
    ImGui::End();

    const ImVec2 size(std::min(io.DisplaySize.x * 0.9f, 1100.0f * scale), 0.0f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##fm4_evo_intro", nullptr, flags)) {
      const float pad = 28.0f * scale;
      ImGui::Dummy(ImVec2(0, pad * 0.5f));
      ImGui::Indent(pad);
      ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.4f * scale);
      ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "%s", kIntroTitle);
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, pad * 0.4f));
      ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f * scale);
      ImGui::PushTextWrapPos(size.x - pad);
      ImGui::TextUnformatted(kIntroBody);
      ImGui::PopTextWrapPos();
      ImGui::Dummy(ImVec2(0, pad * 0.6f));
      ImGui::TextDisabled("A / Enter   Continue");
      ImGui::PopFont();
      ImGui::Unindent(pad);
      ImGui::Dummy(ImVec2(0, pad * 0.5f));
    }
    ImGui::End();
  }

 private:
  static constexpr uint16_t kPadStart = 0x0010, kPadA = 0x1000, kPadB = 0x2000;

  // Newly pressed pad buttons (any controller), XInput bit layout.
  uint16_t PollPad() {
    uint16_t now = 0;
#ifdef _WIN32
    for (DWORD i = 0; i < 4; ++i) {
      XINPUT_STATE st{};
      if (XInputGetState(i, &st) == ERROR_SUCCESS) now |= st.Gamepad.wButtons;
    }
#endif
    const uint16_t pressed = now & ~prev_buttons_;
    prev_buttons_ = now;
    return pressed;
  }

  bool was_open_ = false;
  uint16_t prev_buttons_ = 0xFFFF;
  long long opened_ms_ = 0;
};

}  // namespace

bool EvoUnlockEnabled() { return REXCVAR_GET(fm4_evo_unlock); }  // mods_menu.cpp: Evolution decade unlock switch
void EvoUnlockSetEnabled(bool on) { REXCVAR_SET(fm4_evo_unlock, on); }
// garage_menu.cpp: start year of the newest decade whose Event List column is open (1960 until the first tick).
int EvoNewestUnlockedDecadeYear() {
  std::lock_guard<std::mutex> lock(g_evo_mu);
  int newest = 0;
  if (g_evo.have)
    for (int d = 0; d < kEvoDecades; ++d)
      if (g_evo.lock_level[d] == 0) newest = d;
  return 1960 + 10 * newest;
}
bool EvoHudVisible() { return REXCVAR_GET(fm4_evo_hud); }  // mods_menu.cpp: Evolution HUD switch
void EvoHudSetVisible(bool on) { REXCVAR_SET(fm4_evo_hud, on); }

// 0x824F4344, Event List selection-changed handler: r3 = highlighted EventId.
// 0x82631D6C, inside RefreshHeader (0x82631D60): r3 = header widget.
void DevKeysHeaderRefreshed(PPCRegister& r3) {
  if (!r3.u32) return;
  g_header = r3.u32;
  g_header_vtable = Load32(r3.u32);
}

// 0x826304DC, inside RefreshAffinity (0x826304C8): r31 = affinity component.
void DevKeysAffinityRefreshed(PPCRegister& r31) {
  if (!r31.u32) return;
  g_affinity = r31.u32;
  g_affinity_vtable = Load32(r31.u32);
}

void DevKeysEventHighlighted(PPCRegister& r3) {
  if (PlausibleEventId(r3.u32)) g_highlighted_event = r3.u32;
}

// 0x822F787C, main game loop, once per frame.
void DevKeysFrame(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr) {
  EvoPoll(r1, r13, fpscr);
  EvoIntroPoll(r1, r13, fpscr);
  if (!DevKeysEnabled()) return;
  static bool announced = false;
  if (!announced) {
    announced = true;
    REXLOG_WARN("Dev keys: active (F6 +100k credits, F8 driver level +1, F9 win next race, Shift+F9 win all, F10 reset event, F11 affinity +1)");
  }
  // Edge-detect the keys we use (pressed this frame, not last frame).
  auto pressed = [](int vk) {
    const bool down = KeyDown(vk);
    const bool was = g_prev_keys[vk & 0xFF];
    g_prev_keys[vk & 0xFF] = down;
    return down && !was;
  };
  const bool f6 = pressed(VK_F6), f8 = pressed(VK_F8), f9_pressed = pressed(VK_F9), f10_pressed = pressed(VK_F10),
             f11 = pressed(VK_F11);
  if (!f6 && !f8 && !f9_pressed && !f10_pressed && !f11) return;
  const char* name = f6 ? "F6" : f8 ? "F8" : f9_pressed ? "F9" : f10_pressed ? "F10" : "F11";
  REXLOG_WARN("Dev keys: {} pressed, focus={}, highlighted event={}", name, GameHasFocus(), g_highlighted_event.load());
  if (!GameHasFocus()) return;
  if (g_faulted) {
    REXLOG_WARN("Dev keys: disabled for this session after an earlier failure (restart the game)");
    Beep(false);
    return;
  }
  if (!EnsureScratch()) { Beep(false); return; }

  if (f6 || f8 || f11) {
    Guest g(r1, r13, fpscr);
    bool ok = false;
    if (f6) ok = AddCredits(g, 100000);
    else if (f8) ok = DriverLevelUp(g);
    else ok = AffinityLevelUp(g);
    if (ok && (f8 || f11) && g.ok()) {
      const uint32_t profile = g.Call(kFnGetProfile, Load32(kProfileMgrGlobal));
      RefreshUi(g, true, f11, profile ? Load32(profile + 156) : 0);
    }
    REXLOG_WARN("Dev keys: {} {}", name, ok ? "done" : "FAILED");
    Beep(ok);
    return;
  }

  const uint32_t event_id = TargetEvent();
  if (!event_id) {
    REXLOG_WARN("Dev keys: no event highlighted yet (open the Event List first)");
    Beep(false);
    return;
  }
  Guest g(r1, r13, fpscr);
  if (f9_pressed) {
    const bool all = KeyDown(VK_SHIFT);
    const int won = WinRaces(g, event_id, all ? 64 : 1);
    if (won < 0) {
      REXLOG_WARN("Dev keys: F9 failed for event {}", event_id);
      Beep(false);
    } else if (won == 0) {
      REXLOG_INFO("Dev keys: event {} has no race left to win", event_id);
      Beep(false);
    } else {
      REXLOG_INFO("Dev keys: F9 won {} race(s) of event {}", won, event_id);
      Beep(true);
    }
  } else {
    const bool ok = ResetEvent(g, event_id);
    REXLOG_INFO("Dev keys: F10 reset event {} {}", event_id, ok ? "done" : "FAILED");
    Beep(ok);
  }
}

// Evolution fork: a race result was just recorded (called from the existing prize-car hooks 0x8248CA30 / 0x8248CAB0, so
// no new hook entry). The post-race XP and result rows settle a little later, so DevKeysFrame runs EvoTick 2, 6 and 15 s
// after the last result (T3: the XP of a race is the difference between the settled values of consecutive races, in the
// "Evolution T3" lines of fm4.log). FM4_EVO=0 switches all Evolution tracking off.
void EvoNoteRaceResult(uint32_t event_id, int place) {
  {
    std::lock_guard<std::mutex> lock(g_evo_mu);
    g_evo.last_event = event_id;
    g_evo.last_place = place;
  }
  g_evo_xp_note_ms = EvoNowMs();
  g_evo_xp_polls_done = 0;
}

// 0x824EDE00, CCareerEventSelect (Event List) vfunc slot 5, runs each time the screen is entered: queue the intro
// check for the next frame (EvoIntroPoll).
void EvoIntroEventListEnter(PPCRegister& r3) {
  (void)r3;
  if (EvoIntroMode() == 0) return;
  int idle = kIntroIdle;
  g_intro_state.compare_exchange_strong(idle, kIntroCheck);
}

// 0x824FBED8, CCareerEventSelect input handler entry. True = skip it (input swallowed) while the intro is open.
bool EvoIntroInput(PPCRegister& r3) {
  const int state = g_intro_state.load();
  if (state != kIntroShowing && state != kIntroDismissed && EvoNowMs() >= g_intro_block_until_ms.load()) return false;
  r3.u64 = 1;  // reported as handled
  return true;
}

std::unique_ptr<rex::ui::ImGuiDialog> CreateEvolutionIntro(rex::ui::ImGuiDrawer* drawer) {
  if (!drawer || EvoIntroMode() == 0) return nullptr;
  return std::make_unique<EvolutionIntro>(drawer);
}

std::unique_ptr<rex::ui::ImGuiDialog> CreateEvolutionHud(rex::ui::ImGuiDrawer* drawer) {
  const char* v = std::getenv("FM4_EVO_HUD");
  if (!drawer || !EvoEnabled()) return nullptr;
  if (v && v[0] == '0') REXCVAR_SET(fm4_evo_hud, false);
  return std::make_unique<EvolutionHud>(drawer);
}
