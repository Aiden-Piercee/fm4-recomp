// ---------------------------------------------------------------------------
// Engine builder overlay (Forza-4-Evolution). F7 opens and closes it. Design: docs/ENGINE_BUILDER.md.
//
// Left: the ten slots with the fitted part. Middle: the parts for the selected slot that fit this engine's family and
// are on sale in the newest unlocked Evolution decade. Right: a dyno graph of torque and power, stock (grey), the
// current build (orange) and the highlighted part (blue), with peak numbers, the safe rev limit and warnings.
// Mouse and keyboard only in the MVP; the game still sees pad input while it is open. Parts are free for now.
// ---------------------------------------------------------------------------
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma comment(lib, "user32.lib")
#endif

#include "engine_builder.h"

#include <imgui.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace eb;

constexpr float kKwToHp = 1.341022f;

[[maybe_unused]] bool GameHasFocus() {
#ifdef _WIN32
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
#else
  return true;
#endif
}

bool F7Pressed() {
#ifdef _WIN32
  static bool was_down = false;
  const bool down = (GetAsyncKeyState(VK_F7) & 0x8000) != 0 && GameHasFocus();
  const bool pressed = down && !was_down;
  was_down = down;
  return pressed;
#else
  return false;
#endif
}

std::string PartLabel(const Part* p) {
  if (!p) return "Stock";
  return p->brand + " " + p->name;
}

class EngineBuilderOverlay : public rex::ui::ImGuiDialog {
 public:
  explicit EngineBuilderOverlay(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (F7Pressed()) {
      open_ = !open_;
      REXLOG_INFO("Engine builder: overlay {}", open_ ? "opened" : "closed");
    }
    if (!open_ || !BuilderEnabled()) return;

    const ImVec2 size(io.DisplaySize.x * 0.86f, io.DisplaySize.y * 0.80f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.07f, io.DisplaySize.y * 0.10f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    const float scale = std::max(1.0f, io.DisplaySize.y / 1080.0f);
    bool keep = true;
    if (ImGui::Begin("ENGINE BUILDER##fm4_engine_builder", &keep,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize)) {
      ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f * scale);
      DrawBody(size);
      ImGui::PopFont();
    }
    ImGui::End();
    if (!keep || ImGui::IsKeyPressed(ImGuiKey_Escape)) open_ = false;
  }

 private:
  void DrawBody(const ImVec2& size) {
    const Catalogue* cat = GetCatalogue();
    if (!cat) {
      ImGui::TextWrapped("No parts catalogue: %s", CatalogueSource().c_str());
      return;
    }
    const CarState car = Snapshot();
    if (car.garage_id <= 0) {
      ImGui::TextWrapped("Select a car in your garage first (the builder works on the car you are driving in career).");
      return;
    }
    if (!car.info_ok || !car.build_ok) {
      ImGui::Text("Reading garage car %d ...", car.garage_id);
      return;
    }
    const StockEngine* engine = cat->FindEngine(car.engine_id);
    if (!engine) {
      ImGui::TextWrapped("This car's engine (FM4 engine %d) is not in the builder yet. The first version covers "
                         "American pushrod V8s from 1957 to 1996.", car.engine_id);
      return;
    }
    const auto fam = cat->families.find(engine->family);
    ImGui::Text("%s", engine->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  %s  |  car %d (%d)", fam == cat->families.end() ? engine->family.c_str() : fam->second.name.c_str(),
                        car.car_id, car.year);
    DrawStatus(car);
    ImGui::Separator();

    const float h = ImGui::GetContentRegionAvail().y;
    const float slots_w = size.x * 0.24f, parts_w = size.x * 0.30f;
    ImGui::BeginChild("##eb_slots", ImVec2(slots_w, h), ImGuiChildFlags_Borders);
    DrawSlots(*cat, car);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##eb_parts", ImVec2(parts_w, h), ImGuiChildFlags_Borders);
    DrawParts(*cat, *engine, car);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##eb_dyno", ImVec2(0, h), ImGuiChildFlags_Borders);
    DrawDyno(*cat, *engine, car);
    ImGui::EndChild();
  }

