// ---------------------------------------------------------------------------
// Upgrade shop: show brake and exhaust upgrades on the car (Forza-4-Evolution).
// Research: claude/brake-upgrade-visuals-research-2026-10-12.md
//           claude/exhaust-upgrade-visuals-research-2026-10-12.md
//
// The stock shop never reloads the car model while you are in it. Ride
// height, bumpers and wheels can change on the loaded car, but brakes are
// chosen only when the car model is built (sub_823D4C40), so brake upgrades
// (and the brake growth that comes with bigger rims) only appear after you
// leave the shop. Exhaust upgrades have no visual at all in the stock game;
// this file gives them the car view, a rear camera and the same live reload,
// and switches on the game's unused exhaust "parts string" hookup so data
// packs can show/hide exhaust sections per exhaust level (stage 2).
//
//   1. Camera. 500 ms after the selection changes, the shop moves the car
//      camera to the preset "Upgrades_" + suffix (UICarTargetCameraPresets.xml).
//      The suffix is chosen in sub_82605440, which gives brakes none and gives
//      exhaust the engine camera. We give PartEnum 4 (brakes) the suffix
//      "Brakes" and PartEnum 18 (exhaust) the suffix "Exhaust"
//      -> presets Upgrades_Brakes / Upgrades_Exhaust
//      (tools/ui/add_upgrade_brakes_camera.py, add_upgrade_exhaust_camera.py).
//      Hook BrakePreviewCameraSuffix, 0x82605714 (r4 = suffix, r31 = PartEnum).
//
//   2. Refresh. The shop update (sub_82612AD0) rebuilds its car when byte
//      screen+940 is set: sub_824FF838 -> ... -> sub_824DEA38, which reloads
//      the model when the 180-byte installed-parts block it is given differs
//      from the one it last built. Every shop frame we
//        * reload after a purchase changed the brakes, rim sizes or exhaust,
//        * on the brake / exhaust page, reload with the highlighted part
//          (screen+860) substituted into a copy of the parts block (preview),
//          and back to the real parts when the highlight leaves the page.
//   3. Car view. Part pages have up to four views (car, text, dyno, stats);
//      the game enables the car view for springs, body parts etc. but not
//      for brakes or engine parts. Hook BrakePreviewCarView, 0x82614308
//      (r4 = car-view flag), for PartEnum 4 and 18.
//   4. Exhaust parts strings. sub_828B4080 resolves each installed part's
//      PartsStringID (List_PartsStrings: comma list of section names, "-name"
//      hides a section) only for parts whose byte +13 in the part table
//      (0x82D8A2B8, 16 bytes per PartEnum) is set. Exhaust's is 0, although
//      List_UpgradeEngineExhaust has a PartsStringID column (all 0 in the
//      stock DB, so turning it on changes nothing until a pack sets them).
//      Hook ExhaustPartsFlag, 0x828B4080 (function entry) sets 0x82D8A3E5 = 1.
//      Note: byte +13 is also read by shop UI code (sub_8289E268 callers:
//      sub_825E5AC0/5BE8/5D08, sub_82605B08, sub_8262F2A8), so this may change
//      how the shop treats the exhaust page. FM4_EXHAUST_PARTS=0 turns it off.
//
//      Hooks: BrakePreviewShopFrame 0x82612B08 (r31 = shop screen),
//             BrakePreviewPartsArg  0x824FF9A4 (r25 = parts block for SetCar),
//             BrakePreviewPageEnter 0x826140B0 (r27+12 = PartEnum of the page).
//
// Current car: G = *(0x82DC3828); car Id = G+32; installed parts = G+72,
// 45 x u32 part Ids, slot = PartEnum + 2 (brakes 6, rim size front 12, rear
// 13, exhaust 20). Brake/rim part Ids are CarId * 1000 + level; exhaust part
// Ids are EngineID * 1000 + level (List_UpgradeEngineExhaust, levels 0-3).
// Highlighted part of the open part list: u32 at shop screen+860
// (found by the 2026-10-12 diagnostic run; assumed the same on the exhaust
// page, the log line "previewing" confirms it).
//
// FM4_BRAKE_PREVIEW=0 disables everything here.
// FM4_EXHAUST_PREVIEW=0 disables only the exhaust parts (camera, car view,
// live reload, parts-string flag).
// ---------------------------------------------------------------------------
#include <rex/ppc.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint32_t kGarageGlobal = 0x82DC3828;
constexpr uint32_t kCarIdOffset = 32;
constexpr uint32_t kPartsOffset = 72;
constexpr uint32_t kPartsBytes = 180;
constexpr uint32_t kSlotBrakes = 6;
constexpr uint32_t kSlotRimSizeFront = 12;
constexpr uint32_t kSlotRimSizeRear = 13;
constexpr uint32_t kSlotExhaust = 20;
constexpr uint32_t kPartEnumBrakes = 4;
constexpr uint32_t kPartEnumExhaust = 18;
constexpr uint32_t kBrakesSuffix = 0x82017374;   // "Brakes" in .rdata
constexpr uint32_t kExhaustSuffix = 0x820172B4;  // "Exhaust" in .rdata
constexpr uint32_t kExhaustPartsFlag = 0x82D8A3E5;  // part table entry 18, byte +13
constexpr uint32_t kScreenCarDirty = 940;
constexpr uint32_t kScreenHighlightedPart = 860;

