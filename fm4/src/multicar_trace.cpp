// ---------------------------------------------------------------------------
// Multi-car front-end trace (Forza-4-Evolution). LOG ONLY: reads registers and
// memory, never changes game state, never calls game code.
//
// Question being researched: can the homespace (main-menu stage) or the showroom
// photo mode show more than one car? This logs, to fm4.log, lines starting with
// "[mcar]":
//   * every front-end screen mode change (screen class name from RTTI, mode,
//     render scenario, caller chain)
//   * ShowCar / SetCar calls (which car goes onto the stage, from where)
//   * stage camera type switches (0 = homespace cams, 1 = car-target presets)
//   * every car presentation (the 3D car object) created and destroyed, with a
//     live count, so the homespace (1 car?) can be compared with a race (N cars)
//   * the deferred (render-thread) car wrapper, created and destroyed
//   * homespace object (CUI4Homespace) and photo mode screens being created
// Off by default. Turn on with the F4 overlay setting
// "Forza Motorsport 4 > Multi-car trace > fm4_mcar_trace" or FM4_MCAR_TRACE=1.
// "fm4_mcar_dump" (F4 overlay button) writes the list of live car presentations.
//
// Addresses (static, default.pe; class names from the exe's RTTI, see
// tools/re/rtti_map.py):
//   0x8251BAA8 screen set-mode wrapper (r3 = screen, r4 = mode) -> 0x824FFA48
//   0x824FF838 ShowCar (stage car from G = *(0x82DC3828), car Id at G+32)
//   0x824DF7B0 SetCar wrapper (r3 = car view, r4.. = car args)
//   0x82652368 stage view: set camera type (r3 = view, r4 = type, r5 = force)
//   0x828DB578 / 0x828DB778  CCarPresentation base ctor / dtor (all car kinds)
//   0x828D8A18 / 0x828D8AE0  CCarPresentationDeferred ctor / dtor
//   0x824CB278 / 0x824CB300  CUI4Homespace ctor / dtor
//   0x825AF638 CPhotoModeIngame ctor, 0x825AF6E8 CPhotoModeShowroom ctor
// All hooks run after the function's first instruction (mflr r12), so r12 = the
// caller's return address and r1 = the caller's stack pointer.
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <map>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(fm4_mcar_trace, false, "Forza Motorsport 4/Multi-car trace",
                    "Log front-end screens, stage car changes and every 3D car object created or destroyed "
                    "to fm4.log (lines start with [mcar]). Research only, changes nothing in the game");

namespace {
// image ranges (flat image; see rtti_map.py)
constexpr uint32_t kRdataLo = 0x82000600, kRdataHi = 0x82254334;
constexpr uint32_t kDataLo = 0x82D50000, kDataHi = 0x82E9E0A8;
constexpr uint32_t kTextLo = 0x822A0000, kTextHi = 0x82D4C99C;
constexpr uint32_t kGarageGlobal = 0x82DC3828;

rex::memory::Memory* Mem() { return REX_KERNEL_MEMORY(); }
uint32_t Load32(uint32_t a) {
  const uint8_t* p = Mem()->TranslateVirtual<const uint8_t*>(a);
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
bool InImage(uint32_t a, uint32_t lo, uint32_t hi) { return a >= lo && a + 4 <= hi && (a & 3) == 0; }
// Heap / stack / physical ranges the game's objects live in. Only used for
// pointers the game itself just passed as "this", so they are live objects.
bool PlausibleObj(uint32_t a) {
  if (a & 3) return false;
  return (a >= 0x00010000 && a < 0x80000000) || (a >= 0x82000000 && a < 0x82F00000) ||
         (a >= 0xA0000000 && a < 0xFFF00000);
}

bool g_env_checked = false, g_env_on = false;
bool On() {
  if (!g_env_checked) {
    g_env_checked = true;
    const char* v = std::getenv("FM4_MCAR_TRACE");
    g_env_on = v && *v && *v != '0';
    if (g_env_on) REXLOG_INFO("[mcar] trace on (FM4_MCAR_TRACE)");
  }
  return g_env_on || REXCVAR_GET(fm4_mcar_trace);
}

unsigned Tid() {
#ifdef _WIN32
  return unsigned(GetCurrentThreadId());
#else
  return 0;
#endif
}

// Class name of an object from its vtable's RTTI (vtable[-1] -> locator ->
// type descriptor -> ".?AVName@Ns@@"). Returns "" when anything looks wrong.
std::string ClassOf(uint32_t obj) {
  if (!PlausibleObj(obj)) return "";
  const uint32_t vt = Load32(obj);
  if (!InImage(vt, kRdataLo + 4, kRdataHi)) return "";
  const uint32_t col = Load32(vt - 4);
  if (!InImage(col, kRdataLo, kRdataHi - 16)) return "";
  const uint32_t td = Load32(col + 12);
  if (!InImage(td, kDataLo, kDataHi - 16)) return "";
  const char* s = Mem()->TranslateVirtual<const char*>(td + 8);
  if (std::strncmp(s, ".?A", 3) != 0) return "";
  std::string raw(s, strnlen(s, 160));
  // ".?AVCMainMenu@ForzaUI@@" -> "ForzaUI::CMainMenu" (templates left mangled)
  if (raw.find("?$") != std::string::npos) return raw;
  std::string body = raw.substr(4);
  std::string parts[8];
  int n = 0;
  size_t start = 0;
  while (n < 8 && start < body.size()) {
    const size_t at = body.find('@', start);
    const size_t stop = at == std::string::npos ? body.size() : at;
    if (stop > start) parts[n++] = body.substr(start, stop - start);
    if (at == std::string::npos) break;
    start = at + 1;
  }
  std::string out;
  for (int i = n - 1; i >= 0; --i) { if (!out.empty()) out += "::"; out += parts[i]; }
  return out;
}

// Caller chain: r12 = direct caller, then walk the PPC back chain from the
// caller's frame (each frame saves its LR at [back_chain - 8]).
std::string Chain(uint32_t r12, uint32_t r1, int depth = 4) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08X", r12);
  std::string s = buf;
  uint32_t sp = r1;
  for (int i = 0; i < depth; ++i) {
    if (!PlausibleObj(sp)) break;
    const uint32_t next = Load32(sp);
    if (next <= sp || next - sp > 0x40000 || !PlausibleObj(next)) break;
    const uint32_t lr = Load32(next - 8);
    if (lr < kTextLo || lr >= kTextHi) break;
    std::snprintf(buf, sizeof(buf), " <- %08X", lr);
    s += buf;
    sp = next;
  }
  return s;
}

const char* Scenario(uint32_t mode) {
  switch (mode) {
    case 0: case 4: case 5: case 6: case 7: return "UI_Homespace";
    case 1: case 2: case 8: case 9: case 10: case 13: case 14: return "UI_Car";
    case 3: case 11: case 12: return "UI_NoCar";
    case 15: return "UI_Showroom";
    default: return "?";
  }
}

const char* PresKind(uint32_t caller) {
  // Derived constructors that call the CCarPresentation base constructor.
  if (caller >= 0x829572B0 && caller < 0x82957600) return "Presentation_Unified";
  if (caller >= 0x828F2858 && caller < 0x828F2C00) return "Default_Presentation";
  if (caller >= 0x828E4910 && caller < 0x828E4C00) return "Null_Presentation";
  return "?";
}

struct Live { std::string kind; std::string chain; uint64_t seq; };
std::mutex g_mu;
std::map<uint32_t, Live> g_pres;      // CCarPresentation objects
std::map<uint32_t, Live> g_deferred;  // CCarPresentationDeferred wrappers
uint64_t g_seq = 0;
uint32_t g_last_mode = 0xFFFFFFFF;

void DumpLive(const char* why) {
  std::lock_guard<std::mutex> lk(g_mu);
  REXLOG_INFO("[mcar] live car presentations: {} (deferred wrappers {}) [{}]", g_pres.size(), g_deferred.size(), why);
  for (const auto& [p, l] : g_pres)
    REXLOG_INFO("[mcar]   pres {:08X} #{} {} class {} created from {}", p, l.seq, l.kind, ClassOf(p), l.chain);
  for (const auto& [p, l] : g_deferred)
    REXLOG_INFO("[mcar]   deferred {:08X} #{} created from {}", p, l.seq, l.chain);
}
}  // namespace

