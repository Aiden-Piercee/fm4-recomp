// ---------------------------------------------------------------------------
// "OPTIONS & MODS" main-menu tile (Forza-4-Evolution).
// Research: claude/main-menu-mods-tile-research-2026-10-13.md
//
// Adds a 6th tile to the top-level main menu (screen 120_P_PRE_SPLASH, class
// ForzaUI::CSplash), in the slot the game keeps for its hidden disc-2 tile.
// The tile opens a submenu, like Free Play does:
//
//   id 10  MODS                  -> ImGui overlay: installed packs + features
//   id 11  ASSISTS & DIFFICULTY  -> game screen 59  (300_I_OPT_ASSISTS_DIFFICULTY)
//   id 12  CONTROLLER            -> game screen 41  (831_C_OPT_CONTROLLER)
//   id 13  AUDIO                 -> game screen 13  (837_I_AUDIO_OPTIONS)
//   id 14  CREDITS               -> game screen 226 (834_CREDITS)
//   (id 9 is the tile itself; it is a submenu and never reaches the handler)
//
// Hooks (fm4_hooks.toml):
//   ModsMenuBuild    0x825EA8B4  inside the menu builder 0x825EA638, after the
//                                top-level tiles (r31 = CSplash screen,
//                                [r31+356] = menu). Adds our tile + children.
//   ModsMenuSelect   0x8260802C  inside the select handler 0x82607FF0
//                                (r30 = item id, r31 = screen). The game's own
//                                switch ignores ids > 8, so ours are free.
//   ModsMenuInput    0x82617698  CSplash input handler entry; returns early
//                                (input swallowed) while the overlay is open.
//   ModsMenuText     0x823804E8  localized-string GetText (vtable 0x8200256C
//                                slot 5, a leaf function); returns our UTF-16BE
//                                labels for our private string ids, so no
//                                string-table pack is needed.
//
// Guest functions used:
//   0x822B1A18 (out, table, id)        make a localized-string handle
//   0x82479318 (dst, src)              copy a handle (AddRef)
//   0x82675768 (menu, &label, id, enabled, poster)   add leaf item
//   0x826756B0 (menu, &label, id, enabled, poster)   add submenu item -> child menu
//   0x822B9418 ()                      screen-flow manager; vfunc +64
//                                      (mgr, [screen+276], screen, 0, -1) opens
//                                      a screen (as CSplash/CMainMenu do)
//
// FM4_MODS_MENU=0 turns everything off.
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
#endif

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include <rex/ppc.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/thread_state.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

