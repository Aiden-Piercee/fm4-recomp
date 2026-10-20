// ---------------------------------------------------------------------------
// Home-screen CARS menu: BRAND CENTRAL + USED CAR DEALERSHIP (Forza-4-Evolution).
// Research: /mnt/project-files/evolution-research/garage-menu-brand-central-used-lot-2026-10-20.md
//
// The CARS submenu of the home screen (102_H_CAR_HOME) is built in code by
// sub_825896B8 (menu object at [screen+476]): MY CARS id 8, BUY CARS id 9,
// TAKE PHOTO id 10, CHANGE HOMESPACE id 11. Picks go to sub_82591398(screen, id),
// which switches on id - 4 (4..26) and then opens the screen found for the id in
// the builder's id -> screen map at [screen+356]. Ids 27+ are unused.
//
// With Evolution on (FM4_EVO not 0) this file:
//   * relabels BUY CARS as BRAND CENTRAL,
//   * adds USED CAR DEALERSHIP (id 27) right below it,
//   * turns a pick of id 27 into a pick of id 9, so the stock Buy Cars path runs
//     (car-flow 11, push 1 on G+748, open screen 106 = 115_C_BUY_MFRSELECT), and
//     remembers which of the two was picked (GarageUsedLotActive()).
// Id 27 has no entry in the id -> screen map, so GarageMenuAdd must never run
// without GarageMenuSelect: the stock lookup would return null.
//
// Nothing filters the car list yet: both items show the same stock list until
// the dealer year filter (H5) reads GarageUsedLotActive().
//
// Hooks (fm4_hooks.toml):
//   GarageMenuRelabel 0x82589910  after "lwz r5,-960(r11)" (r5 = IDS_Buy_Cars id),
//                                 just before the label is made for item 9.
//   GarageMenuAdd     0x82589958  after BUY CARS is added, before TAKE PHOTO
//                                 (r30 = CARS submenu).
//   GarageMenuSelect  0x825913AC  pick handler, before "mr r31,r4" (r4 = item id).
//   Labels are answered by GarageMenuText, called from ModsMenuText (GetText
//   hook 0x823804E8, mods_menu.cpp).
//
// Guest functions used (same as mods_menu.cpp):
//   0x822B1A18 (out, table, id)                    make a localized-string handle
//   0x82479318 (dst, src)                          copy a handle (AddRef)
//   0x82675768 (menu, &label, id, enabled, poster) add leaf item
//
// FM4_GARAGE_MENU=0 turns everything off.
// ---------------------------------------------------------------------------
#include <rex/cvar.h>
#include <rex/ppc.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/thread_state.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// F4 overlay: Forza Motorsport 4 > Evolution. Also the Mods page Features tab (GarageMenuSetUserEnabled).
REXCVAR_DEFINE_BOOL(fm4_garage_menu, true, "Forza Motorsport 4/Evolution",
                    "BRAND CENTRAL / USED CAR DEALERSHIP in the CARS menu (off = stock BUY CARS). "
                    "Takes effect the next time the home menu is built");