void McarStageModeChanged(uint32_t mode, uint32_t caller);  // second-car test, below
void McarArmQueueLog(uint32_t ms);                           // deferred-command log, below
void McarAdjustZones(uint32_t iface, PPCRegister& r4, PPCRegister& r5, PPCRegister& r8, PPCRegister& r9,
                     PPCRegister& r10, PPCRegister& r1);  // car zones (placement), below

REXCVAR_DEFINE_COMMAND(fm4_mcar_dump, []() { DumpLive("dump button"); }, "Forza Motorsport 4/Multi-car trace",
                       "Write the list of live 3D car objects to fm4.log now");

// 0x8251BAA8 (+4): screen set-mode wrapper. r3 = screen, r4 = mode.
void McarModeSet(PPCRegister& r3, PPCRegister& r4, PPCRegister& r12, PPCRegister& r1) {
  McarStageModeChanged(r4.u32, r12.u32);
  if (!On()) return;
  const uint32_t mode = r4.u32;
  REXLOG_INFO("[mcar] screen {:08X} {} sets mode {} ({}) from {}", r3.u32, ClassOf(r3.u32), mode, Scenario(mode),
              Chain(r12.u32, r1.u32));
  if (mode != g_last_mode) { g_last_mode = mode; DumpLive("mode change"); }
}

// 0x824FF838 (+4): ShowCar. Logs the car the stage will show.
void McarShowCar(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12, PPCRegister& r1) {
  if (!On()) return;
  const uint32_t G = Load32(kGarageGlobal);
  const uint32_t car = PlausibleObj(G) ? Load32(G + 32) : 0;
  REXLOG_INFO("[mcar] ShowCar r3={:08X} r4={:08X} r5={:08X} stage car {} (G {:08X}) from {}", r3.u32, r4.u32, r5.u32,
              car, G, Chain(r12.u32, r1.u32));
}

// 0x824DF7B0 (+4): SetCar wrapper. r3 = car view object.
void McarSetCar(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6, PPCRegister& r12,
                PPCRegister& r1) {
  if (!On()) return;
  REXLOG_INFO("[mcar] SetCar view {:08X} {} r4={} r5={:08X} r6={:08X} thread {} from {}", r3.u32, ClassOf(r3.u32),
              r4.u32, r5.u32, r6.u32, Tid(), Chain(r12.u32, r1.u32));
}

// 0x82652368 (+4): stage view camera type switch.
void McarCameraType(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r12, PPCRegister& r1) {
  if (!On()) return;
  REXLOG_INFO("[mcar] camera type -> {} (force {}) view {:08X} {} from {}", int32_t(r4.u32), r5.u32, r3.u32,
              ClassOf(r3.u32), Chain(r12.u32, r1.u32));
}

// 0x828DB578 (+4): CCarPresentation base constructor (r3 = this).
void McarPresCtor(PPCRegister& r3, PPCRegister& r4, PPCRegister& r12, PPCRegister& r1) {
  if (!On()) return;
  const std::string chain = Chain(r12.u32, r1.u32, 6);
  size_t n;
  uint64_t seq;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    seq = ++g_seq;
    g_pres[r3.u32] = Live{PresKind(r12.u32), chain, seq};
    n = g_pres.size();
  }
  REXLOG_INFO("[mcar] + car presentation {:08X} #{} {} type {:08X} (live {}) thread {} from {}", r3.u32, seq,
              PresKind(r12.u32), r4.u32, n, Tid(), chain);
}

