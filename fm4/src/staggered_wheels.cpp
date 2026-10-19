// ---------------------------------------------------------------------------
// Staggered wheels: a different rim design on the rear axle (Forza-4-Evolution).
// Research: claude/separate-front-rear-wheels-research-2026-10-12.md
//
// FM4 already keeps two wheel blocks in the car presentation (desc = render
// object + 80): front name std::string at desc+1696, rear at desc+1888, each
// with its own sizes and its own texture set. The rim draw already binds the
// rear texture set to the rear wheels, and every wheel has its own model
// pointer. The stock game just copies the front wheel's name into the rear
// block and loads one rim model (plus an optional "<name>_rightside" model
// for directional rims). This file:
//
//   A. Gives the rear block its own name. The car-setup query (sub_823D4C40)
//      gets an extra column, RearWheelMediaName =
//        1. the rear rim the player pinned for this car (level 3, see D), else
//        2. a List_Wheels row named "<fitted wheel>_rightside" (a staggered
//           set shipped as data: stock or aftermarket, level 1/2), else
//        3. the fitted wheel (stock behaviour).
//      Hooks StaggerSelectHead (0x823D4FAC, r4 = SELECT head, r14 = parts
//      block, r16 = car Id) and StaggerRearColumn (0x823D5E48, r4 = column
//      name for the rear block).
//
//   B. Loads the rear rim into the second model slot. sub_8290A860 builds
//      "game:\media\wheels\<front>_rightside\<front>_rightside.carbin" for that
//      slot. When the rear name differs from the front, we build
//      "game:\media\wheels\<rear>\<rear>.carbin" instead (r29 -> rear name,
//      suffixes -> the plain ones the front model uses). For a "_rightside"
//      partner this is the very same path the game builds anyway.
//      Hooks StaggerRearModelName (0x8290AE30) and four suffix hooks.
//      Cars whose rear equals the front are untouched, so directional
//      "_rightside" rims keep working.
//
//   C. Puts that model on the rear wheels. The rim load callback
//      (sub_82909730, state 2) writes the 4 per-wheel model pointers
//      ([0] FL, [1] FR, [2] RR, [3] RL; [0]/[1] draw with the front texture
//      set, [2]/[3] with the rear one). For a staggered car: front wheels ->
//      slot 1 model, rear wheels -> slot 2 model.
//      Hook StaggerWheelPointers (0x829097EC).
//
//   D. Level 3, pick any rear rim (free, cosmetic, per car model). In the
//      upgrade shop, press F2 (keyboard) or click the right stick (pad) to put
//      the rim currently shown on the front of the car onto the rear. Press
//      again while the front shows that same rim (or Shift+F2) to go back to
//      "match front". Saved in fm4_rear_rims.txt next to the exe
//      ("<CarId> <List_Wheels Id>", -1 = the car's stock wheel). A small HUD
//      shows the rear rim while the shop is open.
//      Hooks StaggerShopFrame (0x82612AE4), StaggerFrontNameSeen
//      (0x823D5E24), StaggerRearNameSeen (0x823D5E6C). The shop car is
//      rebuilt through brake_preview.cpp (UpgradePreviewRequestReload).
//
// Known limits: the pinned rear rim is per car model (Data_Car Id), and AI
// cars of the same model with identical parts also get it. The rim shop icon
// stays one per car. If a rear rim's carbin is missing the game falls back to
// the front model (no stagger) but still uses the rear rim's textures.
//
// FM4_STAGGERED_WHEELS=0 turns everything off; FM4_STAGGERED_WHEELS_PIN=0
// turns off only the rear-rim picker (D).
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
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "user32.lib")
#endif

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

// brake_preview.cpp: rebuild the shop car (full model reload) on the next shop frame.
void UpgradePreviewRequestReload();