bool EnvOn(const char* name) {
  const char* v = std::getenv(name);
  return !(v && v[0] == '0');
}
bool Enabled() {
  static const bool on = EnvOn("FM4_BRAKE_PREVIEW");
  return on;
}
bool ExhaustEnabled() {
  static const bool on = Enabled() && EnvOn("FM4_EXHAUST_PREVIEW");
  return on;
}
bool ExhaustPartsEnabled() {
  static const bool on = ExhaustEnabled() && EnvOn("FM4_EXHAUST_PARTS");
  return on;
}

rex::memory::Memory* Mem() { return REX_KERNEL_MEMORY(); }

// True when [address, address + size) is committed, readable guest memory.
bool Readable(uint32_t address, uint32_t size) {
  if (address < 0x10000 || size == 0 || address + size < address) return false;
  auto* mem = Mem();
  uint32_t a = address;
  const uint32_t end = address + size;
  while (a < end) {
    auto* heap = mem->LookupHeap(a);
    if (!heap) return false;
    rex::memory::HeapAllocationInfo info{};
    if (!heap->QueryRegionInfo(a, &info)) return false;
    if (!(info.state & rex::memory::kMemoryAllocationCommit)) return false;
    if (!(info.protect & rex::memory::kMemoryProtectRead)) return false;
    const uint32_t region_end = info.base_address + info.region_size;
    if (region_end <= a) return false;
    a = region_end;
  }
  return true;
}