// 0x828DB778 (+4): CCarPresentation destructor.
void McarPresDtor(PPCRegister& r3, PPCRegister& r12) {
  if (!On()) return;
  size_t n;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_pres.erase(r3.u32);
    n = g_pres.size();
  }
  REXLOG_INFO("[mcar] - car presentation {:08X} (live {}) from {:08X}", r3.u32, n, r12.u32);
}

// 0x828D8A18 (+4): CCarPresentationDeferred constructor.
void McarDeferredCtor(PPCRegister& r3, PPCRegister& r12, PPCRegister& r1) {
  if (!On()) return;
  const std::string chain = Chain(r12.u32, r1.u32, 6);
  size_t n;
  uint64_t seq;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    seq = ++g_seq;
    g_deferred[r3.u32] = Live{chain.find("824DE3FC") != std::string::npos ? "ui" : "deferred", chain, seq};
    n = g_deferred.size();
  }
  REXLOG_INFO("[mcar] + deferred car {:08X} #{} (live {}) thread {} from {}", r3.u32, seq, n, Tid(), chain);
  if (chain.find("824DE3FC") != std::string::npos) McarArmQueueLog(1500);
}

// 0x828D8AE0 (+4): CCarPresentationDeferred destructor.
void McarDeferredDtor(PPCRegister& r3, PPCRegister& r12) {
  if (!On()) return;
  size_t n;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_deferred.erase(r3.u32);
    n = g_deferred.size();
  }
  REXLOG_INFO("[mcar] - deferred car {:08X} (live {}) from {:08X}", r3.u32, n, r12.u32);
}

// 0x824CB278 / 0x824CB300 (+4): CUI4Homespace constructor / destructor.
void McarHomespaceCtor(PPCRegister& r3, PPCRegister& r4, PPCRegister& r12, PPCRegister& r1) {
  if (!On()) return;
  REXLOG_INFO("[mcar] + CUI4Homespace {:08X} arg {:08X} from {}", r3.u32, r4.u32, Chain(r12.u32, r1.u32, 6));
}
void McarHomespaceDtor(PPCRegister& r3, PPCRegister& r12) {
  if (!On()) return;
  REXLOG_INFO("[mcar] - CUI4Homespace {:08X} from {:08X}", r3.u32, r12.u32);
}

// 0x825AF638 / 0x825AF6E8 (+4): photo mode screens being constructed.
void McarPhotoIngameCtor(PPCRegister& r3, PPCRegister& r12, PPCRegister& r1) {
  if (!On()) return;
  REXLOG_INFO("[mcar] + CPhotoModeIngame {:08X} from {}", r3.u32, Chain(r12.u32, r1.u32, 6));
  DumpLive("in-race photo mode");
}
void McarPhotoShowroomCtor(PPCRegister& r3, PPCRegister& r12, PPCRegister& r1) {
  if (!On()) return;
  REXLOG_INFO("[mcar] + CPhotoModeShowroom {:08X} from {}", r3.u32, Chain(r12.u32, r1.u32, 6));
  DumpLive("showroom photo mode");
}

// ---------------------------------------------------------------------------
// Trace v2: deferred ICarPresentation::SetTransform (vtable slot 8, sub_82352160).
// r3 = the ICarPresentation interface (= deferred wrapper + 8). The function
// saves r4..r10 as 64-bit values (the transform is passed by value), so each is
// logged as two floats. Front-end cars ("ui" wrappers) are logged up to 40 times
// each, race cars 3 times each.
// ---------------------------------------------------------------------------
namespace {
std::map<uint32_t, uint32_t> g_xform_count;
float HiF(uint64_t v) { return std::bit_cast<float>(uint32_t(v >> 32)); }
float LoF(uint64_t v) { return std::bit_cast<float>(uint32_t(v)); }
}  // namespace

void McarSetTransform(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6, PPCRegister& r7,
                      PPCRegister& r8, PPCRegister& r9, PPCRegister& r10, PPCRegister& r12, PPCRegister& r1) {
  McarAdjustZones(r3.u32, r4, r5, r8, r9, r10, r1);
  if (!On()) return;
  const uint32_t wrapper = r3.u32 - 8;
  bool ui = false;
  uint32_t n;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_deferred.find(wrapper);
    ui = it != g_deferred.end() && it->second.kind == "ui";
    n = ++g_xform_count[wrapper];
  }
  if (n > (ui ? 40u : 3u)) return;
  const PPCRegister* regs[7] = {&r4, &r5, &r6, &r7, &r8, &r9, &r10};
  std::string f;
  char buf[48];
  for (const PPCRegister* r : regs) {
    std::snprintf(buf, sizeof(buf), " %.3f %.3f", HiF(r->u64), LoF(r->u64));
    f += buf;
  }
  REXLOG_INFO("[mcar] SetTransform {} car {:08X} #{} floats{} from {}", ui ? "UI" : "race", wrapper, n, f,
              Chain(r12.u32, r1.u32, 6));
}

// ---------------------------------------------------------------------------
// Trace v3: car placement sub_824B9B30(CUI4Car* r3, f1, f2, f3). The stage camera
// (CUI4AnimatedCamera slot 1 -> sub_824BDCA0) calls it every frame for the stage
// car; it computes the car's transform from CUI4Car fields and sends SetTransform.
// ---------------------------------------------------------------------------
namespace { uint32_t g_place_logged = 0; }
void McarPlace(PPCRegister& r3, PPCRegister& f1, PPCRegister& f2, PPCRegister& f3, PPCRegister& r12,
               PPCRegister& r1) {
  if (!On()) return;
  const uint32_t n = ++g_place_logged;
  if (n > 30 && n % 600 != 0) return;
  REXLOG_INFO("[mcar] Place car {:08X} {} f1 {:.4f} f2 {:.4f} f3 {:.4f} #{} from {}", r3.u32, ClassOf(r3.u32), f1.f64,
              f2.f64, f3.f64, n, Chain(r12.u32, r1.u32, 5));
}

