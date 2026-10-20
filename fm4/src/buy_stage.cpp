// ---------------------------------------------------------------------------
// Buy Cars on the main-menu "stage" (Forza-4-Evolution).
// Research: claude/buy-cars-3d-render-research-2026-10-12.md
//           claude/buy-cars-homespace-stage-research-2026-10-12.md
//
// Every front-end screen sets a "mode" when it opens (sub_8251BAA8 -> sub_824FFA48),
// and the mode picks the render scenario (sub_82534EE0):
//   0, 4-7 = UI_Homespace (the stage behind the main menu)
//   1, 2, 8, 9, 10, 13, 14 = UI_Car, 3, 11, 12 = UI_NoCar, 15 = UI_Showroom.
// The Buy Cars car list (ForzaUI::CCarSelect, setup function sub_824EF1E8) sets
// mode 3 (no car, flat background, 2D thumbnail).
//
// Stage 1 (BuyStageMode): at 0x824EF21C replace the mode with
//   FM4_BUYCARS_STAGE_MODE (default 6) so the list opens on the stage.
//   (The add-on pack tools/dlc/make_dealer_thumbs_pack.py hides the flat 2D
//   picture so the stage shows through the picture box.)
//
// Stage 2 (this file): make the stage show the HIGHLIGHTED car at a 3/4 angle.
//   * BuyStageHighlight (0x824FC6D0, CCarSelect focus handler sub_824FC670,
//     r28 = highlighted car Id) remembers the highlighted car.
//   * BuyStageFrame (0x822F7884, main loop, once per frame, between frames so
//     guest calls are safe) calls the game's own "show stock car" routine
//     sub_824CDC18(carId, 0, 0) (the one the buy-car colour screen path uses:
//     builds a stock parts block + default paint, writes G+32 / G+36 / G+72 and
//     calls ShowCar), debounced while scrolling. It also switches the stage view
//     to the car-target camera (type 1) and applies the preset "BuyCarSel"
//     (tools/ui/add_buycar_camera.py adds it to UICarTargetCameraPresets.xml).
//   * BuyStageModeSet (0x824FFA48, mode setter entry = another screen opened)
//     puts everything back: the current car's id, parts block and livery handle
//     in G (only if nothing else changed G+32 meanwhile) and the old camera type.
//
// CCarSelect is shared (buy list, sell list, gift popup...), so the screen's
// flow value G+16 is logged and the setting fm4_buycars_flows (comma list; empty =
// all but 9, the gift popup) restricts it. All options are settings (cvars) shown in
// the F4 overlay under Forza Motorsport 4 > Buy Cars: fm4_buycars_stage (whole
// feature), _car (swap the stage car), _camera (3/4 preset), _delay_ms, _paint,
// _stage_mode, _flows. Use "Save to config" in the overlay to keep them.
//
// Guest addresses (see claude/buy-cars-homespace-stage-research-2026-10-12.md):
//   G = *(0x82DC3828): G+32 car Id, G+36 livery handle (32 bytes, [+28] = ref
//   counted object, release = vtable +8), G+72 parts (180 bytes), G+16 flow.
//   0x824CDC18 (carId, paintByte, flag)   show stock car
//   0x824F1D78 (out, srcHandle)           handle copy; 0x823ECC48 (dst, &src) assign
//   0x8251BA18 ()                         stage scene B; P = [B+3012] stage view,
//                                         [P+140] camera, [P+144] camera type
//   0x82652368 (P, type, force)           switch camera type (1 = car target)
//   0x82389560 (obj, 0, tdViewCamera, tdCarTarget, 0)  dynamic_cast to car target
//   0x824D7A20 (cam, "name", snap, useGeneric)         set camera preset
// ---------------------------------------------------------------------------
#include <rex/cvar.h>
#include <rex/ppc.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

// Settings (F4 overlay > Forza Motorsport 4 > Buy Cars; "Save to config" keeps them).
REXCVAR_DEFINE_BOOL(fm4_buycars_stage, true, "Forza Motorsport 4/Buy Cars",
                    "Open the Buy Cars list on the 3D stage (off = the classic flat list). Takes effect "
                    "the next time the list opens");
REXCVAR_DEFINE_BOOL(fm4_buycars_car, true, "Forza Motorsport 4/Buy Cars",
                    "Show the highlighted car on the stage (off = the stage keeps showing your current car)");
REXCVAR_DEFINE_BOOL(fm4_buycars_camera, true, "Forza Motorsport 4/Buy Cars",
                    "Use the 3/4 view camera (preset BuyCarSel) on the Buy Cars stage");