uint32_t Load32(uint32_t address) {
  auto* p = Mem()->TranslateVirtual<const uint8_t*>(address);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
void Store32(uint32_t address, uint32_t value) {
  auto* p = Mem()->TranslateVirtual<uint8_t*>(address);
  p[0] = uint8_t(value >> 24); p[1] = uint8_t(value >> 16); p[2] = uint8_t(value >> 8); p[3] = uint8_t(value);
}

bool IsPartOf(uint32_t value, uint32_t car) { return car && value / 1000 == car && value % 1000 < 20; }

// Exhaust part Ids are EngineID * 1000 + level (0-3). A highlighted exhaust is
// valid when it belongs to the same engine as the installed one.
bool IsExhaustLike(uint32_t value, uint32_t installed) {
  return value >= 1000 && installed >= 1000 && value / 1000 == installed / 1000 && value % 1000 <= 3;
}

// Returns true when the string at `address` is `text` (cached per call site
// through `state`: 0 = unchecked, 1 = ok, -1 = mismatch, warned once).
bool CheckString(int& state, uint32_t address, const char* text, const char* what) {
  if (!state) {
    const uint32_t n = uint32_t(std::strlen(text)) + 1;
    state = (Readable(address, n) && std::memcmp(Mem()->TranslateVirtual<const char*>(address), text, n) == 0) ? 1 : -1;
    if (state < 0) REXLOG_WARN("Upgrade preview: \"{}\" string not found at {:08X}, {} camera unchanged", text, address, what);
  }
  return state > 0;
}

struct Shown {  // what the shop car was last built with
  uint32_t car = 0, brakes = 0, rim_front = 0, rim_rear = 0, exhaust = 0;
  bool operator!=(const Shown& o) const {
    return car != o.car || brakes != o.brakes || rim_front != o.rim_front || rim_rear != o.rim_rear ||
           exhaust != o.exhaust;
  }
};

// A part to substitute into the next SetCar parts block.
struct Preview {
  uint32_t slot = 0, id = 0;  // id 0 = none
};

// Rim picker (rim_picker.cpp): latest shop values, written by the game thread
// in BrakePreviewShopFrame, read by the overlay on the UI thread.
std::atomic<uint32_t> g_snap_page{~0u}, g_snap_hl{0}, g_snap_fit{0}, g_snap_car{0};
std::atomic<int64_t> g_snap_ms{0};
int64_t SnapNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Game-thread state.
uint32_t g_screen = 0;
bool g_layout_ok = false;
bool g_exhaust_ok = false;      // exhaust slot holds a plausible exhaust Id
Shown g_real;                   // installed parts last seen
Shown g_shown;                  // parts the shop car was last built with
uint32_t g_page_part = ~0u;     // PartEnum of the part page last entered
Preview g_armed[2];             // parts to substitute in the next SetCar
uint32_t g_parts_copy = 0;      // guest buffer for the substituted parts block
bool g_force_reload = false;    // make the next SetCar rebuild the model
}  // namespace

// 0x82605714 in sub_82605440: r4 = camera preset suffix about to be stored for
// an upgrade type, r31 = its PartEnum.
void BrakePreviewCameraSuffix(PPCRegister& r4, PPCRegister& r31) {
  if (!Enabled()) return;
  if (r31.u32 == kPartEnumBrakes) {
    static int checked = 0;
    if (CheckString(checked, kBrakesSuffix, "Brakes", "brake")) r4.u64 = kBrakesSuffix;
  } else if (r31.u32 == kPartEnumExhaust && ExhaustEnabled()) {
    static int checked = 0;
    if (CheckString(checked, kExhaustSuffix, "Exhaust", "exhaust")) r4.u64 = kExhaustSuffix;
  }
}

// 0x826140B0 in sub_82613FD0 (a part page of the shop is set up): r27+12 = PartEnum.
void BrakePreviewPageEnter(PPCRegister& r27) {
  if (!Enabled() || !Readable(r27.u32 + 12, 4)) return;
  const uint32_t part = Load32(r27.u32 + 12);
  if (part != g_page_part) REXLOG_INFO("Brake preview: shop page PartEnum {}", part);
  g_page_part = part;
}

// 0x82614308 in sub_82613FD0, just before the part page's view flags are
// passed to vtable +0x38 (sub_825F8A68): r4 = "car view" flag (stored at
// screen+880; the other views are text, dyno and stats). The game turns the
// car view on for springs, tyre compound, rear wing, rim sizes and body/aero
// parts (PartEnum 5, 8-11, 34-41), not for brakes or exhaust. Turn it on for
// both. The exhaust page keeps its dyno view flag; if the shop only shows one
// view, the car view wins (to check in game).
void BrakePreviewCarView(PPCRegister& r4, PPCRegister& r27) {
  if (!Enabled() || !Readable(r27.u32 + 12, 4)) return;
  const uint32_t part = Load32(r27.u32 + 12);
  if (part == kPartEnumBrakes || (part == kPartEnumExhaust && ExhaustEnabled())) r4.u64 = 1;
}