// ---------------------------------------------------------------------------
// EXPERIMENT v3: a second car on the main-menu stage / showroom photo mode, in
// its OWN CUI4Environment (v2's swap trick shared env1's preload package and load
// request and freed car1 -> crash).
// Off unless fm4_mcar_second_car (F4 > Forza Motorsport 4 > Multi-car trace) is a car id.
//
//   G = manager (CUI4ForzaUIManager), env1 = [[G+2448]], car1 = [env1+24].
//   Build env2 = new(80), ctor sub_824DBB00(env2, &presRef, &viewRef, index) with
//     AddRef'd copies of env1+12 (presentation) and env1+16 ("trackHomespace" view)
//     (the ctor copies both and releases the passed refs, like manager init sub_824E4080).
//     env2 gets its own CUICarPreloadPackage (+20), CUI4Car (+24), CUI4Homespace (+28).
//     env2 is NOT activated (sub_824BADF8) and its full update is NOT run (that would
//     update the shared presentation and view twice per frame).
//   Set the car: vec[0] = env2 for one call of the game's show-stock-car
//     sub_824CDC18(id, paint, 0) (-> SetCar sub_824DEA38(env2)), then vec[0] = env1
//     and G+32/+36/+72 restored.
//   Every frame after env1's update (0x8239A6AC, f31 = dt), the car parts of the
//     environment update sub_8239A718, for env2:
//       CUI4Car::Update sub_824DB470(car2, dt);
//       load request: if [env2+72] && car2 presentation && ![env2+68]:
//         sub_8290E900(env2+40, [env2+32], [env2+36]); if [env2+68]: [env2+72] = 0;
//       if car2 presentation && [car2+2513]: presentation vtable +300 (as the env update);
//       ShowCar (+176) once car2's +2512 loaded flag is set.
//   Off the stage: HideCar (+180). Experiment switched off, or the game unloads the
//     stage cars (sub_824DD990, hooked at entry): env "release car" sub_824DD928(env2).
//     env2 itself is kept for the session.
// No positioning yet: car2 sits where SetCar placed it (stage origin).
// ---------------------------------------------------------------------------
REXCVAR_DEFINE_INT32(fm4_mcar_second_car, 0, "Forza Motorsport 4/Multi-car trace",
                     "EXPERIMENT: car id of a SECOND car on the main-menu stage and showroom photo mode "
                     "(0 = off). Do not save it to the config")
    .range(0, 1000000);
REXCVAR_DEFINE_INT32(fm4_mcar_second_paint, 1, "Forza Motorsport 4/Multi-car trace",
                     "Colour number for the second car (1 = its first colour)")
    .range(0, 255);
REXCVAR_DEFINE_INT32(fm4_mcar_second_index, 1, "Forza Motorsport 4/Multi-car trace",
                     "Environment index given to the second car's environment (the game's is 0). Takes effect "
                     "only before the second environment is first built")
    .range(0, 15);
// Zones: where each car stands on the stage (main menu / showroom photo mode only). Live.
REXCVAR_DEFINE_DOUBLE(fm4_mcar1_x, 0.0, "Forza Motorsport 4/Multi-car trace/Car 1 zone",
                      "Car 1 (your car): sideways, metres").range(-15.0, 15.0);
REXCVAR_DEFINE_DOUBLE(fm4_mcar1_z, 0.0, "Forza Motorsport 4/Multi-car trace/Car 1 zone",
                      "Car 1: forward/back, metres").range(-15.0, 15.0);
REXCVAR_DEFINE_DOUBLE(fm4_mcar1_y, 0.0, "Forza Motorsport 4/Multi-car trace/Car 1 zone",
                      "Car 1: up/down, metres").range(-2.0, 2.0);
REXCVAR_DEFINE_DOUBLE(fm4_mcar1_turn, 0.0, "Forza Motorsport 4/Multi-car trace/Car 1 zone",
                      "Car 1: turn, degrees").range(-180.0, 180.0);
REXCVAR_DEFINE_DOUBLE(fm4_mcar2_x, 2.4, "Forza Motorsport 4/Multi-car trace/Car 2 zone",
                      "Car 2 (second car): sideways, metres").range(-15.0, 15.0);
REXCVAR_DEFINE_DOUBLE(fm4_mcar2_z, 0.0, "Forza Motorsport 4/Multi-car trace/Car 2 zone",
                      "Car 2: forward/back, metres").range(-15.0, 15.0);
REXCVAR_DEFINE_DOUBLE(fm4_mcar2_y, 0.0, "Forza Motorsport 4/Multi-car trace/Car 2 zone",
                      "Car 2: up/down, metres").range(-2.0, 2.0);
REXCVAR_DEFINE_DOUBLE(fm4_mcar2_turn, 180.0, "Forza Motorsport 4/Multi-car trace/Car 2 zone",
                      "Car 2: turn, degrees (180 = facing the other way)").range(-180.0, 180.0);
REXCVAR_DEFINE_COMMAND(fm4_mcar_second_remove, []() { REXCVAR_SET(fm4_mcar_second_car, 0); },
                       "Forza Motorsport 4/Multi-car trace", "Remove the second car now");