REXCVAR_DEFINE_INT32(fm4_buycars_delay_ms, 350, "Forza Motorsport 4/Buy Cars",
                     "Milliseconds the highlight must rest on a car before the stage car is swapped "
                     "(each swap freezes the game for about 0.1 s)")
    .range(0, 3000);
REXCVAR_DEFINE_INT32(fm4_buycars_paint, 1, "Forza Motorsport 4/Buy Cars",
                     "Colour number given to the stage car (1 = the car's first colour; 0 renders black)")
    .range(0, 255);
REXCVAR_DEFINE_INT32(fm4_buycars_stage_mode, 6, "Forza Motorsport 4/Buy Cars",
                     "Stage screen mode for the Buy Cars list (0 and 4-7 are the homespace stage)")
    .range(0, 15);
// Camera of the 3/4 view (car-relative: +X = right side, +Z = front, Y up, metres).
// Changes apply live while the list is open (the preset's numbers are patched in memory).
REXCVAR_DEFINE_DOUBLE(fm4_buycars_cam_x, 3.5, "Forza Motorsport 4/Buy Cars/Camera",
                      "Camera position X (+ = right side of the car)").range(-12.0, 12.0);
REXCVAR_DEFINE_DOUBLE(fm4_buycars_cam_y, 0.35, "Forza Motorsport 4/Buy Cars/Camera",
                      "Camera position Y (height; the car's origin is about half-way up the body)")
    .range(-3.0, 6.0);
REXCVAR_DEFINE_DOUBLE(fm4_buycars_cam_z, 3.9, "Forza Motorsport 4/Buy Cars/Camera",
                      "Camera position Z (+ = in front of the car, - = behind)").range(-12.0, 12.0);
REXCVAR_DEFINE_DOUBLE(fm4_buycars_look_x, 0.0, "Forza Motorsport 4/Buy Cars/Camera",
                      "Look-at point X").range(-6.0, 6.0);
REXCVAR_DEFINE_DOUBLE(fm4_buycars_look_y, -0.15, "Forza Motorsport 4/Buy Cars/Camera",
                      "Look-at point Y (lower = car sits higher in the picture)").range(-3.0, 3.0);
REXCVAR_DEFINE_DOUBLE(fm4_buycars_look_z, 0.25, "Forza Motorsport 4/Buy Cars/Camera",
                      "Look-at point Z").range(-6.0, 6.0);
REXCVAR_DEFINE_DOUBLE(fm4_buycars_fov, 28.0, "Forza Motorsport 4/Buy Cars/Camera",
                      "Camera field of view in degrees (vertical; smaller = closer)").range(5.0, 90.0);
REXCVAR_DEFINE_STRING(fm4_buycars_flows, "", "Forza Motorsport 4/Buy Cars",
                      "Comma list of car-list flow numbers that use the stage (empty = all except 9, "
                      "the gift popup)");
REXCVAR_DEFINE_BOOL(fm4_buycars_color_camera, true, "Forza Motorsport 4/Buy Cars",
                    "The colour screen (shown after choosing a car) uses the same 3/4 camera as the list "
                    "instead of the rear showcase view");
REXCVAR_DEFINE_INT32(fm4_buycars_direct, 0, "Forza Motorsport 4/Buy Cars/Direct buy",
                     "EXPERIMENTAL. 0 = A on a car opens the colour screen (normal). 1 = A buys the car at once "
                     "in the default colour (no showcase, no question asked!). 2 = same, but only while the "
                     "Shift key is held; plain A still opens the colour screen")
    .range(0, 2);
REXCVAR_DEFINE_STRING(fm4_buycars_direct_flows, "", "Forza Motorsport 4/Buy Cars/Direct buy",
                      "Comma list of list flow numbers where direct buy is allowed (empty = nowhere). The "
                      "flow of each list is written to fm4.log as 'Buy stage: car list screen ... flow N'");
REXCVAR_DEFINE_INT32(fm4_buycars_direct_listtype, -1, "Forza Motorsport 4/Buy Cars/Direct buy",
                     "Only allow direct buy when the list's type field equals this (-1 = ignore); logged as "
                     "'type' at list open, to tell the buy list from the sell list")
    .range(-1, 1000);