namespace {

// ---- guest addresses ------------------------------------------------------
constexpr uint32_t kFnMakeLocString = 0x822B1A18;
constexpr uint32_t kFnCopyHandle = 0x82479318;
constexpr uint32_t kFnAddLeaf = 0x82675768;
constexpr uint32_t kFnAddSubmenu = 0x826756B0;
constexpr uint32_t kFnFlowManager = 0x822B9418;
constexpr uint32_t kStrTableMain = 0x8200A350;   // "Main" (same table the builder uses)
constexpr uint32_t kPosterOptions = 0x82046E54;  // "...\Splash\MENU_POSTER_OPTIONS.tga"
constexpr uint32_t kOffMenu = 356;                // CSplash: top-level menu object
constexpr uint32_t kOffFlowArg = 276;             // CSplash: first arg of flow vfunc +64

// ---- our menu --------------------------------------------------------------
// Private string ids. Only the GetText hook ever sees them.
constexpr uint32_t kTextIdBase = 0xF4D0E000;

struct Entry {
  uint32_t id;
  const char* label;
  int32_t screen;  // game screen to open, -1 = our overlay, -2 = submenu tile
};
constexpr Entry kTile = {9, "OPTIONS & MODS", -2};
constexpr Entry kChildren[] = {
    {10, "MODS", -1},
    {11, "ASSISTS & DIFFICULTY", 59},
    {12, "CONTROLLER", 41},
    {13, "AUDIO", 13},
    {14, "CREDITS", 226},
};
constexpr size_t kNumLabels = 1 + sizeof(kChildren) / sizeof(kChildren[0]);

// Guest scratch: handles + UTF-16BE label texts.
constexpr uint32_t kScratchSize = 0x800;
constexpr uint32_t kOffHandleA = 0x000;
constexpr uint32_t kOffHandleB = 0x010;
constexpr uint32_t kOffText = 0x040;   // kNumLabels x kTextSlot
constexpr uint32_t kTextSlot = 0x60;   // 47 UTF-16 chars + NUL

uint32_t g_scratch = 0;
bool g_faulted = false;
std::atomic<bool> g_overlay_open{false};
std::atomic<int64_t> g_block_until_ms{0};  // keep swallowing input briefly after closing

bool Enabled() {
  static const bool on = [] {
    const char* v = std::getenv("FM4_MODS_MENU");
    return !(v && v[0] == '0');
  }();
  return on;
}

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
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
uint32_t TextAddr(size_t index) { return g_scratch + kOffText + uint32_t(index) * kTextSlot; }

bool EnsureScratch() {
  if (g_scratch) return true;
  g_scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(kScratchSize);
  if (!g_scratch) return false;
  std::memset(REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(g_scratch), 0, kScratchSize);
  WriteWide(TextAddr(0), kTile.label, kTextSlot / 2);
  for (size_t i = 0; i + 1 < kNumLabels; ++i) WriteWide(TextAddr(i + 1), kChildren[i].label, kTextSlot / 2);
  return true;
}

// Guest call helper: same protocol as dev_keys.cpp (runs on the hooked
// thread's own context, stack moved 0x400 below the hooked frame, guest
// reentries resumed here, whole context restored afterwards).
class Guest {
 public:
  explicit Guest(PPCRegister& r1) {
    auto* ts = rex::runtime::ThreadState::Get();
    ctx_ = ts ? ts->context() : nullptr;
    if (!ctx_ || &ctx_->r1 != &r1) {
      REXLOG_WARN("Mods menu: hook is not running on the thread's own context; skipped");
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
      REXLOG_WARN("Mods menu: guest call {:08X} could not finish (stuck at {:08X})", target, next);
      return 0;
    }
    return result;
  }
  uint32_t VCall(uint32_t obj, uint32_t offset, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0,
                 uint32_t d = 0) {
    if (!ok_ || !obj) { ok_ = false; return 0; }
    return Call(Load32(Load32(obj) + offset), obj, a, b, c, d);
  }

 private:
  PPCContext* ctx_ = nullptr;
  uint32_t sp_ = 0;
  bool ok_ = true;
  alignas(64) static inline unsigned char saved_[16384];
};

// Adds one item; returns the child menu for a submenu item (0 for a leaf).
uint32_t AddItem(Guest& g, uint32_t menu, size_t label_index, uint32_t id, bool submenu) {
  const uint32_t ha = g_scratch + kOffHandleA, hb = g_scratch + kOffHandleB;
  Store32(ha, 0);
  Store32(hb, 0);
  g.Call(kFnMakeLocString, ha, kStrTableMain, kTextIdBase + uint32_t(label_index));
  g.Call(kFnCopyHandle, hb, ha);  // AddItem consumes (releases) this copy
  const uint32_t r = g.Call(submenu ? kFnAddSubmenu : kFnAddLeaf, menu, hb, id, 1, kPosterOptions);
  if (const uint32_t obj = Load32(ha)) g.VCall(obj, 8);  // release our own reference
  return submenu ? r : 0;
}

void OpenGameScreen(Guest& g, uint32_t screen_obj, uint32_t screen) {
  const uint32_t mgr = g.Call(kFnFlowManager);
  if (!g.ok() || !mgr) return;
  g.VCall(mgr, 64, Load32(screen_obj + kOffFlowArg), screen, 0, 0xFFFFFFFFu);
}

}  // namespace

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