// 0x82612B08 in the shop update (sub_82612AD0), r31 = shop screen. Runs every
// frame while the shop is open and its car manager is ready, just before the
// game tests the refresh byte screen+940.
void BrakePreviewShopFrame(PPCRegister& r31) {
  if (!Enabled()) return;
  const uint32_t screen = r31.u32;
  if (!screen || !Readable(kGarageGlobal, 4)) return;
  const uint32_t g = Load32(kGarageGlobal);
  if (!g || !Readable(g + kCarIdOffset, kPartsOffset + kPartsBytes - kCarIdOffset)) return;
  if (!Readable(screen + kScreenHighlightedPart, 4) || !Readable(screen + kScreenCarDirty, 1)) return;

  const uint32_t parts = g + kPartsOffset;
  g_snap_hl.store(Load32(screen + kScreenHighlightedPart));
  g_snap_fit.store(Load32(parts + 4 * 1));  // slot 1 = fitted rim style (-1 = stock)
  g_snap_car.store(Load32(g + kCarIdOffset));
  g_snap_page.store(g_page_part);
  g_snap_ms.store(SnapNowMs());
  Shown real;
  real.car = Load32(g + kCarIdOffset);
  real.brakes = Load32(parts + 4 * kSlotBrakes);
  real.rim_front = Load32(parts + 4 * kSlotRimSizeFront);
  real.rim_rear = Load32(parts + 4 * kSlotRimSizeRear);
  real.exhaust = ExhaustEnabled() ? Load32(parts + 4 * kSlotExhaust) : 0;

  if (screen != g_screen || real.car != g_real.car) {
    // New shop visit (or another car): the game builds the shop car with the
    // real parts on this first frame.
    g_screen = screen;
    g_real = g_shown = real;
    g_page_part = ~0u;
    g_armed[0] = g_armed[1] = Preview{};
    g_layout_ok = IsPartOf(real.brakes, real.car);
    g_exhaust_ok = ExhaustEnabled() && IsExhaustLike(real.exhaust, real.exhaust);
    REXLOG_INFO("Brake preview: shop opened, car {} brakes {} rims {}/{} exhaust {}{}{}", real.car, real.brakes,
                real.rim_front, real.rim_rear, real.exhaust, g_layout_ok ? "" : " (unexpected parts layout, disabled)",
                (!ExhaustEnabled() || g_exhaust_ok) ? "" : " (unexpected exhaust Id, exhaust preview off)");
    return;
  }
  if (!g_layout_ok) return;

  // What should be on the car now: the real parts, or the highlighted part
  // while the brake / exhaust page is open.
  Shown want = real;
  const uint32_t highlighted = Load32(screen + kScreenHighlightedPart);
  if (g_page_part == kPartEnumBrakes && IsPartOf(highlighted, real.car)) want.brakes = highlighted;
  if (g_exhaust_ok && g_page_part == kPartEnumExhaust && IsExhaustLike(highlighted, real.exhaust))
    want.exhaust = highlighted;

  g_real = real;
  if (want != g_shown) {
    const bool brake_preview = want.brakes != real.brakes;
    const bool exhaust_preview = want.exhaust != real.exhaust;
    REXLOG_INFO("Brake preview: shop car brakes {} rims {}/{} exhaust {} ({})", want.brakes, want.rim_front,
                want.rim_rear, want.exhaust,
                (brake_preview || exhaust_preview) ? "previewing" : "installed parts");
    // Brakes and rim sizes need a full render-model reload (brake meshes load
    // only with the model). Exhaust only needs the game's normal "parts
    // changed" update (keep = 1), the same path that shows the roll cage live:
    // sub_824DB380 rebuilds the parts-string show/hide lists and hands them to
    // the render model. A forced full reload may skip that step.
    const bool needs_full_reload = want.brakes != g_shown.brakes || want.rim_front != g_shown.rim_front ||
                                   want.rim_rear != g_shown.rim_rear || want.car != g_shown.car;
    g_shown = want;
    g_armed[0] = brake_preview ? Preview{kSlotBrakes, want.brakes} : Preview{};
    g_armed[1] = exhaust_preview ? Preview{kSlotExhaust, want.exhaust} : Preview{};
    g_force_reload = needs_full_reload;
    *Mem()->TranslateVirtual<uint8_t*>(screen + kScreenCarDirty) = 1;
  }
}

