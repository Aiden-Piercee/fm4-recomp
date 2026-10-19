// ---------------------------------------------------------------------------
// Rim picker overlay, spike v0 (Forza-4-Evolution). DISPLAY ONLY.
// Design: claude/rim-shop-brand-drilldown-ui-research-2026-10-18.md, mockup A1:
// a 3x3 grid of the current brand's rims in the shop's right-hand panel under
// the PI badge, a brand header, and the weight change of the highlighted rim.
//
// v0 draws over the stock panel (stock keeps working underneath, no input is
// taken) and logs what the shop highlights on the Rim Style page, so we learn
// the real state before wiring input. Data comes from fm4_rim_catalog.tsv
// (make_rim_catalog.py), looked up in the user data root, then the working
// directory. No catalogue = overlay off. FM4_RIM_PICKER=0 turns it off.
//
// Needs the shop snapshot published by brake_preview.cpp
// (UpgradeShopSnapshot, see main-tree-edits.md).
// ---------------------------------------------------------------------------

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include <rex/logging.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// brake_preview.cpp: latest values from the shop frame hook. False until the
// shop has been seen. age_ms = time since the shop last ran its frame.
bool UpgradeShopSnapshot(uint32_t* page_part, uint32_t* highlighted, uint32_t* fitted_wheel, uint32_t* car,
                         int64_t* age_ms);

namespace {

constexpr uint32_t kPartEnumRimStyle = 43;
constexpr int kCols = 3, kRows = 3, kPerPage = kCols * kRows;

struct Wheel {
  int32_t id = 0;
  std::string media, name;
  int32_t maker = 0;
  int32_t price = 0;
  float mass = 0;
  bool shop = false;
};

struct Catalogue {
  std::map<int32_t, std::string> maker_name;
  std::map<int32_t, Wheel> wheels;
  std::map<int32_t, std::vector<const Wheel*>> by_maker;  // shop rims only
  std::map<uint32_t, int32_t> car_stock_wheel;
  std::vector<int32_t> makers;  // sorted by name
};

bool EnvOn(const char* name) {
  const char* v = std::getenv(name);
  return !(v && v[0] == '0');
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  std::istringstream is(s);
  while (std::getline(is, cur, sep)) out.push_back(cur);
  return out;
}

bool LoadCatalogue(const std::filesystem::path& file, Catalogue& cat) {
  std::ifstream in(file);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto f = Split(line, '\t');
    if (f.size() < 3) continue;
    if (f[0] == "M") {
      cat.maker_name[std::atoi(f[1].c_str())] = f[2];
    } else if (f[0] == "W" && f.size() >= 8) {
      Wheel w;
      w.id = std::atoi(f[1].c_str());
      w.media = f[2];
      w.name = f[3];
      w.maker = std::atoi(f[4].c_str());
      w.price = std::atoi(f[5].c_str());
      w.mass = float(std::atof(f[6].c_str()));
      w.shop = f[7] == "1";
      cat.wheels[w.id] = w;
    } else if (f[0] == "C") {
      cat.car_stock_wheel[uint32_t(std::atoi(f[1].c_str()))] = std::atoi(f[2].c_str());
    }
  }
  for (auto& [id, w] : cat.wheels)
    if (w.shop) cat.by_maker[w.maker].push_back(&w);
  for (auto& [mk, v] : cat.by_maker) {
    (void)v;
    cat.makers.push_back(mk);
  }
  std::sort(cat.makers.begin(), cat.makers.end(), [&](int32_t a, int32_t b) {
    return cat.maker_name[a] < cat.maker_name[b];
  });
  return !cat.makers.empty();
}

constexpr ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

class RimPicker : public rex::ui::ImGuiDialog {
 public:
  RimPicker(rex::ui::ImGuiDrawer* drawer, Catalogue cat) : rex::ui::ImGuiDialog(drawer), cat_(std::move(cat)) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    uint32_t page = 0, hl = 0, fit = 0, car = 0;
    int64_t age = 0;
    if (!UpgradeShopSnapshot(&page, &hl, &fit, &car, &age) || age > 300 || page != kPartEnumRimStyle) {
      last_hl_ = ~0u;
      return;
    }
    if (hl != last_hl_) {
      last_hl_ = hl;
      const auto it = cat_.wheels.find(int32_t(hl));
      REXLOG_INFO("Rim picker: Rim Style highlighted {} ({}), fitted wheel {}, car {}", hl,
                  it == cat_.wheels.end() ? "not a rim Id" : it->second.name, int32_t(fit), car);
    }

    // Brand: the highlighted rim's maker; otherwise keep the last one.
    const auto hit = cat_.wheels.find(int32_t(hl));
    if (hit != cat_.wheels.end() && hit->second.shop) brand_ = hit->second.maker;
    if (!cat_.by_maker.count(brand_)) brand_ = cat_.makers.front();
    const auto& rims = cat_.by_maker[brand_];
    int idx = -1;
    for (size_t i = 0; i < rims.size(); ++i)
      if (rims[i]->id == int32_t(hl)) idx = int(i);
    const int page_no = idx >= 0 ? idx / kPerPage : 0;
    const int pages = int((rims.size() + kPerPage - 1) / kPerPage);