void ModsMenuBuild(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr, PPCRegister& r31) {
  (void)r13; (void)fpscr;
  if (!Enabled() || g_faulted || !EnsureScratch()) return;
  const uint32_t menu = Load32(r31.u32 + kOffMenu);
  if (!menu) return;
  Guest g(r1);
  if (!g.ok()) return;
  const uint32_t child = AddItem(g, menu, 0, kTile.id, true);
  if (!g.ok() || !child) {
    REXLOG_WARN("Mods menu: could not add the tile (menu {:08X})", menu);
    return;
  }
  for (size_t i = 0; i + 1 < kNumLabels && g.ok(); ++i) AddItem(g, child, i + 1, kChildren[i].id, false);
  REXLOG_INFO("Mods menu: tile added (menu {:08X}, submenu {:08X}){}", menu, child,
              g.ok() ? "" : " - a child item failed");
}

void ModsMenuSelect(PPCRegister& r1, PPCRegister& r13, PPCFPSCRRegister& fpscr, PPCRegister& r30,
                    PPCRegister& r31) {
  (void)r13; (void)fpscr;
  const uint32_t id = r30.u32;
  REXLOG_INFO("Mods menu: main-menu item {} selected", id);
  if (!Enabled() || g_faulted) return;
  for (const Entry& e : kChildren) {
    if (e.id != id) continue;
    if (e.screen == -1) {
      g_overlay_open = true;
      REXLOG_INFO("Mods menu: overlay opened");
    } else if (e.screen >= 0) {
      Guest g(r1);
      if (g.ok()) OpenGameScreen(g, r31.u32, uint32_t(e.screen));
    }
    return;
  }
}

// Returns true (skip the game's input handler) while the overlay is open.
bool ModsMenuInput(PPCRegister& r3) {
  if (!g_overlay_open && NowMs() >= g_block_until_ms.load()) return false;
  r3.u64 = 1;  // reported as handled
  return true;
}

// Localized-string GetText. True = we answered (r3 = UTF-16BE text).
bool GarageMenuText(PPCRegister& r3);  // garage_menu.cpp: BRAND CENTRAL / USED CAR DEALERSHIP
bool BuyStageUserEnabled();            // buy_stage.cpp: Buy Cars 3D stage toggle
void BuyStageSetUserEnabled(bool on);
bool GarageMenuUserEnabled();          // garage_menu.cpp: Brand Central / Used Car Dealership toggle
void GarageMenuSetUserEnabled(bool on);
bool EngineReliabilityEnabled();       // overheat.cpp: engine reliability toggle
void EngineReliabilitySetEnabled(bool on);
bool EvoUnlockEnabled();               // dev_keys.cpp: Evolution decade unlock + HUD switches
void EvoUnlockSetEnabled(bool on);
bool EvoContractEnabled();             // dev_keys.cpp: contract-out switch
void EvoContractSetEnabled(bool on);
void EvoContractOut(int decade);
int EvoContractLastResult();
int EvoContractOwed();
bool EvoHudVisible();
void EvoHudSetVisible(bool on);
bool EvoIntroUserEnabled();            // dev_keys.cpp: Evolution intro popup
void EvoIntroSetUserEnabled(bool on);
void EvoIntroShowAgain();
bool EvoIntroShowAgainPending();
bool PrizeNoLevelGiftsEnabled();       // hooks.cpp: Game Levelling switches
void PrizeNoLevelGiftsSetEnabled(bool on);
bool PrizeEventCarsEnabled();
void PrizeEventCarsSetEnabled(bool on);
bool PrizeMilestonesEnabled();
void PrizeMilestonesSetEnabled(bool on);
bool PrizeOnlyFilterEnabled();
void PrizeOnlyFilterSetEnabled(bool on);
bool PrizeNoSellEnabled();
void PrizeNoSellSetEnabled(bool on);
bool PartTuneEnabled();                // part_tune.cpp: Performance Parts switches
void PartTuneSetEnabled(bool on);
bool PartWearEnabled();
void PartWearSetEnabled(bool on);
bool UsedLotRotationUserEnabled();
void UsedLotSetRotationUserEnabled(bool on);
bool ShopEraLockEnabled();
void ShopEraLockSetEnabled(bool on);
bool ShopRareEnabled();
void ShopRareSetEnabled(bool on);
bool ShopStockVaryEnabled();
void ShopStockVarySetEnabled(bool on);
bool UsedLotMileageNamesEnabled();
void UsedLotMileageNamesSetEnabled(bool on);
bool UsedLotOverlayEnabled();
void UsedLotOverlaySetEnabled(bool on);
bool UsedLotRevealEnabled();
void UsedLotRevealSetEnabled(bool on);
bool ModsMenuText(PPCRegister& r3) {
  if (GarageMenuText(r3)) return true;
  if (!g_scratch) return false;
  const uint32_t id = Load32(r3.u32 + 8);
  if (id < kTextIdBase || id >= kTextIdBase + kNumLabels) return false;
  r3.u64 = TextAddr(id - kTextIdBase);
  return true;
}