  void DrawStatus(const CarState& car) {
    const bool career = LastRaceWasCareer();
    bool allow = CareerAllowed();
    if (ImGui::Checkbox("Use builds in career events", &allow)) SetCareerAllowed(allow);
    ImGui::SameLine();
    ImGui::TextDisabled("(PI and class do not account for builds yet)");
    if (career && !allow)
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Last race was a career event: the build did not apply there.");
    if (car.fm4_upgrade_scale > 1.0005 || car.fm4_cam_upgraded)
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                         "FM4 engine upgrades are fitted from the upgrade shop. Their gains are cancelled while a build is "
                         "active%s.", car.fm4_cam_upgraded ? "; the FM4 camshaft's curve shape still applies" : "");
  }

  void DrawSlots(const Catalogue& cat, const CarState& car) {
    ImGui::TextDisabled("SLOTS");
    for (int s = 0; s < kNumSlots; ++s) {
      const Part* p = car.parts[s] > 0 ? cat.FindPart(car.parts[s]) : nullptr;
      char label[256];
      std::snprintf(label, sizeof label, "%s\n  %s##slot%d", SlotName(s), PartLabel(p).c_str(), s);
      if (ImGui::Selectable(label, slot_ == s, 0, ImVec2(0, ImGui::GetTextLineHeight() * 2.2f))) {
        slot_ = s;
        preview_ = -1;
      }
    }
    ImGui::Spacing();
    if (ImGui::Button("Back to stock engine")) { ClearBuild(); preview_ = -1; }
  }

  void DrawParts(const Catalogue& cat, const StockEngine& e, const CarState& car) {
    const int year_limit = DecadeYearLimit();
    ImGui::TextDisabled("%s  %s", SlotName(slot_), year_limit > 0 ? ("(on sale to " + std::to_string(year_limit) + ")").c_str() : "");
    const auto parts = cat.PartsFor(slot_, e.family, year_limit);
    const int32_t fitted = car.parts[slot_];
    if (ImGui::Selectable(fitted == 0 ? "> Stock" : "  Stock", preview_ == 0)) preview_ = 0;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) FitPart(slot_, 0);
    for (const Part* p : parts) {
      char label[256];
      std::snprintf(label, sizeof label, "%s%s  (%d)##p%d", p->id == fitted ? "> " : "  ", PartLabel(p).c_str(), p->year, p->id);
      if (ImGui::Selectable(label, preview_ == p->id)) preview_ = p->id;
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) FitPart(slot_, p->id);
    }
    if (parts.empty()) ImGui::TextDisabled("No parts for this engine in this slot yet.");
    ImGui::Spacing();
    if (preview_ >= 0 && preview_ != fitted) {
      if (ImGui::Button("Fit this part")) { FitPart(slot_, preview_); preview_ = -1; }
      ImGui::SameLine();
      ImGui::TextDisabled("(or double-click)");
    }
    if (preview_ > 0) {
      if (const Part* p = cat.FindPart(preview_)) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", PartLabel(p).c_str());
        ImGui::Text("Year %d   Mass %+.1f kg   Price %d CR (free in this version)", p->year, p->mass_kg, p->price);
        std::string mods;
        for (const Mod& m : p->mods) {
          char buf[64];
          std::snprintf(buf, sizeof buf, "%s%s%g  ", m.key.c_str(), m.op == '=' ? "=" : m.op == '+' ? "+=" : "*=", m.value);
          mods += buf;
        }
        ImGui::TextWrapped("%s", mods.c_str());
      }
    }
  }

  void DrawDyno(const Catalogue& cat, const StockEngine& e, const CarState& car) {
    (void)cat;
    // Cache: recompute when the car, build or preview changes.
    int32_t preview_parts[kNumSlots];
    std::memcpy(preview_parts, car.parts, sizeof preview_parts);
    if (preview_ >= 0) preview_parts[slot_] = preview_;
    const bool changed = car.engine_id != cache_engine_ || std::memcmp(car.parts, cache_parts_, sizeof cache_parts_) != 0 ||
                         std::memcmp(preview_parts, cache_preview_, sizeof cache_preview_) != 0;
    if (changed) {
      cache_engine_ = car.engine_id;
      std::memcpy(cache_parts_, car.parts, sizeof cache_parts_);
      std::memcpy(cache_preview_, preview_parts, sizeof cache_preview_);
      const int32_t none[kNumSlots] = {};
      stock_ = RunFor(e, &none);
      build_ = RunFor(e, nullptr);
      prev_ = RunFor(e, &preview_parts);
    }
    const bool show_prev = std::memcmp(preview_parts, car.parts, sizeof preview_parts) != 0;

    auto stats = [](const char* name, const DynoResult& r, ImVec4 col) {
      ImGui::TextColored(col, "%-8s %4.0f Nm @ %4.0f   %4.0f hp @ %4.0f   safe %4.0f rpm%s", name, r.peak_torque_nm,
                         r.peak_torque_rpm, r.peak_power_kw * kKwToHp, r.peak_power_rpm, r.safe_rpm,
                         r.knock_margin < 0.0 ? "   KNOCK: too much compression for the fuel" : "");
    };
    const ImVec4 c_stock(0.62f, 0.62f, 0.62f, 1.0f), c_build(1.0f, 0.55f, 0.15f, 1.0f), c_prev(0.35f, 0.7f, 1.0f, 1.0f);
    stats("Stock", stock_, c_stock);
    stats("Build", build_, c_build);
    if (show_prev) stats("Preview", prev_, c_prev);

    // Graph: torque (solid) and power (thin) against rpm.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gw = avail.x, gh = std::max(120.0f, avail.y - ImGui::GetTextLineHeightWithSpacing());
    const ImVec2 a(p0.x + 40.0f, p0.y + 6.0f), b(p0.x + gw - 40.0f, p0.y + gh - 20.0f);
    dl->AddRectFilled(a, b, IM_COL32(18, 18, 20, 255));
    double rpm_hi = std::max({stock_.safe_rpm, build_.safe_rpm, show_prev ? prev_.safe_rpm : 0.0}) + 800.0;
    rpm_hi = std::clamp(std::ceil(rpm_hi / 1000.0) * 1000.0, 5000.0, stock_.rpm_max);
    double t_hi = 1.0, p_hi = 1.0;
    for (const DynoResult* r : {&stock_, &build_, &prev_}) {
      t_hi = std::max(t_hi, r->peak_torque_nm);
      p_hi = std::max(p_hi, r->peak_power_kw * kKwToHp);
    }
    t_hi *= 1.15; p_hi *= 1.15;
    for (int k = 1000; k <= int(rpm_hi); k += 1000) {
      const float x = a.x + float(k / rpm_hi) * (b.x - a.x);
      dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), IM_COL32(45, 45, 50, 255));
      char buf[16];
      std::snprintf(buf, sizeof buf, "%d", k / 1000);
      dl->AddText(ImVec2(x - 4.0f, b.y + 3.0f), IM_COL32(150, 150, 150, 255), buf);
    }
    dl->AddText(ImVec2(a.x, b.y + 3.0f), IM_COL32(150, 150, 150, 255), "x1000 rpm");
    auto curve = [&](const DynoResult& r, ImU32 col, bool power, float thick) {
      ImVec2 last;
      bool have = false;
      for (int i = 0; i < DynoResult::kPoints; ++i) {
        const double rpm = r.rpm_at(i);
        if (rpm > rpm_hi) break;
        const double v = power ? r.torque_nm[i] * rpm / 9549.3 * kKwToHp : r.torque_nm[i];
        const ImVec2 pt(a.x + float(rpm / rpm_hi) * (b.x - a.x),
                        b.y - float(std::max(0.0, v) / (power ? p_hi : t_hi)) * (b.y - a.y));
        if (have) dl->AddLine(last, pt, col, thick);
        last = pt;
        have = true;
      }
    };
    curve(stock_, IM_COL32(150, 150, 150, 255), false, 2.0f);
    curve(stock_, IM_COL32(150, 150, 150, 160), true, 1.0f);
    curve(build_, IM_COL32(255, 140, 40, 255), false, 2.5f);
    curve(build_, IM_COL32(255, 140, 40, 170), true, 1.2f);
    if (show_prev) {
      curve(prev_, IM_COL32(90, 180, 255, 255), false, 2.5f);
      curve(prev_, IM_COL32(90, 180, 255, 170), true, 1.2f);
    }
    char ax[48];
    std::snprintf(ax, sizeof ax, "%.0f Nm", t_hi);
    dl->AddText(ImVec2(p0.x, a.y), IM_COL32(170, 170, 170, 255), ax);
    std::snprintf(ax, sizeof ax, "%.0f hp", p_hi);
    dl->AddText(ImVec2(b.x + 2.0f, a.y), IM_COL32(170, 170, 170, 255), ax);
    ImGui::Dummy(ImVec2(gw, gh));
    ImGui::TextDisabled("thick = torque, thin = power. Double-click a part to fit it. F7 or Esc closes.");
  }

  bool open_ = false;
  int slot_ = kCam;
  int32_t preview_ = -1;  // -1 none, 0 stock, else PartId
  int cache_engine_ = -1;
  int32_t cache_parts_[kNumSlots] = {};
  int32_t cache_preview_[kNumSlots] = {};
  DynoResult stock_, build_, prev_;
};

}  // namespace

// fm4_app.cpp: created with the other overlays. Loads the parts catalogue.
std::unique_ptr<rex::ui::ImGuiDialog> CreateEngineBuilder(rex::ui::ImGuiDrawer* drawer,
                                                          const std::filesystem::path& user_root) {
  if (!drawer) return nullptr;
  eb::LoadCatalogueOnce(user_root);
  if (!eb::BuilderEnabled()) {
    REXLOG_INFO("Engine builder: off (fm4_engine_builder, FM4_ENGINE_BUILDER or FM4_EVO)");
    return nullptr;
  }
  return std::make_unique<EngineBuilderOverlay>(drawer);
}