namespace {

// ---- guest layout ---------------------------------------------------------
constexpr uint32_t kGarageGlobal = 0x82DC3828;  // G; G+32 car Id, G+72 parts
constexpr uint32_t kCarIdOffset = 32;
constexpr uint32_t kPartsOffset = 72;
constexpr uint32_t kPartsSlots = 45;            // 180-byte parts block
constexpr uint32_t kSlotWheelStyle = 1;         // -1 = stock wheel
constexpr uint32_t kDescOffset = 80;            // render object -> car presentation
constexpr uint32_t kDescFrontName = 1696;       // std::string (28 bytes)
constexpr uint32_t kDescRearName = 1888;
constexpr uint32_t kRightsideFlag = 39280;      // byte: second model loaded
constexpr uint32_t kWheelPtrs = 13632;          // from render object + 2368

// .rdata strings the front model path uses (checked at first use).
constexpr uint32_t kStrBackslash = 0x82000D18;   // "\\"
constexpr uint32_t kStrSlodDir = 0x82016F14;     // "_SLOD\\"
constexpr uint32_t kStrCarbin = 0x82001368;      // ".carbin"
constexpr uint32_t kStrSCarbin = 0x82151610;     // "_s.carbin"
constexpr uint32_t kStrSelectHead = 0x8200CB20;  // car-setup SELECT head

// Slots a shop preview may change (wheel style, brakes, rim sizes, exhaust):
// ignored when deciding "is this the player's car".
bool PreviewSlot(uint32_t slot) { return slot == 1 || slot == 6 || slot == 12 || slot == 13 || slot == 20; }

using Clock = std::chrono::steady_clock;

// ---- switches -------------------------------------------------------------
bool EnvOn(const char* name) {
  const char* v = std::getenv(name);
  return !(v && v[0] == '0');
}
bool Enabled() {
  static const bool on = EnvOn("FM4_STAGGERED_WHEELS");
  return on;
}
bool PinEnabled() {
  static const bool on = Enabled() && EnvOn("FM4_STAGGERED_WHEELS_PIN");
  return on;
}

// ---- guest memory helpers -------------------------------------------------
rex::memory::Memory* Mem() { return REX_KERNEL_MEMORY(); }

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
uint8_t Load8(uint32_t address) { return *Mem()->TranslateVirtual<const uint8_t*>(address); }

// std::string in this build: +0 inline buffer (or heap pointer when capacity
// at +20 >= 16), +16 length.
bool ReadStdString(uint32_t s, std::string& out) {
  out.clear();
  if (!Readable(s, 24)) return false;
  const uint32_t len = Load32(s + 16), cap = Load32(s + 20);
  if (len > 256) return false;
  const uint32_t data = cap >= 16 ? Load32(s) : s;
  if (len && !Readable(data, len)) return false;
  out.assign(Mem()->TranslateVirtual<const char*>(data), len);
  return true;
}
bool ReadCString(uint32_t a, std::string& out, uint32_t max = 512) {
  out.clear();
  for (uint32_t i = 0; i < max; ++i) {
    if (!Readable(a + i, 1)) return false;
    const char c = *Mem()->TranslateVirtual<const char*>(a + i);
    if (!c) return true;
    out.push_back(c);
  }
  return false;
}
bool StringAt(uint32_t address, const char* text) {
  const uint32_t n = uint32_t(std::strlen(text)) + 1;
  return Readable(address, n) && std::memcmp(Mem()->TranslateVirtual<const char*>(address), text, n) == 0;
}

uint32_t GuestAlloc(uint32_t size) { return Mem()->SystemHeapAlloc(size); }
void GuestWrite(uint32_t address, const std::string& text) {
  std::memcpy(Mem()->TranslateVirtual<char*>(address), text.c_str(), text.size() + 1);
}

// ---- state ----------------------------------------------------------------
std::mutex g_mutex;
std::map<uint32_t, int32_t> g_pins;  // car Id -> rear List_Wheels Id (-1 = stock)
bool g_pins_loaded = false;
uint32_t g_player_car = 0;           // car Id of the last player-car build
int32_t g_front_style_shown = 0;     // its wheel style (parts slot 1)
std::string g_front_name, g_rear_name;  // names from that build (HUD)
std::atomic<int64_t> g_shop_seen_ms{0};

thread_local bool t_head_applied = false;  // A: our SELECT head is in use
thread_local bool t_redirect = false;      // B: second model slot -> rear rim

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

std::filesystem::path PinPath() { return rex::filesystem::GetExecutableFolder() / "fm4_rear_rims.txt"; }

void LoadPinsLocked() {
  if (g_pins_loaded) return;
  g_pins_loaded = true;
  std::ifstream in(PinPath());
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream f(line);
    uint32_t car = 0;
    int32_t wheel = 0;
    if (f >> car >> wheel && car && wheel) g_pins[car] = wheel;
  }
  REXLOG_INFO("Staggered wheels: {} pinned rear rim(s) loaded", g_pins.size());
}
void SavePinsLocked() {
  std::ofstream out(PinPath(), std::ios::trunc);
  for (const auto& [car, wheel] : g_pins) out << car << ' ' << wheel << '\n';
}