// ---- one-click resets (F4 overlay buttons) ----
namespace {
const char* const kBuyCarsSettings[] = {
    "fm4_buycars_stage", "fm4_buycars_car", "fm4_buycars_camera", "fm4_buycars_delay_ms", "fm4_buycars_paint",
    "fm4_buycars_stage_mode", "fm4_buycars_cam_x", "fm4_buycars_cam_y", "fm4_buycars_cam_z",
    "fm4_buycars_look_x", "fm4_buycars_look_y", "fm4_buycars_look_z", "fm4_buycars_fov", "fm4_buycars_flows",
    "fm4_buycars_color_camera", "fm4_buycars_direct", "fm4_buycars_direct_flows",
    "fm4_buycars_direct_listtype"};
void ResetBuyCarsSettings() {
  for (const char* n : kBuyCarsSettings) rex::cvar::ResetToDefault(n);
}
}  // namespace
REXCVAR_DEFINE_COMMAND(fm4_buycars_reset_defaults, []() { ResetBuyCarsSettings(); },
                       "Forza Motorsport 4/Buy Cars",
                       "RESET every Buy Cars setting to its default (then use 'Save to config' to keep it)");
REXCVAR_DEFINE_COMMAND(fm4_buycars_classic, []() {
  ResetBuyCarsSettings();
  REXCVAR_SET(fm4_buycars_stage, false);
  REXCVAR_SET(fm4_buycars_color_camera, false);
}, "Forza Motorsport 4/Buy Cars",
                       "CLASSIC Buy Cars: turn all 3D stage changes off (also remove the thumbnail pack to get "
                       "the old pictures back). Takes effect the next time the list opens");

// Mods menu FEATURES tab toggle (mods_menu.cpp): the whole stage on or off in one go.
bool BuyStageUserEnabled() { return REXCVAR_GET(fm4_buycars_stage); }
void BuyStageSetUserEnabled(bool on) {
  REXCVAR_SET(fm4_buycars_stage, on);
  REXCVAR_SET(fm4_buycars_color_camera, on);
}

namespace {
constexpr uint32_t kGarageGlobal = 0x82DC3828;
constexpr uint32_t kFnShowStockCar = 0x824CDC18;
constexpr uint32_t kFnCopyHandle = 0x824F1D78;
constexpr uint32_t kFnAssignHandle = 0x823ECC48;
constexpr uint32_t kFnStageScene = 0x8251BA18;
constexpr uint32_t kFnSetCameraType = 0x82652368;
constexpr uint32_t kFnDynCast = 0x82389560;
constexpr uint32_t kFnSetPreset = 0x824D7A20;
constexpr uint32_t kFnStrCtor = 0x823395D8;      // (str, cstr)
constexpr uint32_t kFnStrDtor = 0x82334380;      // (str, 1, 0)
constexpr uint32_t kFnPresetLookup = 0x824D4070; // (table, &name, ordinal, 0) -> preset
constexpr uint32_t kPresetTableSlot = 0x82DDDB30; // holds the preset table pointer
constexpr uint32_t kTypeViewCamera = 0x82D665A0;
constexpr uint32_t kTypeCarTarget = 0x82D66E18;
constexpr uint32_t kFnBuyCurrent = 0x824FBB98;   // (out, screenLike, 0): buys car B[764] in colour B[768]
constexpr uint32_t kSentinelCarId = 777777;  // list entries at or above are not cars

constexpr uint32_t kStartDelay = 20;   // frames after the list opens before we touch the stage
constexpr uint32_t kCamEarly = 3;     // the camera is applied from this frame on (the car waits for kStartDelay)
constexpr uint32_t kCameraPoll = 30;   // frames between checks that the camera is still ours

// Guest scratch block.
constexpr uint32_t kScratchSize = 0x800;
constexpr uint32_t kOffHandle = 0x000;  // snapshot of the livery handle (32 bytes)
constexpr uint32_t kOffParts = 0x040;   // snapshot of the parts block (180 bytes)
constexpr uint32_t kOffName = 0x100;    // camera preset name
constexpr uint32_t kOffName2 = 0x180;   // "CarColorSel"
constexpr uint32_t kOffOut = 0x3C0;     // result handle of the direct buy
constexpr uint32_t kOffBuy = 0x400;     // zeroed fake screen object for the direct buy (0x400 bytes)
constexpr uint32_t kOffStr = 0x140;     // guest std::string for the preset lookup (32 bytes)

bool Enabled() { return REXCVAR_GET(fm4_buycars_stage); }
bool CarEnabled() { return REXCVAR_GET(fm4_buycars_car); }
bool ColorCameraEnabled() { return REXCVAR_GET(fm4_buycars_color_camera); }
bool CameraEnabled() { return REXCVAR_GET(fm4_buycars_camera); }
uint32_t PaintByte() { return uint32_t(REXCVAR_GET(fm4_buycars_paint)) & 0xFF; }
uint32_t DelayMs() { return uint32_t(REXCVAR_GET(fm4_buycars_delay_ms)); }
uint32_t StageMode() { return uint32_t(REXCVAR_GET(fm4_buycars_stage_mode)); }
uint64_t NowMs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count());
}