namespace {
void ResetZones() {
  const char* const names[] = {"fm4_mcar1_x", "fm4_mcar1_z", "fm4_mcar1_y", "fm4_mcar1_turn",
                               "fm4_mcar2_x", "fm4_mcar2_z", "fm4_mcar2_y", "fm4_mcar2_turn"};
  for (const char* n : names) rex::cvar::ResetToDefault(n);
}
}  // namespace
REXCVAR_DEFINE_COMMAND(fm4_mcar_zones_reset, []() { ResetZones(); }, "Forza Motorsport 4/Multi-car trace",
                       "Put both cars back to the default zones");

namespace {
constexpr uint32_t kFnNew = 0x82853370;
constexpr uint32_t kFnEnvCtor = 0x824DBB00;
constexpr uint32_t kFnEnvReleaseCar = 0x824DD928;
constexpr uint32_t kFnUI4CarUpdate = 0x824DB470;
constexpr uint32_t kFnLoadRequest = 0x8290E900;
constexpr uint32_t kFnShowStockCar = 0x824CDC18;
constexpr uint32_t kFnCopyHandle = 0x824F1D78;
constexpr uint32_t kFnAssignHandle = 0x823ECC48;
constexpr uint32_t kFnCopyPresRef = 0x824C2120;   // (dst holder, src holder), AddRef
constexpr uint32_t kFnCopyViewRef = 0x82479318;   // (dst holder, src holder), AddRef
constexpr uint32_t kFnPlaceCar = 0x824B9B30;      // (CUI4Car*, f1, f2, f3): transform -> SetTransform
constexpr uint32_t kEnvSize = 80;
constexpr uint32_t kPresShow = 176, kPresHide = 180, kPresTail = 300;
constexpr uint32_t kScratch2Size = 0x200;  // +0 livery handle copy (32), +0x40 parts (180), +0x100/+0x110 holders

rex::memory::Memory* Mem2() { return REX_KERNEL_MEMORY(); }
uint8_t* Base2() { return Mem2()->virtual_membase(); }
uint8_t* P2(uint32_t a) { return Mem2()->TranslateVirtual<uint8_t*>(a); }
void Store32b(uint32_t a, uint32_t v) {
  uint8_t* p = P2(a);
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
uint8_t Load8(uint32_t a) { return *P2(a); }
float LoadF(uint32_t a) { return std::bit_cast<float>(Load32(a)); }

// Guest call helper (same protocol as buy_stage.cpp / dev_keys.cpp): runs on the main
// game thread's own context, stack moved below the hooked frame, re-entry unwinds
// followed, whole context restored afterwards.
class Guest2 {
 public:
  explicit Guest2(PPCRegister& r1) {
    auto* ts = rex::runtime::ThreadState::Get();
    ctx_ = ts ? ts->context() : nullptr;
    if (!ctx_ || &ctx_->r1 != &r1) { ctx_ = nullptr; ok_ = false; return; }
    sp_ = (r1.u32 - 0x400) & ~0xFu;
  }
  bool ok() const { return ok_; }
  uint32_t Call(uint32_t target, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) {
    return CallF(target, 0.0, false, a, b, c, d);
  }
  uint32_t CallF(uint32_t target, double f1, bool set_f1, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0,
                 uint32_t d = 0, bool set_f23 = false, double f2 = 0.0, double f3 = 0.0) {
    if (!ok_ || !target) { ok_ = false; return 0; }
    static_assert(sizeof(PPCContext) <= sizeof(saved_));
    std::memcpy(saved_, static_cast<void*>(ctx_), sizeof(PPCContext));
    PPCContext& ctx = *ctx_;
    ctx.r1.u64 = sp_;
    ctx.r3.u64 = a; ctx.r4.u64 = b; ctx.r5.u64 = c; ctx.r6.u64 = d;
    if (set_f1) ctx.f1.f64 = f1;
    if (set_f23) { ctx.f2.f64 = f2; ctx.f3.f64 = f3; }
    ctx.lr = 0;
    uint32_t next = target;
    bool nonlocal = false, failed = false;
    for (uint32_t steps = 0;; ++steps) {
      if (steps > 1000000) { failed = true; break; }
      PPCFunc* fn = rex::runtime::ResolveIndirectFunction(next);
      if (!fn) { failed = true; break; }
      try {
        ctx.dispatch_address = next;
        fn(ctx, Base2());
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
      REXLOG_WARN("[mcar2] guest call {:08X} could not finish (stuck at {:08X})", target, next);
      ok_ = false;
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

// ---- state (main game thread only) ----
bool g_stage_ok = false;    // main menu or showroom photo mode is up
uint32_t g_stage_frames = 0;
bool g_faulted2 = false;
uint32_t g_env2 = 0;        // our CUI4Environment
uint32_t g_car2_id = 0;     // car id given to it (0 = none / released)
bool g_car2_visible = false;
uint32_t g_scratch2 = 0;
uint32_t g_log2 = 0;
uint32_t g_frame2 = 0;

bool IsStageCaller(uint32_t caller) {
  return (caller >= 0x8258C8F8 && caller < 0x8258D400) ||  // CMainMenu (set-mode call at 0x8258CBBC)
         (caller >= 0x825B9700 && caller < 0x825B9900);    // CPhotoModeShowroom (0x825B97DC)
}
uint32_t Car2() { return g_env2 ? Load32(g_env2 + 24) : 0; }
uint32_t Car2Pres() {
  const uint32_t c = Car2();
  const uint32_t p = PlausibleObj(c) ? Load32(c + 16) : 0;
  return PlausibleObj(p) ? p : 0;
}
void Fault(const char* what) {
  g_faulted2 = true;
  REXLOG_WARN("[mcar2] {} failed; experiment off for this session", what);
}
// Game's "release the environment's car" (presentation released, load cancelled).
void ReleaseCar2(Guest2& g, const char* why) {
  if (!g_env2 || !g_car2_id) return;
  // The release skips DestroyAllCars for car2 (McarDestroyAll), so its presentation stays in the
  // presentation's car list until the game's next DestroyAllCars: hide it first.
  if (const uint32_t pres = Car2Pres()) g.VCall(pres, kPresHide);
  g.Call(kFnEnvReleaseCar, g_env2);
  if (!g.ok()) { Fault("release car2"); return; }
  REXLOG_INFO("[mcar2] car2 released ({})", why);
  g_car2_id = 0;
  g_car2_visible = false;
}
}  // namespace

void McarStageModeChanged(uint32_t mode, uint32_t caller) {
  const bool ok = (mode == 0 || (mode >= 4 && mode <= 7)) && IsStageCaller(caller);
  if (ok != g_stage_ok && REXCVAR_GET(fm4_mcar_second_car) != 0)
    REXLOG_INFO("[mcar2] stage {} (mode {}, caller {:08X})", ok ? "up" : "down", mode, caller);
  g_stage_ok = ok;
  g_stage_frames = 0;
}

// 0x824DD990 (+4): the game unloads the stage cars (e.g. before a race). Release car2 first.
void McarUnloadAll(PPCRegister& r3, PPCRegister& r1) {
  if (g_faulted2 || !g_env2 || !g_car2_id) return;
  if (Load32(kGarageGlobal) != r3.u32) return;
  Guest2 g(r1);
  if (!g.ok()) { Fault("unload hook context"); return; }
  ReleaseCar2(g, "game unloads the stage");
}

// 0x8239A6AC in sub_8239A5E8 (manager frame update), right after env1's update. f31 = dt.
void McarEnvFrame(PPCRegister& r31, PPCRegister& r1, PPCRegister& f31) {
  const uint32_t mgr = r31.u32;
  if (!PlausibleObj(mgr) || Load32(kGarageGlobal) != mgr) return;  // only the UI manager's update
  const uint32_t vb = Load32(mgr + 2448), ve = Load32(mgr + 2452);
  if (!PlausibleObj(vb) || ve < vb + 4) return;
  const uint32_t env1 = Load32(vb);
  if (!PlausibleObj(env1)) return;
  const uint32_t car1 = Load32(env1 + 24);
  ++g_frame2;

  // Trace: car1 placement fields now and then (floats; which are position is unknown yet).
  if (On() && PlausibleObj(car1) && g_frame2 % 300 == 1)
    REXLOG_INFO("[mcar] car1 {:08X} +2432 {:.3f} {:.3f} {:.3f} +2240 {:.3f} {:.3f} {:.3f} +2516 {:.3f} {:.3f} "
                "+2504 {:.3f}", car1, LoadF(car1 + 2432), LoadF(car1 + 2436), LoadF(car1 + 2440),
                LoadF(car1 + 2240), LoadF(car1 + 2244), LoadF(car1 + 2248), LoadF(car1 + 2516),
                LoadF(car1 + 2520), LoadF(car1 + 2504));

  if (g_faulted2) return;
  const uint32_t want = uint32_t(REXCVAR_GET(fm4_mcar_second_car));
  if (!want) {
    if (g_car2_id) { Guest2 g(r1); if (g.ok()) ReleaseCar2(g, "switched off"); }
    return;
  }
  if (!g_stage_ok) {
    if (g_car2_visible) {
      if (const uint32_t pres = Car2Pres()) {
        Guest2 g(r1);
        g.VCall(pres, kPresHide);
        if (!g.ok()) { Fault("HideCar"); return; }
        REXLOG_INFO("[mcar2] car2 hidden (stage down)");
      }
      g_car2_visible = false;
    }
    return;
  }
  if (++g_stage_frames < 90) return;  // let the screen and car1 settle (about 1.5 s)
  if (!PlausibleObj(car1) || !PlausibleObj(Load32(car1 + 16)) || Load8(car1 + 2512) == 0) return;

  Guest2 g(r1);
  if (!g.ok()) { Fault("game-thread context"); return; }

  if (!g_scratch2) {
    g_scratch2 = Mem2()->SystemHeapAlloc(kScratch2Size);
    if (!g_scratch2) { Fault("scratch alloc"); return; }
    std::memset(P2(g_scratch2), 0, kScratch2Size);
  }
  if (!g_env2) {
    const uint32_t hp = g_scratch2 + 0x100, hv = g_scratch2 + 0x110;
    Store32b(hp, 0);
    Store32b(hv, 0);
    g.Call(kFnCopyPresRef, hp, env1 + 12);
    g.Call(kFnCopyViewRef, hv, env1 + 16);
    const uint32_t env2 = g.Call(kFnNew, kEnvSize);
    if (!g.ok() || !PlausibleObj(env2)) { Fault("env2 alloc"); return; }
    g.Call(kFnEnvCtor, env2, hp, hv, uint32_t(REXCVAR_GET(fm4_mcar_second_index)));
    if (!g.ok()) { Fault("env2 ctor"); return; }
    g_env2 = env2;
    REXLOG_INFO("[mcar2] env2 {:08X} ({}) built: preload {:08X} car {:08X} {} homespace {:08X} (env1 {:08X})", env2,
                ClassOf(env2), Load32(env2 + 20), Load32(env2 + 24), ClassOf(Load32(env2 + 24)), Load32(env2 + 28),
                env1);
  }

  if (g_car2_id != want) {
    if (g_car2_id) ReleaseCar2(g, "car changed");
    if (!g.ok() || g_faulted2) return;
    const uint32_t G = mgr;
    const uint32_t id1 = Load32(G + 32);
    std::memcpy(P2(g_scratch2 + 0x40), P2(G + 72), 180);
    g.Call(kFnCopyHandle, g_scratch2, G + 36);
    Store32b(vb, g_env2);  // the SetCar path picks vec[G+8] = vec[0]
    Store32b(G + 32, 0);   // force the full "build parts and show" path
    g.Call(kFnShowStockCar, want, uint32_t(REXCVAR_GET(fm4_mcar_second_paint)), 0);
    const bool shown_ok = g.ok();
    Store32b(vb, env1);
    Store32b(G + 32, id1);
    std::memcpy(P2(G + 72), P2(g_scratch2 + 0x40), 180);
    if (shown_ok) {
      g.Call(kFnAssignHandle, G + 36, g_scratch2);
      const uint32_t h = Load32(g_scratch2 + 28);
      if (h) g.VCall(h, 8);  // release our handle copy
    }
    Store32b(g_scratch2 + 28, 0);
    if (!shown_ok || !g.ok()) { Fault("show car2"); return; }
    g_car2_id = want;
    g_car2_visible = false;
    REXLOG_INFO("[mcar2] env2 given car {} (car1 stays {}): car2 presentation {:08X}, load request {} / {:08X}", want,
                id1, Load32(Car2() + 16), Load8(g_env2 + 72), Load32(g_env2 + 68));
  }

  const uint32_t car2 = Car2();
  if (!PlausibleObj(car2)) return;
  g.CallF(kFnUI4CarUpdate, f31.f64, true, car2);
  if (!g.ok()) { Fault("CUI4Car::Update"); return; }
  if (Load8(g_env2 + 72) && Car2Pres() && Load32(g_env2 + 68) == 0) {
    g.Call(kFnLoadRequest, g_env2 + 40, Load32(g_env2 + 32), Load32(g_env2 + 36));
    if (!g.ok()) { Fault("load request"); return; }
    if (Load32(g_env2 + 68)) {
      Store32b(g_env2 + 72, Load32(g_env2 + 72) & 0x00FFFFFFu);  // byte +72 = 0
      REXLOG_INFO("[mcar2] car2 load request started ({:08X})", Load32(g_env2 + 68));
    }
  }
  const uint32_t pres = Car2Pres();
  if (pres && Load8(car2 + 2513)) {
    g.VCall(pres, kPresTail);
    if (!g.ok()) { Fault("presentation +300"); return; }
  }
  // Placement: the stage camera only places car1 each frame; place car2 the same way
  // (sub_824B9B30, f1 = 1, f2 = f3 = 0). Its SetTransform is rewritten by
  // McarAdjustCar2Transform (flip + sideways offset).
  if (g_car2_visible && pres) {
    g.CallF(kFnPlaceCar, 1.0, true, car2, 0, 0, 0, true, 0.0, 0.0);
    if (!g.ok()) { Fault("place car2"); return; }
  }
  if (!g_car2_visible && pres && Load8(car2 + 2512) != 0) {
    g.VCall(pres, kPresShow);
    if (!g.ok()) { Fault("ShowCar"); return; }
    g_car2_visible = true;
    REXLOG_INFO("[mcar2] car2 ShowCar sent (presentation {:08X})", pres);
  } else if (!g_car2_visible && g_log2++ % 120 == 0) {
    REXLOG_INFO("[mcar2] waiting: presentation {:08X} loaded {} request {} / {:08X}", pres, Load8(car2 + 2512),
                Load8(g_env2 + 72), Load32(g_env2 + 68));
  }
}

// ---------------------------------------------------------------------------
// Trace v4: every deferred render-thread command is queued through sub_8233E908
// (436 call sites): r3 = queue owner, r4 = executor function, r5 = target object,
// r6 = size, r7 = command object (its class names the command, e.g.
// CCarPresentationDeferred::CParams1ICarPresentationLoad). Logged for 1.5 s after
// each front-end car is created (to see what releases the previous stage car), and
// always for commands whose class mentions Destroy/Release/Unload/Cancel/Free.
// ---------------------------------------------------------------------------
namespace {
std::atomic<int64_t> g_queue_until{0};
std::atomic<uint32_t> g_queue_logged{0};
int64_t NowMs2() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

void McarArmQueueLog(uint32_t ms) {
  g_queue_until = NowMs2() + ms;
  g_queue_logged = 0;
}

void McarDeferredQueue(PPCRegister& r4, PPCRegister& r5, PPCRegister& r7, PPCRegister& r12,
                       PPCRegister& r1) {
  if (!On()) return;
  const bool armed = NowMs2() < g_queue_until.load();
  // class name cached per vtable (this runs for every render command)
  thread_local std::map<uint32_t, std::string> cache;
  const uint32_t vt = PlausibleObj(r7.u32) ? Load32(r7.u32) : 0;
  auto it = cache.find(vt);
  if (it == cache.end()) it = cache.emplace(vt, ClassOf(r7.u32)).first;
  const std::string& cls = it->second;
  const bool interesting = cls.find("Destroy") != std::string::npos || cls.find("Release") != std::string::npos ||
                           cls.find("Unload") != std::string::npos || cls.find("Cancel") != std::string::npos ||
                           cls.find("Free") != std::string::npos;
  if (!armed && !interesting) return;
  if (armed && !interesting && ++g_queue_logged > 400) return;
  REXLOG_INFO("[mcar] queue {} cmd {:08X} {} exec {:08X} target {:08X} {} thread {} from {}", armed ? "A" : "I", r7.u32,
              cls, r4.u32, r5.u32, ClassOf(r5.u32), Tid(), Chain(r12.u32, r1.u32, 6));
}

// ---------------------------------------------------------------------------
// v5 FIX: the front end's "release car" sub_824DD838(CUI4Car*) begins with
// [car+4] (IPresentation) vtable slot 24 = DestroyAllCars (bctrl at 0x824DD85C;
// found with the v4 queue log: CPresentationDeferred::CParamsIPresentationDestroyAllCars
// queued from 0x824DD860 <- SetCar). The menu assumes one car, so releasing ANY
// CUI4Car destroys EVERY car presentation, which left car1 dangling (v2/v3 crash).
// Hook at that bctrl (r31 = the CUI4Car being released); return true = skip it:
//   * releasing car2 (our CUI4Car): skip DestroyAllCars. car2's old presentation
//     stays in the presentation's car list until the game's next DestroyAllCars.
//   * the game releasing another car (car1 changes, stage unload) while car2 has a
//     presentation: release car2 first (with the skip above) so it drops its
//     pointer, then let DestroyAllCars run. car2 is rebuilt on the next stage frame.
// ---------------------------------------------------------------------------
bool McarDestroyAll(PPCRegister& r31, PPCRegister& r1) {
  const uint32_t car = r31.u32;
  if (!g_env2) return false;
  const uint32_t car2 = Car2();
  if (car == car2) {
    REXLOG_INFO("[mcar2] skipped DestroyAllCars while releasing car2 {:08X}", car2);
    return true;
  }
  if (g_car2_id && !g_faulted2) {
    REXLOG_INFO("[mcar2] game releases car {:08X} (DestroyAllCars): releasing car2 first", car);
    Guest2 g(r1);
    if (!g.ok()) { Fault("DestroyAllCars hook context"); return false; }
    ReleaseCar2(g, "game DestroyAllCars");
  }
  return false;
}

// ---------------------------------------------------------------------------
// v7: car zones. Every SetTransform (deferred ICarPresentation slot 8) for car1's or
// car2's presentation is rewritten while the stage (main menu / showroom photo mode)
// is up. The transform is a 4x4 row-major matrix passed by value: r4..r10 = floats
// 0..13 (two per register, first float in the high word) and floats 14/15 in the
// caller's parameter area at r1+88 (the caller in sub_824B9B30 does std r11,88(r1)).
// Rows 0-2 = rotation (right, up, forward), row 3 = translation (x, y, z, 1).
// Turn = yaw about the car's own vertical axis: row0' = c*row0 - s*row2,
// row2' = s*row0 + c*row2. Offsets are added to the translation.
// ---------------------------------------------------------------------------
namespace {
float Hi(const PPCRegister& r) { return std::bit_cast<float>(uint32_t(r.u64 >> 32)); }
float Lo(const PPCRegister& r) { return std::bit_cast<float>(uint32_t(r.u64)); }
void SetHiLo(PPCRegister& r, float hi, float lo) {
  r.u64 = (uint64_t(std::bit_cast<uint32_t>(hi)) << 32) | std::bit_cast<uint32_t>(lo);
}
void StoreF(uint32_t a, float v) { Store32b(a, std::bit_cast<uint32_t>(v)); }

void ApplyZone(PPCRegister& r4, PPCRegister& r5, PPCRegister& r8, PPCRegister& r9, PPCRegister& r10,
               uint32_t zaddr, double dx, double dy, double dz, double turn_deg) {
  if (turn_deg != 0.0) {
    const double a = turn_deg * 3.14159265358979323846 / 180.0;
    const float c = float(std::cos(a)), sn = float(std::sin(a));
    const float r0[3] = {Hi(r4), Lo(r4), Hi(r5)};
    const float r2[3] = {Hi(r8), Lo(r8), Hi(r9)};
    float n0[3], n2[3];
    for (int i = 0; i < 3; ++i) {
      n0[i] = c * r0[i] - sn * r2[i];
      n2[i] = sn * r0[i] + c * r2[i];
    }
    SetHiLo(r4, n0[0], n0[1]);
    SetHiLo(r5, n0[2], Lo(r5));
    SetHiLo(r8, n2[0], n2[1]);
    SetHiLo(r9, n2[2], Lo(r9));
  }
  if (dx != 0.0 || dy != 0.0) SetHiLo(r10, Hi(r10) + float(dx), Lo(r10) + float(dy));
  if (dz != 0.0 && PlausibleObj(zaddr)) StoreF(zaddr, LoadF(zaddr) + float(dz));
}
}  // namespace

void McarAdjustZones(uint32_t iface, PPCRegister& r4, PPCRegister& r5, PPCRegister& r8, PPCRegister& r9,
                     PPCRegister& r10, PPCRegister& r1) {
  if (!g_stage_ok) return;
  const uint32_t zaddr = r1.u32 + 88;  // float 14 (z) of the matrix, high word of the stack doubleword
  // car2
  if (g_car2_id && g_env2) {
    const uint32_t p2 = Car2Pres();
    if (p2 && iface == p2) {
      ApplyZone(r4, r5, r8, r9, r10, zaddr, REXCVAR_GET(fm4_mcar2_x), REXCVAR_GET(fm4_mcar2_y),
                REXCVAR_GET(fm4_mcar2_z), REXCVAR_GET(fm4_mcar2_turn));
      return;
    }
  }
  // car1: the game's stage car, [[G+2448][0] + 24] + 16 (vec[0] may briefly be env2 during our swap)
  const double x = REXCVAR_GET(fm4_mcar1_x), y = REXCVAR_GET(fm4_mcar1_y), z = REXCVAR_GET(fm4_mcar1_z),
               t = REXCVAR_GET(fm4_mcar1_turn);
  if (x == 0.0 && y == 0.0 && z == 0.0 && t == 0.0) return;
  const uint32_t G = Load32(kGarageGlobal);
  if (!PlausibleObj(G)) return;
  const uint32_t vb = Load32(G + 2448);
  if (!PlausibleObj(vb)) return;
  const uint32_t env = Load32(vb);
  if (!PlausibleObj(env) || env == g_env2) return;
  const uint32_t car1 = Load32(env + 24);
  if (!PlausibleObj(car1) || Load32(car1 + 16) != iface) return;
  ApplyZone(r4, r5, r8, r9, r10, zaddr, x, y, z, t);
}