namespace {

// ---- guest addresses ------------------------------------------------------
constexpr uint32_t kFnMakeLocString = 0x822B1A18;
constexpr uint32_t kFnCopyHandle = 0x82479318;
constexpr uint32_t kFnAddLeaf = 0x82675768;
constexpr uint32_t kStrTableMain = 0x8200A350;  // "Main" (same table the builder uses)

constexpr uint32_t kIdBuyCars = 9;
constexpr uint32_t kIdUsedLot = 27;

// Private string ids, answered only by GarageMenuText. Kept apart from the
// MODS tile's range (0xF4D0E000 + n).
constexpr uint32_t kTextIdBase = 0xF4D0E100;
constexpr const char* kLabels[] = {"BRAND CENTRAL", "USED CAR DEALERSHIP"};
constexpr uint32_t kNumLabels = sizeof(kLabels) / sizeof(kLabels[0]);
constexpr uint32_t kLabelBrandCentral = 0;
constexpr uint32_t kLabelUsedLot = 1;

// Guest scratch: handles + UTF-16BE label texts.
constexpr uint32_t kScratchSize = 0x200;
constexpr uint32_t kOffHandleA = 0x000;
constexpr uint32_t kOffHandleB = 0x010;
constexpr uint32_t kOffText = 0x040;  // kNumLabels x kTextSlot
constexpr uint32_t kTextSlot = 0x60;  // 47 UTF-16 chars + NUL

uint32_t g_scratch = 0;
bool g_faulted = false;
std::atomic<bool> g_used_lot{false};

bool Enabled() {
  static const bool on = [] {
    const char* evo = std::getenv("FM4_EVO");
    const char* v = std::getenv("FM4_GARAGE_MENU");
    return !(evo && evo[0] == '0') && !(v && v[0] == '0');
  }();
  return on && REXCVAR_GET(fm4_garage_menu);
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
// ASCII -> UTF-16BE, NUL-terminated.
void WriteWide(uint32_t a, const char* s, uint32_t cap_chars) {
  uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(a);
  uint32_t i = 0;
  for (; s[i] && i + 1 < cap_chars; ++i) { p[2 * i] = 0; p[2 * i + 1] = uint8_t(s[i]); }
  p[2 * i] = 0; p[2 * i + 1] = 0;
}
uint32_t TextAddr(uint32_t index) { return g_scratch + kOffText + index * kTextSlot; }

bool EnsureScratch() {
  if (g_scratch) return true;
  g_scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(kScratchSize);
  if (!g_scratch) return false;
  std::memset(REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(g_scratch), 0, kScratchSize);
  for (uint32_t i = 0; i < kNumLabels; ++i) WriteWide(TextAddr(i), kLabels[i], kTextSlot / 2);
  return true;
}

// Guest call helper: same protocol as mods_menu.cpp / dev_keys.cpp (runs on the
// hooked thread's own context, stack moved 0x400 below the hooked frame, guest
// reentries resumed here, whole context restored afterwards).
class Guest {
 public:
  explicit Guest(PPCRegister& r1) {
    auto* ts = rex::runtime::ThreadState::Get();
    ctx_ = ts ? ts->context() : nullptr;
    if (!ctx_ || &ctx_->r1 != &r1) {
      REXLOG_WARN("Garage menu: hook is not running on the thread's own context; skipped");
      ok_ = false;
      return;
    }
    sp_ = (r1.u32 - 0x400) & ~0xFu;
  }
  bool ok() const { return ok_; }

  uint32_t Call(uint32_t target, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0,
                uint32_t e = 0) {
    if (!ok_ || !target) { ok_ = false; return 0; }
    static_assert(sizeof(PPCContext) <= sizeof(saved_));
    std::memcpy(saved_, static_cast<void*>(ctx_), sizeof(PPCContext));
    PPCContext& ctx = *ctx_;
    ctx.r1.u64 = sp_;
    ctx.r3.u64 = a; ctx.r4.u64 = b; ctx.r5.u64 = c; ctx.r6.u64 = d; ctx.r7.u64 = e;
    ctx.lr = 0;
    uint32_t next = target;
    bool nonlocal = false, failed = false;
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
        next = uint32_t(ctx.lr);
        nonlocal = true;
      }
      if (next == 0) break;
    }
    const uint32_t result = ctx.r3.u32;
    std::memcpy(static_cast<void*>(ctx_), saved_, sizeof(PPCContext));
    if (failed) {
      g_faulted = true;
      ok_ = false;
      REXLOG_WARN("Garage menu: guest call {:08X} could not finish (stuck at {:08X})", target, next);
      return 0;
    }
    return result;
  }
  uint32_t VCall(uint32_t obj, uint32_t offset, uint32_t a = 0) {
    if (!ok_ || !obj) { ok_ = false; return 0; }
    return Call(Load32(Load32(obj) + offset), obj, a);
  }

 private:
  PPCContext* ctx_ = nullptr;
  uint32_t sp_ = 0;
  bool ok_ = true;
  alignas(64) static inline unsigned char saved_[16384];
};

// Adds one leaf the way the builder adds the stock CARS items (enabled, no poster).
bool AddLeaf(Guest& g, uint32_t menu, uint32_t label_index, uint32_t id) {
  const uint32_t ha = g_scratch + kOffHandleA, hb = g_scratch + kOffHandleB;
  Store32(ha, 0);
  Store32(hb, 0);
  g.Call(kFnMakeLocString, ha, kStrTableMain, kTextIdBase + label_index);
  g.Call(kFnCopyHandle, hb, ha);  // the add consumes (releases) this copy
  g.Call(kFnAddLeaf, menu, hb, id, 1, 0);
  if (const uint32_t obj = Load32(ha)) g.VCall(obj, 8);  // release our own reference
  return g.ok();
}

}  // namespace

// Mods page Features tab (mods_menu.cpp).
bool GarageMenuUserEnabled() { return REXCVAR_GET(fm4_garage_menu); }
void GarageMenuSetUserEnabled(bool on) { REXCVAR_SET(fm4_garage_menu, on); }