    // Map the 2000x1125 reference layout onto the 16:9 game view.
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float vw = W, vh = W * 9.0f / 16.0f;
    if (vh > H) { vh = H; vw = H * 16.0f / 9.0f; }
    const float ox = (W - vw) * 0.5f, oy = (H - vh) * 0.5f;
    auto X = [&](float x) { return ox + x / 2000.0f * vw; };
    auto Y = [&](float y) { return oy + y / 1125.0f * vh; };
    const float k = vh / 1125.0f;  // pixels per reference pixel

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    auto text = [&](float size, ImVec2 p, ImU32 c, const std::string& s) {
      dl->AddText(font, size * k, p, c, s.c_str());
    };
    auto center = [&](float size, ImVec2 a, ImVec2 b, ImU32 c, const std::string& s) {
      const ImVec2 sz = font->CalcTextSizeA(size * k, FLT_MAX, 0.0f, s.c_str());
      dl->PushClipRect(a, b, true);
      dl->AddText(font, size * k, ImVec2((a.x + b.x - sz.x) * 0.5f, (a.y + b.y - sz.y) * 0.5f), c, s.c_str());
      dl->PopClipRect();
    };

    // Panel under the PI badge (covers the stock stats list in v0).
    const ImVec2 p0(X(1557), Y(374)), p1(X(2000), Y(756));
    dl->AddRectFilled(p0, p1, Col(17, 17, 17));
    dl->AddLine(p0, ImVec2(p1.x, p0.y), Col(44, 44, 44));

    // Header: brand, position, page.
    const auto mi = std::find(cat_.makers.begin(), cat_.makers.end(), brand_) - cat_.makers.begin();
    text(24, ImVec2(X(1573), Y(384)), Col(238, 238, 238), cat_.maker_name[brand_]);
    {
      std::ostringstream os;
      os << "brand " << (mi + 1) << " / " << cat_.makers.size() << "  |  " << rims.size() << " rims";
      if (pages > 1) os << "  |  page " << (page_no + 1) << "/" << pages;
      const std::string s = os.str();
      const ImVec2 sz = font->CalcTextSizeA(16 * k, FLT_MAX, 0.0f, s.c_str());
      text(16, ImVec2(X(1985) - sz.x, Y(388)), Col(169, 165, 138), s);
    }

    // 3x3 grid.
    const float gx = 1569, gy = 420, tw = 138, th = 84, gap = 6;
    for (int i = 0; i < kPerPage; ++i) {
      const size_t n = size_t(page_no) * kPerPage + i;
      const float x = gx + (i % kCols) * (tw + gap), y = gy + (i / kCols) * (th + gap);
      const ImVec2 a(X(x), Y(y)), b(X(x + tw), Y(y + th));
      if (n >= rims.size()) { dl->AddRectFilled(a, b, Col(26, 26, 26)); continue; }
      const bool sel = int(n) == idx;
      dl->AddRectFilled(a, b, sel ? Col(235, 235, 235) : Col(42, 42, 42));
      if (sel) {
        dl->AddRectFilled(a, ImVec2(b.x, a.y + 3 * k), Col(216, 67, 31));
        dl->AddRectFilled(ImVec2(a.x, b.y - 3 * k), b, Col(216, 67, 31));
      }
      center(17, ImVec2(a.x + 4 * k, a.y + 4 * k), ImVec2(b.x - 4 * k, b.y - 4 * k),
             sel ? Col(17, 17, 17) : Col(210, 210, 210), rims[n]->name);
    }

    // Weight change of the highlighted rim against the fitted one (4 wheels).
    const Wheel* now = nullptr;
    if (hit != cat_.wheels.end()) now = &hit->second;
    int32_t fid = int32_t(fit);
    if (fid <= 0) {
      const auto sw = cat_.car_stock_wheel.find(car);
      fid = sw == cat_.car_stock_wheel.end() ? 0 : sw->second;
    }
    const auto fw = cat_.wheels.find(fid);
    dl->AddLine(ImVec2(X(1557), Y(696)), ImVec2(X(2000), Y(696)), Col(44, 44, 44));
    text(14, ImVec2(X(1573), Y(702)), Col(169, 165, 138), "WEIGHT CHANGE");
    if (now && fw != cat_.wheels.end()) {
      const float d = 4.0f * (now->mass - fw->second.mass);
      char buf[48];
      std::snprintf(buf, sizeof buf, "%+.0f KG", d);
      text(27, ImVec2(X(1573), Y(718)), d <= 0 ? Col(139, 154, 42) : Col(220, 90, 60), buf);
    } else {
      text(27, ImVec2(X(1573), Y(718)), Col(120, 120, 120), "-");
    }
    text(13, ImVec2(X(1800), Y(730)), Col(110, 110, 110), "rim picker v0: display only");
  }

 private:
  Catalogue cat_;
  int32_t brand_ = 0;
  uint32_t last_hl_ = ~0u;
};

}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateRimPicker(rex::ui::ImGuiDrawer* drawer,
                                                      const std::filesystem::path& user_root) {
  if (!drawer || !EnvOn("FM4_RIM_PICKER")) return nullptr;
  Catalogue cat;
  const std::filesystem::path candidates[] = {user_root / "fm4_rim_catalog.tsv", "fm4_rim_catalog.tsv"};
  for (const auto& p : candidates) {
    if (LoadCatalogue(p, cat)) {
      REXLOG_INFO("Rim picker: catalogue {} ({} rims, {} makers)", p.string(), cat.wheels.size(), cat.makers.size());
      return std::make_unique<RimPicker>(drawer, std::move(cat));
    }
  }
  REXLOG_INFO("Rim picker: no fm4_rim_catalog.tsv found, overlay off");
  return nullptr;
}