// 0x824FF9A4 in sub_824FF838 (show the current car), just before the parts
// block is passed on to SetCar: r25 = installed parts. When a preview is
// armed, pass a copy with the highlighted brake / exhaust instead.
void BrakePreviewPartsArg(PPCRegister& r25) {
  if (!Enabled()) return;
  const bool armed = g_armed[0].id || g_armed[1].id;
  if (Readable(r25.u32, kPartsBytes))
    REXLOG_INFO("Brake preview: show-car call, parts brakes {} exhaust {}{}", Load32(r25.u32 + 4 * kSlotBrakes),
                Load32(r25.u32 + 4 * kSlotExhaust), armed ? " (preview armed)" : "");
  if (!armed) return;
  const Preview previews[2] = {g_armed[0], g_armed[1]};
  g_armed[0] = g_armed[1] = Preview{};
  if (!Readable(r25.u32, kPartsBytes)) return;
  if (!g_parts_copy) g_parts_copy = REX_KERNEL_MEMORY()->SystemHeapAlloc(kPartsBytes);
  if (!g_parts_copy) return;
  std::memcpy(Mem()->TranslateVirtual<uint8_t*>(g_parts_copy), Mem()->TranslateVirtual<const uint8_t*>(r25.u32),
              kPartsBytes);
  for (const Preview& p : previews)
    if (p.id) Store32(g_parts_copy + 4 * p.slot, p.id);
  r25.u64 = g_parts_copy;
}

// 0x823D4C40, entry of the car presentation builder (diagnostic): r5 = car Id,
// r7 = parts block. Logs every model build so we can see whether the shop's
// refresh actually rebuilds the car.
void BrakePreviewModelBuild(PPCRegister& r5, PPCRegister& r7) {
  if (!Enabled()) return;
  const uint32_t brakes = Readable(r7.u32 + 4 * kSlotBrakes, 4) ? Load32(r7.u32 + 4 * kSlotBrakes) : 0;
  const uint32_t exhaust = Readable(r7.u32 + 4 * kSlotExhaust, 4) ? Load32(r7.u32 + 4 * kSlotExhaust) : 0;
  REXLOG_INFO("Brake preview: car model build, car {} brakes {} exhaust {}", r5.u32, brakes, exhaust);
}

// 0x824DEA80 in SetCar (sub_824DEA38), right after it compared the new parts
// block with the one it last built (r3 = 1 when they differ); r22 = its
// "keep the loaded model" argument (the shop passes 1).
//
// Test 2026-10-12: the presentation was rebuilt with each highlighted brake,
// but the car on screen kept its brakes. The brake models are loaded only when
// the car's render model is loaded (sub_829073C0 via the load worker
// sub_82917828); with "keep" = 1, SetCar updates the existing render model,
// which swaps wheels but never reloads brakes. With "keep" = 0 it first resets
// the render model (sub_824DD838) and loads it again, brakes included. So for
// our refreshes we report "different" and pass keep = 0, once. (Exhaust uses
// the same full reload, so section show/hide from parts strings is re-applied.)
void BrakePreviewForceReload(PPCRegister& r3, PPCRegister& r22) {
  if (!Enabled() || !g_force_reload) return;
  g_force_reload = false;
  REXLOG_INFO("Brake preview: reloading the shop car's render model");
  r3.u64 = 1;
  r22.u64 = 0;
}