// ---------------------------------------------------------------------------
// Older-cars-only filter. Called from the single handler at 0x8250D790 (hooks.cpp,
// PrizeCarPopupSetQuery) with r4 = the formatted car-list query. For the Buy Cars
// list (flow 11) while this menu is on:
//   BRAND CENTRAL            AND Garage.Year BETWEEN d AND d+9   (newest unlocked decade)
//   USED CAR DEALERSHIP      AND Garage.Year < d                 (everything older)
// The rewritten query goes in our own guest buffer and query_ptr is pointed at it.
// ---------------------------------------------------------------------------
int EvoNewestUnlockedDecadeYear();  // dev_keys.cpp

namespace {
constexpr uint32_t kGarageGlobal = 0x82DC3828;  // G; G+16 = car-list flow (11 = buy new)
constexpr uint32_t kFlowBuyNew = 11;
constexpr uint32_t kQueryBufSize = 4096;
uint32_t g_query_buf = 0;
}  // namespace

bool GarageMenuCarListQuery(uint32_t& query_ptr) {
  if (!Enabled() || g_faulted || !query_ptr) return false;
  const uint32_t g = Load32(kGarageGlobal);
  if (g < 0x10000 || Load32(g + 16) != kFlowBuyNew) return false;
  const char* p = REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(query_ptr);
  const std::string q(p, strnlen(p, kQueryBufSize));
  if (q.size() >= kQueryBufSize - 128 || q.find("FROM Drivable_Data_Car AS Garage WHERE ") == std::string::npos)
    return false;
  const size_t order = q.rfind(" ORDER BY ");
  if (order == std::string::npos) return false;
  const int decade = EvoNewestUnlockedDecadeYear();
  const std::string clause = g_used_lot.load()
      ? " AND Garage.Year < " + std::to_string(decade)
      : " AND Garage.Year BETWEEN " + std::to_string(decade) + " AND " + std::to_string(decade + 9);
  if (!g_query_buf) g_query_buf = REX_KERNEL_MEMORY()->SystemHeapAlloc(kQueryBufSize);
  if (!g_query_buf) return false;
  const std::string out = q.substr(0, order) + clause + q.substr(order);
  uint8_t* dst = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(g_query_buf);
  std::memcpy(dst, out.c_str(), out.size() + 1);
  query_ptr = g_query_buf;
  REXLOG_INFO("Garage menu: {} list{}", g_used_lot.load() ? "used" : "Brand Central", clause);
  return true;
}

// For the dealer year filter (H5): true while the buy screens were opened from
// USED CAR DEALERSHIP, false when opened from BRAND CENTRAL.
bool GarageUsedLotActive() { return g_used_lot.load(); }

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

// r5 = string id about to be used for item 9's label.
void GarageMenuRelabel(PPCRegister& r5) {
  if (!Enabled() || g_faulted || !EnsureScratch()) return;
  r5.u64 = kTextIdBase + kLabelBrandCentral;
}

// r30 = CARS submenu, BUY CARS already added.
void GarageMenuAdd(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr, PPCRegister& r30) {
  (void)r13; (void)fpscr;
  if (!Enabled() || g_faulted || !EnsureScratch() || !r30.u32) return;
  Guest g(r1);
  if (!g.ok()) return;
  const bool ok = AddLeaf(g, r30.u32, kLabelUsedLot, kIdUsedLot);
  REXLOG_INFO("Garage menu: USED CAR DEALERSHIP {} (menu {:08X})", ok ? "added" : "could not be added", r30.u32);
}

// r4 = picked item id, before the handler copies it to r31 and looks it up.
void GarageMenuSelect(PPCRegister& r4) {
  const uint32_t id = r4.u32;
  if (id == kIdUsedLot) {
    g_used_lot = true;
    r4.u64 = kIdBuyCars;  // stock Buy Cars path from here on
    REXLOG_INFO("Garage menu: USED CAR DEALERSHIP picked");
    return;
  }
  if (g_used_lot.exchange(false)) REXLOG_INFO("Garage menu: used lot cleared (item {} picked)", id);
}

// Called from ModsMenuText (GetText). True = r3 now points at our UTF-16BE text.
bool GarageMenuText(PPCRegister& r3) {
  if (!g_scratch) return false;
  const uint32_t id = Load32(r3.u32 + 8);
  if (id < kTextIdBase || id >= kTextIdBase + kNumLabels) return false;
  r3.u64 = TextAddr(id - kTextIdBase);
  return true;
}