// The player's current car is G+32 with parts G+72. A build is the player's
// car when the car Id matches and the parts block matches except for the
// slots the shop previews change.
bool IsPlayerCarBuild(uint32_t car, uint32_t parts) {
  if (!car || !Readable(kGarageGlobal, 4)) return false;
  const uint32_t g = Load32(kGarageGlobal);
  if (!g || !Readable(g + kCarIdOffset, 4) || Load32(g + kCarIdOffset) != car) return false;
  if (!Readable(g + kPartsOffset, kPartsSlots * 4) || !Readable(parts, kPartsSlots * 4)) return false;
  if (parts == g + kPartsOffset) return true;
  for (uint32_t s = 0; s < kPartsSlots; ++s)
    if (!PreviewSlot(s) && Load32(parts + 4 * s) != Load32(g + kPartsOffset + 4 * s)) return false;
  return true;
}

// Ring of guest buffers for the rewritten SELECT head (the game copies it into
// a std::string right after our hook; a ring keeps concurrent builds safe).
constexpr uint32_t kHeadBufSize = 1024;
constexpr uint32_t kHeadBufCount = 8;
uint32_t g_head_bufs[kHeadBufCount] = {};
std::atomic<uint32_t> g_head_next{0};

uint32_t g_str_rear_column = 0;  // "RearWheelMediaName"

}  // namespace

// ===========================================================================
// A. Rear wheel name
// ===========================================================================

// 0x823D4FAC (after "addi r4,r11,-13536") in sub_823D4C40: r4 = the SELECT
// head (0x8200CB20), r14 = parts block, r16 = car Id. Used by both the stock
// and the aftermarket query (branch at 0x823D4FB0).
void StaggerSelectHead(PPCRegister& r4, PPCRegister& r14, PPCRegister& r16) {
  t_head_applied = false;
  if (!Enabled()) return;
  static const bool head_ok = StringAt(kStrSelectHead, "SELECT *,List_UpgradeBrakes.Level AS BrakeLevel, List_Wheels.MediaName AS WheelMediaName");
  if (!head_ok || r4.u32 != kStrSelectHead) {
    static bool warned = false;
    if (!warned) { warned = true; REXLOG_WARN("Staggered wheels: car-setup SELECT head not found, feature off"); }
    return;
  }

  const uint32_t car = r16.u32, parts = r14.u32;
  std::string pinned;  // level 3 subquery, empty = none
  if (PinEnabled() && IsPlayerCarBuild(car, parts)) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadPinsLocked();
    g_player_car = car;
    g_front_style_shown = int32_t(Load32(parts + 4 * kSlotWheelStyle));
    auto it = g_pins.find(car);
    if (it != g_pins.end()) {
      if (it->second == -1)
        pinned = "(SELECT W3.MediaName FROM List_Wheels AS W3 WHERE W3.Id = Data_Car.StockWheelID), ";
      else if (it->second > 0)
        pinned = "(SELECT W3.MediaName FROM List_Wheels AS W3 WHERE W3.Id = " + std::to_string(it->second) + "), ";
    }
  }

  const std::string sql =
      std::string("SELECT *,List_UpgradeBrakes.Level AS BrakeLevel, List_Wheels.MediaName AS WheelMediaName, COALESCE(") +
      pinned +
      "(SELECT R.MediaName FROM List_Wheels AS R WHERE R.MediaName = List_Wheels.MediaName || '_rightside' LIMIT 1), "
      "List_Wheels.MediaName) AS RearWheelMediaName";
  if (sql.size() + 1 > kHeadBufSize) return;

  const uint32_t i = g_head_next.fetch_add(1) % kHeadBufCount;
  if (!g_head_bufs[i]) g_head_bufs[i] = GuestAlloc(kHeadBufSize);
  if (!g_head_bufs[i]) return;
  GuestWrite(g_head_bufs[i], sql);
  r4.u64 = g_head_bufs[i];
  t_head_applied = true;
}

