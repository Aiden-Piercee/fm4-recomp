// ---------------------------------------------------------------------------
// Engine builder: calibration report for the engine model against stock FM4 torque curves.
// Build: c++ -std=c++17 -O2 -Wall -Wextra -I fm4/src/engine_build tools/engine_build/dyno_calibrate.cpp
//          fm4/src/engine_build/dyno.cpp fm4/src/engine_build/catalogue.cpp -o dyno_calibrate
// Run:   ./dyno_calibrate <catalogue.tsv> <curves.csv> [lo_rpm] [hi_rpm]
//
// curves.csv: header line, then "EngineID,rpm,torque_nm" rows, one per curve point. Export it from your own game
// database (the curve tables are FM4 data and are not shipped in this repository). Points with torque <= 0 (the
// fuel cut FM4 puts at the end of a curve) are ignored.
//
// For every E row of the catalogue that has a curve, prints model vs FM4 peak torque rpm and peak power rpm and the
// RMS difference of the two curves each normalised to its own peak torque, over lo..hi rpm (default 1000 rpm to 85%
// of the FM4 curve's last positive point). The model is only ever used as a ratio against itself, so the shape is
// what matters; this is a sanity check, not a fit target.
// ---------------------------------------------------------------------------
#include "catalogue.h"
#include "dyno.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Curve {
  std::vector<double> rpm, nm;
  double At(double r) const {
    if (rpm.empty()) return 0.0;
    if (r <= rpm.front()) return nm.front();
    for (size_t i = 1; i < rpm.size(); ++i)
      if (r <= rpm[i]) {
        const double f = (r - rpm[i - 1]) / (rpm[i] - rpm[i - 1]);
        return nm[i - 1] + (nm[i] - nm[i - 1]) * f;
      }
    return nm.back();
  }
};

bool LoadCurves(const std::string& path, std::map<int, Curve>& out) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  std::getline(in, line);  // header
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string a, b, c;
    if (!std::getline(ss, a, ',') || !std::getline(ss, b, ',') || !std::getline(ss, c, ',')) continue;
    const double t = std::strtod(c.c_str(), nullptr);
    if (t <= 0.0) continue;
    Curve& cv = out[std::atoi(a.c_str())];
    cv.rpm.push_back(std::strtod(b.c_str(), nullptr));
    cv.nm.push_back(t);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <catalogue.tsv> <curves.csv> [lo_rpm] [hi_rpm]\n", argv[0]);
    return 2;
  }
  eb::Catalogue cat;
  if (!eb::LoadCatalogue(argv[1], cat)) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
  for (const std::string& e : cat.errors) std::fprintf(stderr, "%s\n", e.c_str());
  std::map<int, Curve> curves;
  if (!LoadCurves(argv[2], curves)) { std::fprintf(stderr, "cannot open %s\n", argv[2]); return 2; }
  const double lo_arg = argc > 3 ? std::atof(argv[3]) : 1000.0;
  const double hi_arg = argc > 4 ? std::atof(argv[4]) : 0.0;

  std::printf("%-8s %-30s %7s %7s %6s | %7s %7s %6s | %7s %7s | %6s %6s\n", "Engine", "Name", "TqRpm", "FM4", "err%",
              "PwRpm", "FM4", "err%", "Tq Nm", "kW", "RMS", "float");
  int n = 0, within = 0;
  double rms_sum = 0.0;
  for (const auto& [id, e] : cat.engines) {
    const auto it = curves.find(id);
    if (it == curves.end() || it->second.rpm.size() < 4) continue;
    const Curve& fm = it->second;
    const double fm_max = fm.rpm.back();
    const double hi = hi_arg > 0.0 ? hi_arg : 0.85 * fm_max;
    const eb::DynoResult r = eb::RunDyno(eb::StockSpec(e), fm_max);

    double fm_pk = 0.0, fm_pk_rpm = 0.0, fm_pw = 0.0, fm_pw_rpm = 0.0;
    for (size_t i = 0; i < fm.rpm.size(); ++i) {
      if (fm.rpm[i] > hi) break;
      if (fm.nm[i] > fm_pk) { fm_pk = fm.nm[i]; fm_pk_rpm = fm.rpm[i]; }
      const double p = fm.nm[i] * fm.rpm[i];
      if (p > fm_pw) { fm_pw = p; fm_pw_rpm = fm.rpm[i]; }
    }
    // Model peaks inside the same window.
    double m_pk = 0.0, m_pk_rpm = 0.0, m_pw = 0.0, m_pw_rpm = 0.0;
    for (double rpm = 0.0; rpm <= hi; rpm += 25.0) {
      const double t = eb::TorqueAt(r, rpm);
      if (t > m_pk) { m_pk = t; m_pk_rpm = rpm; }
      if (t * rpm > m_pw) { m_pw = t * rpm; m_pw_rpm = rpm; }
    }
    double ss = 0.0;
    int k = 0;
    for (double rpm = lo_arg; rpm <= hi; rpm += 100.0, ++k) {
      const double d = eb::TorqueAt(r, rpm) / m_pk - fm.At(rpm) / fm_pk;
      ss += d * d;
    }
    const double rms = k ? std::sqrt(ss / k) : 0.0;
    const double tq_err = 100.0 * (m_pk_rpm / fm_pk_rpm - 1.0), pw_err = 100.0 * (m_pw_rpm / fm_pw_rpm - 1.0);
    std::printf("%-8d %-30.30s %7.0f %7.0f %+6.1f | %7.0f %7.0f %+6.1f | %7.0f %7.0f | %6.3f %6.0f\n", int(id),
                e.name.c_str(), m_pk_rpm, fm_pk_rpm, tq_err, m_pw_rpm, fm_pw_rpm, pw_err, m_pk,
                m_pw * 2.0 * 3.14159265358979 / 60e3, rms, r.valve_float_rpm);
    ++n;
    within += std::fabs(tq_err) <= 15.0;
    rms_sum += rms;
  }
  if (n) std::printf("%d engines: peak torque rpm within 15%% for %d, mean shape RMS %.3f\n", n, within, rms_sum / n);
  return 0;
}