// True when `flow` is allowed by the fm4_buycars_flows setting ("" = every flow but 9, the gift popup).
bool FlowInList(const std::string& list, uint32_t flow, bool empty_means) {
  if (list.empty()) return empty_means;
  const char* p = list.c_str();
  while (*p) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(p, &end, 10);
    if (end == p) { ++p; continue; }
    if (v == flow) return true;
    p = end;
  }
  return false;
}
bool FlowAllowed(uint32_t flow) {
  const std::string list = std::string(REXCVAR_GET(fm4_buycars_flows));
  if (list.empty()) return flow != 9;
  return FlowInList(list, flow, true);
}
bool KeyShift() {
#ifdef _WIN32
  return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#else
  return false;
#endif
}

rex::memory::Memory* Mem() { return REX_KERNEL_MEMORY(); }
uint8_t* Base() { return Mem()->virtual_membase(); }
bool Readable(uint32_t a, uint32_t n) { return a >= 0x10000 && a < 0xF0000000u && a + n > a; }
uint32_t Load32(uint32_t a) {
  const uint8_t* p = Mem()->TranslateVirtual<const uint8_t*>(a);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
void Store32(uint32_t a, uint32_t v) {
  uint8_t* p = Mem()->TranslateVirtual<uint8_t*>(a);
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
uint8_t* Ptr(uint32_t a) { return Mem()->TranslateVirtual<uint8_t*>(a); }

// Calls guest code on the current (main game) thread, on the thread's own
// PPCContext (kernel exports reach it through the PCR): context saved, stack
// moved below the hooked frame, function called, context restored. Same
// protocol as dev_keys.cpp (copied so the two files stay independent).
class Guest {
 public:
  Guest(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr) {
    (void)r13; (void)fpscr;
    auto* ts = rex::runtime::ThreadState::Get();
    ctx_ = ts ? ts->context() : nullptr;
    if (!ctx_ || &ctx_->r1 != &r1) {
      REXLOG_WARN("Buy stage: hook is not running on the thread's own context; skipping");
      ctx_ = nullptr;
      ok_ = false;
      return;
    }
    sp_ = (r1.u32 - 0x400) & ~0xFu;
  }
  bool ok() const { return ok_; }

  uint32_t Call(uint32_t target, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0) {
    if (!ok_ || !target) { ok_ = false; return 0; }
    static_assert(sizeof(PPCContext) <= sizeof(saved_));
    std::memcpy(saved_, static_cast<void*>(ctx_), sizeof(PPCContext));
    PPCContext& ctx = *ctx_;
    ctx.r1.u64 = sp_;
    ctx.r3.u64 = a; ctx.r4.u64 = b; ctx.r5.u64 = c; ctx.r6.u64 = d; ctx.r7.u64 = e;
    ctx.lr = 0;  // LR 0 marks "return to this code", like XThread::Execute
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
      REXLOG_WARN("Buy stage: guest call {:08X} could not finish (stuck at {:08X})", target, next);
      ok_ = false;
      return 0;
    }
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

// ---- state (main game thread only) ----
bool g_opening = false;       // our own mode change of the list is in progress
bool g_list_open = false;     // the list we switched to the stage is on screen
bool g_faulted = false;       // a guest call failed: stay out of the way this session
uint32_t g_screen = 0;
uint32_t g_frame = 0;
uint32_t g_pending = 0;       // highlighted car
uint64_t g_pending_ms = 0;    // time of the last change of g_pending
uint32_t g_shown = 0;         // car we last put on the stage (0 = none yet)
bool g_modified = false;      // G holds our car; the snapshot below can restore it
uint32_t g_snap_id = 0;
bool g_cam_dirty = false;
int64_t g_cam_saved = -1;     // camera type before we switched it
uint32_t g_scratch = 0;
uint32_t g_logged = 0;
uint32_t g_skip_logged = 0;
uint32_t g_cam_next = 0;      // first frame at which the camera may be tried again
uint32_t g_cam_fail = 0;
uint32_t g_list_type = 0;     // field this+632 of the list screen, logged to tell buy and sell lists apart
// Colour screen camera (the screen shown after a car is chosen): its preset "CarColorSel" is
// overwritten in memory with our 3/4 camera while that screen is up, then put back.
bool g_color_pending = false;  // the colour screen's own mode change is about to happen
bool g_color_patched = false;
uint32_t g_color_preset = 0;
uint32_t g_color_table = 0;
uint8_t g_color_saved[36];
double g_cam_sig = 0;

bool EnsureScratch() {
  if (g_scratch) return true;
  g_scratch = Mem()->SystemHeapAlloc(kScratchSize);
  if (!g_scratch) return false;
  std::memset(Ptr(g_scratch), 0, kScratchSize);
  std::memcpy(Ptr(g_scratch + kOffName), "BuyCarSel", 10);
  std::memcpy(Ptr(g_scratch + kOffName2), "CarColorSel", 12);
  return true;
}

void StoreFloat(uint32_t a, double v) { Store32(a, std::bit_cast<uint32_t>(float(v))); }

// The camera numbers from the settings as one number, to notice changes.
double CameraSignature() {
  return REXCVAR_GET(fm4_buycars_cam_x) * 1.0 + REXCVAR_GET(fm4_buycars_cam_y) * 3.1 +
         REXCVAR_GET(fm4_buycars_cam_z) * 5.3 + REXCVAR_GET(fm4_buycars_look_x) * 7.7 +
         REXCVAR_GET(fm4_buycars_look_y) * 11.3 + REXCVAR_GET(fm4_buycars_look_z) * 13.9 +
         REXCVAR_GET(fm4_buycars_fov) * 17.1;
}

// Writes the camera settings into the loaded preset "BuyCarSel" (struct layout from the
// preset serializer sub_824BE698: +0/4/8 position, +16/20/24 facing direction, +32 fov).
uint32_t FindPreset(Guest& g, uint32_t name_off) {
  const uint32_t table = Readable(kPresetTableSlot, 4) ? Load32(kPresetTableSlot) : 0;
  if (!Readable(table, 12)) return 0;
  // The lookup (sub_824D4070) walks a sorted array of 96-byte entries at [table+4] .. [table+8]. A table that is
  // still being built made it fault on a garbage pointer (guest AV in sub_824BDF10, Alfa Romeo / F class opens).
  // Only call it when the array looks sane; otherwise report "not found" and let the caller retry later.
  const uint32_t arr_begin = Load32(table + 4), arr_end = Load32(table + 8);
  if (arr_end < arr_begin || (arr_end - arr_begin) % 96 != 0 || (arr_end - arr_begin) / 96 > 4096) return 0;
  if (arr_end != arr_begin && (!Readable(arr_begin, 96) || !Readable(arr_end - 96, 96))) return 0;
  g.Call(kFnStrCtor, g_scratch + kOffStr, g_scratch + name_off);
  const uint32_t preset = g.Call(kFnPresetLookup, table, g_scratch + kOffStr, 0xFFFFFFFFu, 0);
  g.Call(kFnStrDtor, g_scratch + kOffStr, 1, 0);
  return (g.ok() && Readable(preset, 64)) ? preset : 0;
}

// Writes the camera settings into a loaded preset (struct layout from sub_824BE698).
void WritePresetNumbers(uint32_t preset) {
  const double px = REXCVAR_GET(fm4_buycars_cam_x), py = REXCVAR_GET(fm4_buycars_cam_y),
               pz = REXCVAR_GET(fm4_buycars_cam_z);
  double fx = REXCVAR_GET(fm4_buycars_look_x) - px, fy = REXCVAR_GET(fm4_buycars_look_y) - py,
         fz = REXCVAR_GET(fm4_buycars_look_z) - pz;
  double n = std::sqrt(fx * fx + fy * fy + fz * fz);
  if (n < 0.01) { fx = 0; fy = 0; fz = -1; n = 1; }
  StoreFloat(preset + 0, px); StoreFloat(preset + 4, py); StoreFloat(preset + 8, pz);
  StoreFloat(preset + 16, fx / n); StoreFloat(preset + 20, fy / n); StoreFloat(preset + 24, fz / n);
  StoreFloat(preset + 32, REXCVAR_GET(fm4_buycars_fov));
}

// Puts the colour screen's camera preset back (no guest call needed).
void RestoreColorPreset() {
  if (!g_color_patched) return;
  g_color_patched = false;
  if (g_color_preset && Readable(kPresetTableSlot, 4) && Load32(kPresetTableSlot) == g_color_table) {
    std::memcpy(Ptr(g_color_preset), g_color_saved, 36);
    REXLOG_INFO("Buy stage: colour screen camera preset restored");
  }
}

bool PatchPreset(Guest& g) {
  const uint32_t preset = FindPreset(g, kOffName);
  if (!preset) {
    if (g_logged++ < 40) REXLOG_INFO("Buy stage: preset BuyCarSel not found; run tools/ui/add_buycar_camera.py");
    return false;
  }
  WritePresetNumbers(preset);
  return true;
}

// Puts the stage on the car-target camera with our preset. Returns false when
// the stage view is not there (yet).
bool ApplyCamera(Guest& g) {
  const uint32_t B = g.Call(kFnStageScene, 0);
  if (!g.ok() || !Readable(B, 4) || !Readable(B + 3012, 4)) return false;
  const uint32_t P = Load32(B + 3012);
  if (!Readable(P, 160)) return false;
  const uint32_t type = Load32(P + 144);
  if (g_cam_saved < 0) g_cam_saved = type;
  if (type != 1) {
    g.Call(kFnSetCameraType, P, 1, 1);
    if (!g.ok()) return false;
    if (g_logged++ < 40) REXLOG_INFO("Buy stage: camera type {} -> 1", type);
  }
  const uint32_t cam = g.Call(kFnDynCast, Load32(P + 140), 0, kTypeViewCamera, kTypeCarTarget, 0);
  if (!g.ok()) return false;
  if (!cam) {
    if (g_logged++ < 40) REXLOG_INFO("Buy stage: stage camera is not a car-target camera (type {})", Load32(P + 144));
    return false;
  }
  PatchPreset(g);
  g.Call(kFnSetPreset, cam, g_scratch + kOffName, 1, 1);
  return g.ok();
}

// Puts G (current car) back as it was when the list opened.
void RestoreCar(Guest& g) {
  const uint32_t G = Load32(kGarageGlobal);
  if (Readable(G, 0x100) && Load32(G + 32) == g_shown) {
    Store32(G + 32, g_snap_id);
    std::memcpy(Ptr(G + 72), Ptr(g_scratch + kOffParts), 180);
    g.Call(kFnAssignHandle, G + 36, g_scratch + kOffHandle);
    REXLOG_INFO("Buy stage: restored current car {}", g_snap_id);
  } else {
    REXLOG_INFO("Buy stage: current car changed meanwhile, not restoring");
  }
  const uint32_t p = Load32(g_scratch + kOffHandle + 28);
  if (p) g.VCall(p, 8);  // release our copy of the livery handle
  g_modified = false;
}
}  // namespace

// 0x824EF21C in sub_824EF1E8 (CCarSelect setup), just after `li r4,3`:
// r4 = screen mode about to be set, r31 = the screen.
void BuyStageMode(PPCRegister& r4, PPCRegister& r31) {
  if (!Enabled()) return;
  uint32_t flow = 0xFFFFFFFF;
  const uint32_t G = Load32(kGarageGlobal);
  if (Readable(G + 16, 4)) flow = Load32(G + 16);
  const bool allowed = FlowAllowed(flow);
  g_list_type = Readable(r31.u32, 640) ? Load32(r31.u32 + 632) : 0;
  REXLOG_INFO("Buy stage: car list screen {:08X} opening, flow {}, type {}, mode {} -> {}", r31.u32, flow,
              g_list_type, r4.u32, allowed ? StageMode() : r4.u32);
  RestoreColorPreset();  // back from the colour screen
  g_list_open = allowed && r4.u32 == 3;
  g_opening = g_list_open;
  g_screen = r31.u32;
  g_frame = 0;
  g_pending = 0;
  g_pending_ms = NowMs();
  g_shown = 0;
  g_cam_dirty = true;
  g_logged = 0;
  g_skip_logged = 0;
  g_cam_next = 0;
  g_cam_fail = 0;
  if (allowed && r4.u32 == 3) r4.u64 = StageMode();
}

// 0x824FC6D0 in sub_824FC670 (CCarSelect focus handler): r28 = highlighted
// car Id (list entries >= 777777 are not cars), r31 = the screen.
void BuyStageHighlight(PPCRegister& r28, PPCRegister& r31) {
  if (!Enabled() || !g_list_open || r31.u32 != g_screen) return;
  const uint32_t id = r28.u32;
  if (id == 0 || id >= kSentinelCarId) return;
  if (id != g_pending) {
    g_pending = id;
    g_pending_ms = NowMs();
    if (g_logged++ < 40) REXLOG_INFO("Buy stage: highlighted car {}", id);
  }
}

// 0x822F7884, main game loop, once per frame (the dev keys use 0x822F787C).
void BuyStageFrame(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr) {
  if (!Enabled() || !g_list_open || g_faulted) return;
  ++g_frame;
  g_opening = false;  // the list's own mode change is long done (or never reached the setter)
  const bool early = g_frame < kStartDelay;  // the car waits; the camera may start sooner
  if (early && (g_frame < kCamEarly || !CameraEnabled() || !g_cam_dirty)) return;
  const uint32_t G = Load32(kGarageGlobal);
  if (!Readable(G, 0x100)) return;

  const bool car_due = !early && CarEnabled() && g_pending && g_pending != g_shown &&
                       NowMs() - g_pending_ms >= DelayMs() &&
                       !(g_shown == 0 && g_pending == Load32(G + 32));
  if (CameraEnabled()) {
    const double sig = CameraSignature();
    if (sig != g_cam_sig) { g_cam_sig = sig; g_cam_dirty = true; g_cam_fail = 0; }
  }
  if (!early && !car_due && CameraEnabled() && !g_cam_dirty && g_frame % kCameraPoll == 0) g_cam_dirty = true;
  const bool cam_due = CameraEnabled() && g_cam_dirty && g_frame >= g_cam_next;
  if (!early && g_pending && g_pending != g_shown && !car_due && g_skip_logged != g_pending) {
    g_skip_logged = g_pending;
    REXLOG_INFO("Buy stage: car {} waiting (shown {}, current {}, delay {} ms, mode {})", g_pending, g_shown,
                Load32(G + 32), DelayMs(), StageMode());
  }
  if (!car_due && !cam_due) return;
  if (!EnsureScratch()) return;
  Guest g(r1, r13, fpscr);
  if (!g.ok()) { g_faulted = true; return; }

  if (car_due) {
    if (!g_modified) {  // snapshot the real current car before the first swap
      g_snap_id = Load32(G + 32);
      std::memcpy(Ptr(g_scratch + kOffParts), Ptr(G + 72), 180);
      g.Call(kFnCopyHandle, g_scratch + kOffHandle, G + 36);
      g_modified = true;
    }
    Store32(G + 32, 0);  // force the "build parts and show" path
    // paint byte 1 = the car's first colour (0 rendered black; the colour screen path passes 1)
    g.Call(kFnShowStockCar, g_pending, PaintByte(), 0);
    g_shown = g_pending;
    g_cam_dirty = true;
    if (g_logged++ < 40) REXLOG_INFO("Buy stage: showing car {} (current car {})", g_pending, g_snap_id);
  }
  if (g.ok() && g_cam_dirty && CameraEnabled() && g_frame >= g_cam_next) {
    if (ApplyCamera(g)) {
      g_cam_dirty = false;
    } else if (++g_cam_fail >= 60) {
      g_cam_dirty = false;  // give up until the settings change
    } else {
      g_cam_next = g_frame + 5;
    }
  }
  if (!g.ok()) {
    g_faulted = true;
    REXLOG_WARN("Buy stage: a guest call failed; stage tweaks off for this session");
  }
}

// 0x824FFA48 (mode setter, entry): another screen is opening. The list's own
// call is let through; any other call ends our changes.
void BuyStageModeSet(PPCRegister& r4, PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr) {
  if (g_list_open) REXLOG_INFO("Buy stage: mode setter called with mode {} (opening {})", r4.u32, g_opening);
  if (g_color_pending) g_color_pending = false;  // the colour screen's own mode change keeps its patch
  else RestoreColorPreset();
  if (g_opening) { g_opening = false; return; }
  if (!g_list_open && !g_modified && g_cam_saved < 0) return;
  g_list_open = false;
  if (!g_modified && g_cam_saved < 0) return;
  Guest g(r1, r13, fpscr);
  if (!g.ok()) { g_modified = false; g_cam_saved = -1; return; }
  if (g_modified) RestoreCar(g);
  if (g_cam_saved >= 0) {
    const uint32_t B = g.Call(kFnStageScene, 0);
    if (g.ok() && Readable(B, 4) && Readable(B + 3012, 4)) {
      const uint32_t P = Load32(B + 3012);
      if (Readable(P, 160) && Load32(P + 144) != uint32_t(g_cam_saved)) {
        g.Call(kFnSetCameraType, P, uint32_t(g_cam_saved), 1);
        REXLOG_INFO("Buy stage: camera type restored to {}", g_cam_saved);
      }
    }
    g_cam_saved = -1;
  }
}

// 0x824DEA38 (SetCar), after its first instruction: logging only.
void BuyStageSetCar(PPCRegister& r3, PPCRegister& r4, PPCRegister& r12) {
  if (!Enabled() || !g_list_open) return;
  if (g_logged++ < 40) REXLOG_INFO("Buy stage: SetCar car {} view {:08X} from {:08X}", r4.u32, r3.u32, r12.u32);
}

// 0x8250BA70, entry of CCarColorSelect::Init (the colour screen after a car was chosen in a buy
// flow; flows 9-17, 9 = gift). Its camera is the preset "CarColorSel" (the rear showcase view);
// overwrite that preset's numbers with our 3/4 camera while the screen is up.
void BuyStageColorInit(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr) {
  RestoreColorPreset();
  if (!Enabled() || !ColorCameraEnabled()) return;
  const uint32_t G = Load32(kGarageGlobal);
  if (!Readable(G + 16, 4)) return;
  const uint32_t flow = Load32(G + 16);
  if (flow < 9 || flow > 17 || !FlowAllowed(flow)) return;
  if (!EnsureScratch()) return;
  Guest g(r1, r13, fpscr);
  if (!g.ok()) return;
  const uint32_t preset = FindPreset(g, kOffName2);
  if (!preset) {
    REXLOG_INFO("Buy stage: colour screen camera preset CarColorSel not found");
    return;
  }
  std::memcpy(g_color_saved, Ptr(preset), 36);
  g_color_preset = preset;
  g_color_table = Load32(kPresetTableSlot);
  WritePresetNumbers(preset);
  g_color_patched = true;
  g_color_pending = true;
  REXLOG_INFO("Buy stage: colour screen (flow {}) uses the 3/4 camera", flow);
}

// 0x8250D200 in CCarSelect::OnCarChosen (A on a car), just before it asks the UI to open the
// next screen: r28 = car Id, r31 = the list screen. Returns true to skip the screen change.
// EXPERIMENTAL direct buy: runs the game's own purchase (sub_824FBB98 -> sub_82433230: checks
// credits, debits, adds the car, makes it the current car) in the default colour.
bool BuyStageDirectBuy(PPCRegister& r28, PPCRegister& r31, PPCRegister& r1, PPCRegister& r13,
                       PPCFPSCRRegister& fpscr) {
  const int mode = REXCVAR_GET(fm4_buycars_direct);
  if (mode == 0 || !Enabled()) return false;
  const uint32_t car = r28.u32, screen = r31.u32;
  if (!car || car >= kSentinelCarId) return false;
  const uint32_t G = Load32(kGarageGlobal);
  if (!Readable(G + 16, 4) || !Readable(screen, 640)) return false;
  const uint32_t flow = Load32(G + 16);
  if (!FlowInList(std::string(REXCVAR_GET(fm4_buycars_direct_flows)), flow, false)) return false;
  const int want_type = REXCVAR_GET(fm4_buycars_direct_listtype);
  if (want_type >= 0 && Load32(screen + 632) != uint32_t(want_type)) return false;
  if (Ptr(screen + 584)[0] != 0) return false;  // multi-select list: leave to the game
  if (mode == 2 && !KeyShift()) return false;
  if (!EnsureScratch()) return false;
  Guest g(r1, r13, fpscr);
  if (!g.ok()) return false;
  std::memset(Ptr(g_scratch + kOffBuy), 0, 0x400);
  Store32(g_scratch + kOffOut, 0);
  Store32(G + 16 + 764, car);
  Store32(G + 16 + 768, PaintByte());
  g.Call(kFnBuyCurrent, g_scratch + kOffOut, g_scratch + kOffBuy, 0);
  const uint32_t garage_id = Load32(g_scratch + kOffBuy + 496);
  if (!g.ok()) {
    REXLOG_WARN("Buy stage: direct buy of car {} failed inside the game code", car);
    return true;
  }
  if (!garage_id) {
    REXLOG_INFO("Buy stage: direct buy of car {} did nothing (not enough credits?); opening the normal screen", car);
    return false;
  }
  REXLOG_INFO("Buy stage: direct buy of car {} done, new garage car {}", car, garage_id);
  g.VCall(screen, 244);  // refresh the list (vtable slot 61)
  return true;
}