// Called by staggered_wheels.cpp: rebuild the shop car (full model reload) on
// the next shop frame, e.g. after the player picked another rear rim.
// Rim picker overlay (rim_picker.cpp): false until the shop has run a frame.
bool UpgradeShopSnapshot(uint32_t* page_part, uint32_t* highlighted, uint32_t* fitted_wheel, uint32_t* car,
                         int64_t* age_ms) {
  const int64_t t = g_snap_ms.load();
  if (!t) return false;
  *page_part = g_snap_page.load();
  *highlighted = g_snap_hl.load();
  *fitted_wheel = g_snap_fit.load();
  *car = g_snap_car.load();
  *age_ms = SnapNowMs() - t;
  return true;
}

void UpgradePreviewRequestReload() {
  if (!Enabled() || !g_screen || !Readable(g_screen + kScreenCarDirty, 1)) return;
  g_force_reload = true;
  *Mem()->TranslateVirtual<uint8_t*>(g_screen + kScreenCarDirty) = 1;
}

// 0x828B4080, entry of the parts-string resolver (sub_828B4080). Its loop over
// PartEnum 0-42 only resolves PartsStringID for parts whose part-table byte
// +13 is set. Turn it on for exhaust (entry 18) before the loop runs, so
// List_UpgradeEngineExhaust.PartsStringID can show/hide exhaust sections.
// r3 = the parts block the lists are built from (+8 = slot 0 of the u32 part
// Ids, so the exhaust Id is at r3 + 8 + 4 * 18 = r3 + 80). Callers:
// sub_823B84A0 (car loader thread, lists stored for the model load) and
// sub_824DB380 (SetCar "parts changed" update, lists passed to the render model).
void ExhaustPartsFlag(PPCRegister& r3) {
  if (!ExhaustPartsEnabled()) return;
  static bool done = false;
  if (!done && Readable(kExhaustPartsFlag, 1)) {
    auto* flag = Mem()->TranslateVirtual<uint8_t*>(kExhaustPartsFlag);
    if (*flag != 1) {
      *flag = 1;
      REXLOG_INFO("Exhaust parts: enabled parts strings for exhaust (part table byte {:08X})", kExhaustPartsFlag);
    }
    done = true;
  }
  if (Readable(r3.u32 + 80, 4))
    REXLOG_INFO("Exhaust diag: parts-string lists built, exhaust part {}", Load32(r3.u32 + 80));
}

// 0x828B4E78 in sub_828B4080 ("mr r5,r3" right after GetInt("PartsStringID")):
// r3 = PartsStringID, r27 = PartEnum, r23 = address of this part's Id.
// A 0 for the exhaust means the DB patch did not apply.
void ExhaustDiagStringId(PPCRegister& r3, PPCRegister& r27, PPCRegister& r23) {
  if (!ExhaustPartsEnabled() || r27.u32 != kPartEnumExhaust) return;
  const uint32_t part = Readable(r23.u32, 4) ? Load32(r23.u32) : 0;
  REXLOG_INFO("Exhaust diag: exhaust part {} -> PartsStringID {}", part, r3.u32);
}

// 0x828B4F48 in sub_828B4080 ("lbz r11,0(r11)" in the token loop): r11 = the
// token's characters, r27 = PartEnum. Tokens starting with '-' go to the hide list.
void ExhaustDiagToken(PPCRegister& r11, PPCRegister& r27) {
  if (!ExhaustPartsEnabled() || r27.u32 != kPartEnumExhaust || !Readable(r11.u32, 1)) return;
  char text[64] = {};
  for (uint32_t i = 0; i + 1 < sizeof(text) && Readable(r11.u32 + i, 1); ++i) {
    const char c = *Mem()->TranslateVirtual<const char*>(r11.u32 + i);
    if (!c) break;
    text[i] = c;
  }
  REXLOG_INFO("Exhaust diag: token \"{}\"", text);
}