// ---------------------------------------------------------------------------
// Overlay: installed packs (read-only explorer) + feature switches.
// ---------------------------------------------------------------------------
namespace {

struct ZipInfo {
  std::string name;
  int priority = -1;
  std::string mount;  // from zipmount.xml, empty = package root mount
};
struct PackInfo {
  std::string folder;
  std::string name;
  std::string offer;
  std::vector<ZipInfo> zips;
  size_t loose_files = 0;
  bool overrides_game = false;  // any zip with priority >= 27000
};
struct Scan {
  std::vector<PackInfo> packs;
  std::map<std::string, std::vector<std::string>> shared_mounts;  // mount -> pack names (2+)
  std::string root;
};

std::string ReadHeaderName(const std::filesystem::path& header) {
  std::ifstream f(header, std::ios::binary);
  std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
  std::string s;
  for (size_t i = 8; i + 1 < b.size() && i < 8 + 0x100; i += 2) {
    const unsigned c = (unsigned(b[i]) << 8) | b[i + 1];
    if (!c) break;
    s += c < 0x80 ? char(c) : '?';
  }
  return s;
}

Scan ScanPacks(const std::filesystem::path& user_root) {
  namespace fs = std::filesystem;
  Scan scan;
  const fs::path content = user_root / "0000000000000000" / "4D530910" / "00000002";
  const fs::path headers = user_root / "0000000000000000" / "4D530910" / "Headers" / "00000002";
  scan.root = content.string();
  std::error_code ec;
  if (!fs::is_directory(content, ec)) return scan;
  static const std::regex pri_re(R"re(_pri_(\d+)\.zip$)re", std::regex::icase);
  static const std::regex mount_re(R"re(<zip\s+Name="([^"]+)"\s+Mount="([^"]+)")re", std::regex::icase);
  std::map<std::string, std::vector<std::string>> mounts;
  for (const auto& dir : fs::directory_iterator(content, ec)) {
    if (!dir.is_directory()) continue;
    PackInfo p;
    p.folder = dir.path().filename().string();
    p.name = ReadHeaderName(headers / (p.folder + ".header"));
    if (p.name.empty()) p.name = p.folder;
    const fs::path media = dir.path() / "Media";
    for (const auto& f : fs::directory_iterator(media, ec)) {
      if (f.path().extension() == ".puboffer") p.offer = f.path().stem().string();
    }
    std::map<std::string, std::string> zip_mounts;
    {
      std::ifstream zm(media / "DLCZips" / "zipmount.xml");
      std::string line;
      while (std::getline(zm, line)) {
        std::smatch m;
        if (std::regex_search(line, m, mount_re)) zip_mounts[m[1]] = m[2];
      }
    }
    for (const auto& f : fs::directory_iterator(media / "DLCZips", ec)) {
      if (f.path().extension() != ".zip") continue;
      ZipInfo z;
      z.name = f.path().filename().string();
      std::smatch m;
      if (std::regex_search(z.name, m, pri_re)) z.priority = std::atoi(m[1].str().c_str());
      if (auto it = zip_mounts.find(z.name); it != zip_mounts.end()) z.mount = it->second;
      if (z.priority >= 27000) p.overrides_game = true;
      if (!z.mount.empty()) mounts[z.mount].push_back(p.name);
      p.zips.push_back(std::move(z));
    }
    for (auto it = fs::recursive_directory_iterator(media, ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
      if (ec) break;
      if (it->is_directory() && it->path().filename() == "DLCZips") {  // zips listed above
        it.disable_recursion_pending();
        continue;
      }
      if (it->is_regular_file() && it->path().extension() != ".puboffer") ++p.loose_files;
    }
    scan.packs.push_back(std::move(p));
  }
  std::sort(scan.packs.begin(), scan.packs.end(),
            [](const PackInfo& a, const PackInfo& b) { return a.name < b.name; });
  for (auto& [mount, names] : mounts)
    if (names.size() > 1) scan.shared_mounts[mount] = names;
  return scan;
}

struct Feature {
  const char* env;
  const char* what;
};
constexpr Feature kFeatures[] = {
    {"FM4_PRIZE_CARS", "Prize car for completing an event"},
    {"FM4_EVENT_GRID_SCROLL", "Event List scrolling (more than 310 events)"},
    {"FM4_DEV_KEYS", "Developer keys (F6/F8/F9/F10/F11)"},
    {"FM4_AI_DRIVER_POOL", "Real AI drivers"},
    {"FM4_UPGRADE_AUDIO", "Upgrade-dependent engine audio"},
    {"FM4_EXHAUST_PREVIEW", "Exhaust upgrade car view"},
    {"FM4_EXHAUST_PARTS", "Per-tier exhaust parts"},
    {"FM4_OVERHEAT", "Engine overheating (0 off, 1 probe, 2 model)"},
    {"FM4_OVERHEAT_HUD", "Overheat HUD readout"},
    {"FM4_MAX_CARS", "Cars on track"},
    {"FM4_STAGGERED_WHEELS", "Separate front and rear rims"},
    {"FM4_STAGGERED_WHEELS_PIN", "Rear rim picker in the upgrade shop (F2 / R-stick)"},
    {"FM4_MODS_MENU", "This menu"},
};

class ModsOverlay : public rex::ui::ImGuiDialog {
 public:
  ModsOverlay(rex::ui::ImGuiDrawer* drawer, std::filesystem::path user_root)
      : rex::ui::ImGuiDialog(drawer), user_root_(std::move(user_root)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!g_overlay_open) { was_open_ = false; return; }
    if (!was_open_) {  // just opened: rescan, swallow the A press that opened us
      scan_ = ScanPacks(user_root_);
      selected_ = 0;
      tab_ = 0;
      prev_buttons_ = 0xFFFF;
      was_open_ = true;
      REXLOG_INFO("Mods menu: {} packs found in {}", scan_.packs.size(), scan_.root);
    }
    const uint16_t pressed = PollPad();
    if ((pressed & kPadB) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { CloseOverlay(); return; }
    if (pressed & (kPadLB | kPadRB)) tab_ ^= 1;
    if (tab_ == 1 && (pressed & kPadA)) BuyStageSetUserEnabled(!BuyStageUserEnabled());
    if (!scan_.packs.empty()) {
      const int n = int(scan_.packs.size());
      if (pressed & kPadDown) { selected_ = (selected_ + 1) % n; scroll_to_selected_ = true; }
      if (pressed & kPadUp) { selected_ = (selected_ + n - 1) % n; scroll_to_selected_ = true; }
    }

    const ImVec2 size(io.DisplaySize.x * 0.8f, io.DisplaySize.y * 0.8f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.1f, io.DisplaySize.y * 0.1f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    const float scale = std::max(1.0f, io.DisplaySize.y / 1080.0f);
    bool keep = true;
    if (ImGui::Begin("MODS##fm4_mods", &keep,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
      ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f * scale);
      if (ImGui::Button(tab_ == 0 ? "[ INSTALLED PACKS ]" : "  INSTALLED PACKS  ")) tab_ = 0;
      ImGui::SameLine();
      if (ImGui::Button(tab_ == 1 ? "[ FEATURES ]" : "  FEATURES  ")) tab_ = 1;
      ImGui::SameLine();
      ImGui::TextDisabled("   LB/RB switch tab   D-pad select   A toggle (Features)   B / Esc close");
      ImGui::Separator();
      if (tab_ == 0) DrawPacks(size); else DrawFeatures();
      ImGui::PopFont();
    }
    ImGui::End();
    if (!keep) CloseOverlay();
  }

 private:
  static constexpr uint16_t kPadUp = 0x0001, kPadDown = 0x0002, kPadLB = 0x0100, kPadRB = 0x0200,
                            kPadA = 0x1000, kPadB = 0x2000;

  // Newly pressed pad buttons (any controller), XInput bit layout.
  uint16_t PollPad() {
    uint16_t now = 0;
#ifdef _WIN32
    for (DWORD i = 0; i < 4; ++i) {
      XINPUT_STATE st{};
      if (XInputGetState(i, &st) == ERROR_SUCCESS) {
        now |= st.Gamepad.wButtons;
        if (st.Gamepad.sThumbLY > 16000) now |= kPadUp;
        if (st.Gamepad.sThumbLY < -16000) now |= kPadDown;
      }
    }
#endif
    const uint16_t pressed = now & ~prev_buttons_;
    prev_buttons_ = now;
    return pressed;
  }

  void CloseOverlay() {
    g_overlay_open = false;
    was_open_ = false;
    g_block_until_ms = NowMs() + 400;  // the B press must not also reach the menu
    REXLOG_INFO("Mods menu: overlay closed");
  }

  void DrawPacks(const ImVec2& size) {
    if (scan_.packs.empty()) {
      ImGui::Text("No packs found in %s", scan_.root.c_str());
      return;
    }
    ImGui::Text("%zu packs installed", scan_.packs.size());
    if (!scan_.shared_mounts.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "   %zu folders overridden by more than one pack",
                         scan_.shared_mounts.size());
    }
    const float list_w = size.x * 0.38f;
    ImGui::BeginChild("##packs", ImVec2(list_w, 0), ImGuiChildFlags_Borders);
    for (int i = 0; i < int(scan_.packs.size()); ++i) {
      const PackInfo& p = scan_.packs[i];
      if (p.overrides_game) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
      if (ImGui::Selectable((p.name + "##" + p.folder).c_str(), selected_ == i)) selected_ = i;
      if (p.overrides_game) ImGui::PopStyleColor();
      if (selected_ == i && scroll_to_selected_) { ImGui::SetScrollHereY(0.5f); scroll_to_selected_ = false; }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##details", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const PackInfo& p = scan_.packs[std::clamp(selected_, 0, int(scan_.packs.size()) - 1)];
    ImGui::TextWrapped("%s", p.name.c_str());
    ImGui::TextDisabled("Folder  %s", p.folder.c_str());
    ImGui::Text("Offer   %s", p.offer.empty() ? "-" : p.offer.c_str());
    ImGui::Text("Loose files  %zu", p.loose_files);
    if (p.overrides_game)
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Overrides base game files (priority 27000+)");
    ImGui::Separator();
    ImGui::Text("Zips (%zu)", p.zips.size());
    for (const ZipInfo& z : p.zips) {
      ImGui::BulletText("%s", z.name.c_str());
      if (!z.mount.empty()) {
        const bool shared = scan_.shared_mounts.count(z.mount) != 0;
        ImGui::SameLine();
        if (shared) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "-> %s (shared)", z.mount.c_str());
        else ImGui::TextDisabled("-> %s", z.mount.c_str());
      }
    }
    for (const ZipInfo& z : p.zips) {
      auto it = scan_.shared_mounts.find(z.mount);
      if (z.mount.empty() || it == scan_.shared_mounts.end()) continue;
      ImGui::Separator();
      ImGui::TextWrapped("%s is also mounted by:", z.mount.c_str());
      for (const std::string& other : it->second)
        if (other != p.name) ImGui::BulletText("%s", other.c_str());
    }
    ImGui::EndChild();
  }

  void DrawFeatures() {
    bool stage = BuyStageUserEnabled();
    if (ImGui::Checkbox("Buy Cars 3D stage (background and car view; off = classic Buy Cars)", &stage))
      BuyStageSetUserEnabled(stage);
    ImGui::TextDisabled("Applies the next time the car list opens. A on the pad also toggles it. "
                        "F4 > Forza Motorsport 4 > Buy Cars > Save to config keeps it for next launch.");
    bool garage = GarageMenuUserEnabled();
    if (ImGui::Checkbox("Brand Central / Used Car Dealership in the CARS menu (off = stock Buy Cars; next menu open)", &garage))
      GarageMenuSetUserEnabled(garage);
    bool rel = EngineReliabilityEnabled();
    if (ImGui::Checkbox("Engine reliability (heat, wear, faults; damage only in Simulation damage mode)", &rel))
      EngineReliabilitySetEnabled(rel);
    bool gifts = PrizeNoLevelGiftsEnabled();
    if (ImGui::Checkbox("Evolution: no gift car on level-up (live)", &gifts)) PrizeNoLevelGiftsSetEnabled(gifts);
    bool evcars = PrizeEventCarsEnabled();
    if (ImGui::Checkbox("Evolution: prize car for completing an event (live)", &evcars)) PrizeEventCarsSetEnabled(evcars);
    bool miles = PrizeMilestonesEnabled();
    if (ImGui::Checkbox("Evolution: car at driver level 10/20/30/40/50 (live)", &miles)) PrizeMilestonesSetEnabled(miles);
    bool intro = EvoIntroUserEnabled();
    if (ImGui::Checkbox("Evolution: intro popup the first time the Event List opens (live)", &intro))
      EvoIntroSetUserEnabled(intro);
    ImGui::SameLine();
    if (EvoIntroShowAgainPending()) ImGui::TextDisabled("[ shows on the next Event List open ]");
    else if (ImGui::SmallButton("Show intro again")) EvoIntroShowAgain();
    bool pfilt = PrizeOnlyFilterEnabled();
    if (ImGui::Checkbox("Evolution: hide prize-only works cars from Buy Cars / used lot (next list open)", &pfilt))
      PrizeOnlyFilterSetEnabled(pfilt);
    bool sell0 = PrizeNoSellEnabled();
    if (ImGui::Checkbox("Evolution: prize cars and works variants sell for 0 credits (live)", &sell0)) PrizeNoSellSetEnabled(sell0);
    bool ptune = PartTuneEnabled();
    if (ImGui::Checkbox("Part tuning character (brand/size power-curve shape)", &ptune)) PartTuneSetEnabled(ptune);
    bool pwear = PartWearEnabled();
    if (ImGui::Checkbox("Engine wear (age/mileage power loss)", &pwear)) PartWearSetEnabled(pwear);
    bool ulot = UsedLotRotationUserEnabled();
    if (ImGui::Checkbox("Rotating used lot (new stock after each finished event; applies at the next roll)", &ulot)) UsedLotSetRotationUserEnabled(ulot);
    bool shop_era = ShopEraLockEnabled();
    if (ImGui::Checkbox("Shop: era-locked parts (higher tiers and later-decade part types appear as decades open; next pass)", &shop_era)) ShopEraLockSetEnabled(shop_era);
    bool shop_rare = ShopRareEnabled();
    if (ImGui::Checkbox("Shop: rare parts (only some are on sale at a time)", &shop_rare)) ShopRareSetEnabled(shop_rare);
    bool shop_vary = ShopStockVaryEnabled();
    if (ImGui::Checkbox("Shop: stock varies after finished events (rare parts re-roll; sparse / normal / flooded days)", &shop_vary)) ShopStockVarySetEnabled(shop_vary);
    bool lot_names = UsedLotMileageNamesEnabled();
    if (ImGui::Checkbox("Used lot: mileage in the listing name (English; next list open)", &lot_names)) UsedLotMileageNamesSetEnabled(lot_names);
    bool ulov = UsedLotOverlayEnabled();
    if (ImGui::Checkbox("Used lot: status box next to the price (mileage, owners, condition)", &ulov)) UsedLotOverlaySetEnabled(ulov);
    bool ulrev = UsedLotRevealEnabled();
    if (ImGui::Checkbox("Used lot: reveal hidden condition before buying", &ulrev)) UsedLotRevealSetEnabled(ulrev);
    bool evo_unlock = EvoUnlockEnabled();
    if (ImGui::Checkbox("Evolution decade unlock (next decade opens after a championship + 4 events; off = columns stay as they are)", &evo_unlock))
      EvoUnlockSetEnabled(evo_unlock);
    bool evo_contract = EvoContractEnabled();
    if (ImGui::Checkbox("Evolution contract-out (skip a decade, repay 50% of winnings)", &evo_contract))
      EvoContractSetEnabled(evo_contract);
    if (evo_contract) {
      static const char* const kDecades[] = {"1960s", "1970s", "1980s", "1990s", "2000s", "2010s"};
      static const int kFees[] = {10000, 15000, 20000, 30000, 40000, 50000};
      static int confirm = -1;
      static bool requested = false;  // LastResult is 0 until the first request, so show status only after one
      ImGui::TextUnformatted("  Skip a decade (fee is taken from future winnings):");
      for (int d = 0; d < 6; ++d) {
        ImGui::SameLine();
        char label[48];
        std::snprintf(label, sizeof(label), "%s##skip%d", kDecades[d], d);
        if (ImGui::SmallButton(label)) confirm = d;
      }
      if (confirm >= 0) {
        ImGui::Text("  Skip the %s for $%d, repaid as 50%% of winnings?", kDecades[confirm], kFees[confirm]);
        ImGui::SameLine();
        if (ImGui::SmallButton("Sign")) { EvoContractOut(confirm); requested = true; confirm = -1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) confirm = -1;
      }
      const int res = EvoContractLastResult();
      if (!requested) {}
      else if (res == 0) ImGui::TextUnformatted("  Signing ...");
      else if (res == 1) ImGui::Text("  Signed, owe $%d", EvoContractOwed());
      else if (res == 2) ImGui::TextUnformatted("  Refused (already owing, already skipped, or off)");
    }
    bool evo_hud = EvoHudVisible();
    if (ImGui::Checkbox("Evolution HUD (top-left progress readout)", &evo_hud))
      EvoHudSetVisible(evo_hud);
    ImGui::Separator();
    ImGui::TextWrapped("Feature switches are environment variables read at startup "
                       "(set them in the launcher .bat). Shown here read-only.");
    ImGui::Separator();
    if (ImGui::BeginTable("##features", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
      ImGui::TableSetupColumn("Switch");
      ImGui::TableSetupColumn("Value");
      ImGui::TableSetupColumn("Feature");
      ImGui::TableHeadersRow();
      for (const Feature& f : kFeatures) {
        const char* v = std::getenv(f.env);
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(f.env);
        ImGui::TableNextColumn();
        if (v) ImGui::TextUnformatted(v); else ImGui::TextDisabled("default");
        ImGui::TableNextColumn(); ImGui::TextUnformatted(f.what);
      }
      ImGui::EndTable();
    }
  }

  std::filesystem::path user_root_;
  Scan scan_;
  bool was_open_ = false;
  int selected_ = 0;
  int tab_ = 0;
  bool scroll_to_selected_ = false;
  uint16_t prev_buttons_ = 0xFFFF;
};

}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateModsOverlay(rex::ui::ImGuiDrawer* drawer,
                                                        const std::filesystem::path& user_root) {
  if (!drawer || !Enabled()) return nullptr;
  return std::make_unique<ModsOverlay>(drawer, user_root);
}