// 0x823D5E48 (after "mr r4,r30") in sub_823D4C40: r4 = the column name used
// for the rear block's wheel name ("WheelMediaName"). Only switched when our
// SELECT head (with the extra column) is in use for this build.
void StaggerRearColumn(PPCRegister& r4) {
  if (!t_head_applied) return;
  t_head_applied = false;
  if (!g_str_rear_column) {
    static const std::string name = "RearWheelMediaName";
    const uint32_t a = GuestAlloc(uint32_t(name.size()) + 1);
    if (!a) return;
    GuestWrite(a, name);
    g_str_rear_column = a;
  }
  r4.u64 = g_str_rear_column;
}

// 0x823D5E24 / 0x823D5E6C in sub_823D4C40: r3 = the front / rear wheel name
// just read from the query, r16 = car Id. Remembered for the shop HUD.
void StaggerFrontNameSeen(PPCRegister& r3, PPCRegister& r16) {
  if (!PinEnabled()) return;
  std::string name;
  if (!ReadCString(r3.u32, name, 128)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (r16.u32 == g_player_car) g_front_name = name;
}
void StaggerRearNameSeen(PPCRegister& r3, PPCRegister& r16) {
  if (!PinEnabled()) return;
  std::string name;
  if (!ReadCString(r3.u32, name, 128)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (r16.u32 == g_player_car) g_rear_name = name;
}

// ===========================================================================
// B. Rear rim model in the second model slot (sub_8290A860)
// ===========================================================================

// 0x8290AE30 ("mr r4,r25"), start of the "_rightside" path build: r26 = car
// presentation, r29 = its front name std::string (desc+1696).
void StaggerRearModelName(PPCRegister& r26, PPCRegister& r29) {
  t_redirect = false;
  if (!Enabled()) return;
  static const bool strings_ok = StringAt(kStrBackslash, "\\") && StringAt(kStrSlodDir, "_SLOD\\") &&
                                 StringAt(kStrCarbin, ".carbin") && StringAt(kStrSCarbin, "_s.carbin");
  if (!strings_ok) {
    static bool warned = false;
    if (!warned) { warned = true; REXLOG_WARN("Staggered wheels: path strings not found, rear models off"); }
    return;
  }
  std::string front, rear;
  if (!ReadStdString(r26.u32 + kDescFrontName, front) || !ReadStdString(r26.u32 + kDescRearName, rear)) return;
  if (rear.empty() || rear == front) return;
  r29.u64 = r26.u32 + kDescRearName;
  t_redirect = true;
  REXLOG_INFO("Staggered wheels: rear rim model {} (front {})", rear, front);
}

// Suffixes of the second model path (all after their "addi r5,...").
// 0x8290AE48 "_rightside\\"      -> "\\"
// 0x8290AEC4 "_rightside_SLOD\\" -> "_SLOD\\"
// 0x8290AF30 "_rightside.carbin" -> ".carbin"
// 0x8290AF44 "_rightside_s.carbin" -> "_s.carbin" (last one, ends the redirect)
void StaggerRearDirSuffix(PPCRegister& r5) { if (t_redirect) r5.u64 = kStrBackslash; }
void StaggerRearSlodDirSuffix(PPCRegister& r5) { if (t_redirect) r5.u64 = kStrSlodDir; }
void StaggerRearModelSuffix(PPCRegister& r5) { if (t_redirect) r5.u64 = kStrCarbin; }
void StaggerRearSlodModelSuffix(PPCRegister& r5) {
  if (t_redirect) r5.u64 = kStrSCarbin;
  t_redirect = false;
}

// ===========================================================================
// C. Per-wheel model pointers (sub_82909730, rim load callback)
// ===========================================================================

// 0x829097EC ("li r3,1", reached by every path): r29 = load state, r30 =
// render object, r31 = render object + 2368, r10 / r11 = first / second model
// slot (valid in state 2). The second slot holds the rear rim when its file
// existed (flag byte r30+39280 = 1).
void StaggerWheelPointers(PPCRegister& r10, PPCRegister& r11, PPCRegister& r29, PPCRegister& r30,
                          PPCRegister& r31) {
  if (!Enabled() || r29.u32 != 2) return;
  const uint32_t obj = r30.u32;
  if (!Readable(obj + kRightsideFlag, 1) || !Load8(obj + kRightsideFlag)) return;
  std::string front, rear;
  const uint32_t desc = obj + kDescOffset;
  if (!ReadStdString(desc + kDescFrontName, front) || !ReadStdString(desc + kDescRearName, rear)) return;
  if (rear.empty() || rear == front) return;
  if (!Readable(r31.u32 + kWheelPtrs, 16)) return;
  Store32(r31.u32 + kWheelPtrs + 0, r10.u32);   // front left
  Store32(r31.u32 + kWheelPtrs + 4, r10.u32);   // front right
  Store32(r31.u32 + kWheelPtrs + 8, r11.u32);   // rear right
  Store32(r31.u32 + kWheelPtrs + 12, r11.u32);  // rear left
}

// ===========================================================================
// D. Rear-rim picker in the upgrade shop
// ===========================================================================
namespace {
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
bool PadRightStickClick() {
#ifdef _WIN32
  for (DWORD i = 0; i < 4; ++i) {
    XINPUT_STATE st{};
    if (XInputGetState(i, &st) == ERROR_SUCCESS && (st.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB)) return true;
  }
#endif
  return false;
}
void Beep(bool ok) {
#ifdef _WIN32
  MessageBeep(ok ? MB_OK : MB_ICONHAND);
#else
  (void)ok;
#endif
}
bool g_prev_down = false;
}  // namespace

// 0x82612AE4 in the shop update (sub_82612AD0), after "mr r31,r3": runs every
// frame while the upgrade shop is open (game thread).
void StaggerShopFrame(PPCRegister& r31) {
  if (!PinEnabled() || !r31.u32) return;
  g_shop_seen_ms.store(NowMs());

#ifdef _WIN32
  const bool f2 = GameHasFocus() && KeyDown(VK_F2);
  const bool shift = KeyDown(VK_SHIFT);
#else
  const bool f2 = false, shift = false;
#endif
  const bool down = f2 || PadRightStickClick();
  const bool pressed = down && !g_prev_down;
  g_prev_down = down;
  if (!pressed) return;

  if (!Readable(kGarageGlobal, 4)) return;
  const uint32_t g = Load32(kGarageGlobal);
  if (!g || !Readable(g + kCarIdOffset, 4) || !Readable(g + kPartsOffset, kPartsSlots * 4)) return;
  const uint32_t car = Load32(g + kCarIdOffset);
  if (!car) { Beep(false); return; }

  std::string what;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadPinsLocked();
    // The rim on the front of the shop car right now: the last player build
    // of this car (a highlighted rim when the shop previews it), else the
    // fitted one.
    const int32_t shown =
        g_player_car == car ? g_front_style_shown : int32_t(Load32(g + kPartsOffset + 4 * kSlotWheelStyle));
    const int32_t pin = shown == 0 ? -1 : shown;  // 0 is not a wheel Id; treat as stock
    auto it = g_pins.find(car);
    if (shift || (it != g_pins.end() && it->second == pin)) {
      if (it != g_pins.end()) g_pins.erase(it);
      what = "rear rim matches front";
    } else {
      g_pins[car] = pin;
      what = pin == -1 ? "rear rim = stock wheel" : "rear rim = wheel " + std::to_string(pin);
    }
    SavePinsLocked();
  }
  REXLOG_INFO("Staggered wheels: car {}: {}", car, what);
  Beep(true);
  UpgradePreviewRequestReload();
}

// ---- HUD ------------------------------------------------------------------
namespace {
class StaggerHud : public rex::ui::ImGuiDialog {
 public:
  explicit StaggerHud(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (NowMs() - g_shop_seen_ms.load() > 300) return;  // shop not open
    std::string front, rear;
    bool pinned = false;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      front = g_front_name;
      rear = g_rear_name;
      pinned = g_player_car && g_pins.count(g_player_car);
    }
    const float scale = std::max(0.6f, io.DisplaySize.y / 1080.0f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.02f, io.DisplaySize.y * 0.86f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##fm4_stagger_hud", nullptr, flags)) {
      ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f * scale);
      if (!rear.empty() && rear != front)
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "REAR RIM  %s  (%s)", rear.c_str(),
                           pinned ? "your pick" : "staggered set");
      else
        ImGui::TextDisabled("REAR RIM  matches front");
      ImGui::TextDisabled("F2 / R-stick click: put the rim shown on the front onto the rear");
      ImGui::PopFont();
    }
    ImGui::End();
  }
};
}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateStaggerHud(rex::ui::ImGuiDrawer* drawer) {
  if (!drawer || !PinEnabled()) return nullptr;
  return std::make_unique<StaggerHud>(drawer);
}