// ---------------------------------------------------------------------------
// Exhaust kit override (sub_82911BD0, the function that applies the
// parts-string show/hide lists to a car model).
//
// After applying the lists, sub_82911BD0 finds the visible rear bumper
// (bumperR<x>), hides every "exhaust*" section (0x82911F9C) and re-shows the
// exhaust matching the bumper's letter x: exhaustL<x>/exhaustR<x> (fallback
// exhaustLa/exhaustL), else exhaust<x>/exhausta/exhaust. That wipes out any
// exhaust change made by the lists. The letter is a std::string at r1+160
// (inline buffer, length at r1+176); length != 1 skips the re-show (all
// exhausts stay hidden).
//
// Hook at 0x82911F90 (just before the hide-all), r22 = hide list, r26 = show
// list (vectors of 28-byte std::strings: +0 inline buffer or heap pointer
// when capacity (+20) >= 16, +16 length):
//   * a show token exhaust[L|R]<b..z> (only parts strings add these; the
//     default list only has the 'a' and plain names) -> use that letter;
//   * else a hide token "exhaust*", or exhaust[L|R]<bumper letter>
//     -> show no exhaust (length 0).
// ---------------------------------------------------------------------------
namespace {
bool ReadGuestString(uint32_t s, char* out, size_t cap) {
  if (!Readable(s, 24)) return false;
  const uint32_t len = Load32(s + 16), capacity = Load32(s + 20);
  if (len >= cap) return false;
  const uint32_t data = capacity >= 16 ? Load32(s) : s;
  if (len && !Readable(data, len)) return false;
  std::memcpy(out, Mem()->TranslateVirtual<const char*>(data), len);
  out[len] = 0;
  return true;
}

// "exhaust", optional L/R, then exactly one lowercase letter -> that letter.
char ExhaustTokenLetter(const char* t) {
  if (std::strncmp(t, "exhaust", 7) != 0 && std::strncmp(t, "Exhaust", 7) != 0) return 0;
  const char* p = t + 7;
  if (*p == 'L' || *p == 'R') ++p;
  if (p[0] >= 'a' && p[0] <= 'z' && p[1] == 0) return p[0];
  return 0;
}

template <typename F>
void ForEachToken(uint32_t vec, F&& f) {
  if (!Readable(vec, 8)) return;
  const uint32_t begin = Load32(vec), end = Load32(vec + 4);
  if (end < begin || (end - begin) / 28 > 256) return;
  char buf[64];
  for (uint32_t s = begin; s + 28 <= end; s += 28)
    if (ReadGuestString(s, buf, sizeof(buf))) f(buf);
}
}  // namespace

void ExhaustKitOverride(PPCRegister& r1, PPCRegister& r22, PPCRegister& r26) {
  if (!ExhaustPartsEnabled()) return;
  const uint32_t letter_str = r1.u32 + 160;
  if (!Readable(letter_str, 24) || Load32(letter_str + 16) != 1 || Load32(letter_str + 20) >= 16) return;
  const char bumper = *Mem()->TranslateVirtual<const char*>(letter_str);

  char show = 0;
  ForEachToken(r26.u32, [&](const char* t) {
    const char c = ExhaustTokenLetter(t);
    if (c && c != 'a') show = c;
  });
  bool hide = false;
  ForEachToken(r22.u32, [&](const char* t) {
    if (std::strcmp(t, "exhaust*") == 0 || ExhaustTokenLetter(t) == bumper) hide = true;
  });

  if (show) {
    *Mem()->TranslateVirtual<char*>(letter_str) = show;
    REXLOG_INFO("Exhaust kit: bumper letter '{}' -> exhaust letter '{}'", bumper, show);
  } else if (hide) {
    Store32(letter_str + 16, 0);
    REXLOG_INFO("Exhaust kit: bumper letter '{}' -> no exhaust", bumper);
  }
}
