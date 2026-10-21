// ---------------------------------------------------------------------------
// Engine builder: host test for the engine model (fm4/src/engine_build/dyno.cpp) and the catalogue parser.
// Build: c++ -std=c++17 -O2 -Wall -Wextra -I fm4/src/engine_build tools/engine_build/dyno_test.cpp
//          fm4/src/engine_build/dyno.cpp fm4/src/engine_build/catalogue.cpp -o dyno_test
// Run:   ./dyno_test [catalogue.tsv] [-v]   (prints PASS/FAIL per check, returns nonzero on any failure;
//        with a catalogue path it also parses it and runs every stock engine; -v prints the baseline curve)
//
// The direction checks use a 1969-style 350 ci small-block Chevrolet: 4.00 x 3.48 in, 10.25:1, 2.02/1.60 valves,
// 222/230 deg at 0.050 in, 0.45 in lift, hydraulic flat tappet, dual-plane intake, 600 cfm four-barrel, cast log
// manifolds, points ignition.
// ---------------------------------------------------------------------------
#include "catalogue.h"
#include "dyno.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_fail = 0, g_pass = 0;

void Check(bool ok, const char* name, const char* fmt = "", double a = 0.0, double b = 0.0) {
  std::printf("%s  %-58s ", ok ? "PASS" : "FAIL", name);
  std::printf(fmt, a, b);
  std::printf("\n");
  (ok ? g_pass : g_fail)++;
}

eb::EngineSpec Baseline() {
  eb::EngineSpec s;
  s.bore_mm = 101.6;
  s.stroke_mm = 88.4;
  s.cylinders = 8;
  s.compression = 10.25;
  s.intake_valve_mm = 2.02 * 25.4;
  s.exhaust_valve_mm = 1.60 * 25.4;
  s.intake_duration = 222.0;
  s.exhaust_duration = 230.0;
  s.intake_lift_mm = 0.45 * 25.4;
  s.exhaust_lift_mm = 0.46 * 25.4;
  s.lobe_separation = 110.0;
  s.lifter = eb::kHydraulicFlat;
  s.intake = eb::kDualPlane;
  s.carb_cfm = 600.0;
  s.carbureted = 1;
  s.air_filter_restriction = 0.05;
  s.header = eb::kCastLog;
  s.pipe_diameter_mm = 57.0;
  s.dual_exhaust = 1;
  s.muffler_restriction = 0.5;
  s.ignition = eb::kPoints;
  s.fuel_octane = 99.0;
  return s;
}

bool AllFinite(const eb::DynoResult& r) {
  for (double t : r.torque_nm)
    if (!std::isfinite(t) || t < 0.0) return false;
  return std::isfinite(r.peak_power_kw) && std::isfinite(r.peak_torque_nm) && std::isfinite(r.safe_rpm) &&
         std::isfinite(r.valve_float_rpm) && std::isfinite(r.idle_rpm) && std::isfinite(r.knock_margin);
}

double MaxJump(const eb::DynoResult& r) {
  double j = 0.0;
  for (int i = 1; i < eb::DynoResult::kPoints; ++i) j = std::fmax(j, std::fabs(r.torque_nm[i] - r.torque_nm[i - 1]));
  return r.peak_torque_nm > 0.0 ? j / r.peak_torque_nm : 0.0;
}

// Mean torque over an rpm band (fine steps through TorqueAt).
double Band(const eb::DynoResult& r, double lo, double hi) {
  double sum = 0.0;
  int n = 0;
  for (double rpm = lo; rpm <= hi + 1e-6; rpm += 50.0, ++n) sum += eb::TorqueAt(r, rpm);
  return n ? sum / n : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
  const char* cat_path = nullptr;
  bool verbose = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "-v")) verbose = true;
    else cat_path = argv[i];
  }

  using namespace eb;
  const EngineSpec base = Baseline();
  const DynoResult b = RunDyno(base);

  if (verbose) {
    std::printf("baseline 350: rpm torque_nm power_kw\n");
    for (int i = 0; i < DynoResult::kPoints; i += 4)
      std::printf("  %6.0f %6.1f %6.1f\n", b.rpm_at(i), b.torque_nm[i], b.torque_nm[i] * b.rpm_at(i) * 2 * 3.14159265 / 60e3);
  }

  // --- Basic sanity ---------------------------------------------------------------------------------------------
  Check(std::fabs(DisplacementCc(base) - 5733.0) < 15.0, "displacement of a 350 ci", "%.0f cc", DisplacementCc(base));
  Check(AllFinite(b), "baseline finite and non-negative");
  Check(MaxJump(b) < 0.06, "baseline smooth (max step < 6% of peak)", "%.3f", MaxJump(b));
  Check(b.peak_torque_nm > 380.0 && b.peak_torque_nm < 480.0, "baseline peak torque 380..480 N.m", "%.0f N.m @ %.0f",
        b.peak_torque_nm, b.peak_torque_rpm);
  Check(b.peak_power_kw > 220.0 && b.peak_power_kw < 300.0, "baseline peak power 220..300 kW", "%.0f kW @ %.0f",
        b.peak_power_kw, b.peak_power_rpm);
  Check(b.peak_torque_rpm >= 3200.0 && b.peak_torque_rpm <= 4500.0, "baseline peak torque rpm 3200..4500", "%.0f",
        b.peak_torque_rpm);
  Check(b.peak_power_rpm > b.peak_torque_rpm + 1000.0, "peak power well above peak torque", "%.0f vs %.0f",
        b.peak_power_rpm, b.peak_torque_rpm);
  Check(b.idle_rpm > 500.0 && b.idle_rpm < 1000.0, "baseline idle 500..1000", "%.0f", b.idle_rpm);
  Check(std::fabs(TorqueAt(b, 0.0) - b.torque_nm[0]) < 1e-9 && TorqueAt(b, -5.0) == 0.0 &&
            std::fabs(TorqueAt(b, 1e9) - b.torque_nm[DynoResult::kPoints - 1]) < 1e-9,
        "TorqueAt clamps its range");

  // --- Robustness: defaults, zeros, extremes ----------------------------------------------------------------------
  {
    EngineSpec def;
    EngineSpec zero;
    std::memset(static_cast<void*>(&zero), 0, sizeof(zero));
    EngineSpec wild = base;
    wild.bore_mm = 1e9; wild.stroke_mm = -3; wild.compression = 40; wild.intake_duration = 1000;
    wild.lobe_separation = 0; wild.carb_cfm = 1; wild.pipe_diameter_mm = 0; wild.cylinders = 99; wild.intake = 17;
    wild.header = -2; wild.lifter = 9; wild.ignition = 7; wild.fuel_octane = std::nan("");
    EngineSpec tiny = base;
    tiny.bore_mm = 50; tiny.stroke_mm = 50; tiny.cylinders = 1; tiny.carb_cfm = 2000;
    bool ok = true, smooth = true;
    for (const EngineSpec* s : {&def, &zero, &wild, &tiny})
      for (double rmax : {3000.0, 10000.0, 12000.0, 0.0}) {
        const DynoResult r = RunDyno(*s, rmax);
        ok = ok && AllFinite(r) && r.peak_torque_nm > 0.0;
        smooth = smooth && MaxJump(r) < 0.08;
      }
    Check(ok, "defaults / all-zero / out-of-range specs stay finite");
    Check(smooth, "and smooth (max step < 8% of peak)");
  }

  // --- Directions of part effects --------------------------------------------------------------------------------
  {
    EngineSpec s = base;  // bigger cam: 248/254, 0.52 in, 108 LSA
    s.intake_duration = 248; s.exhaust_duration = 254; s.intake_lift_mm = s.exhaust_lift_mm = 0.52 * 25.4;
    s.lobe_separation = 108;
    const DynoResult r = RunDyno(s);
    Check(r.peak_power_rpm > b.peak_power_rpm, "bigger cam: peak power rpm up", "%.0f -> %.0f", b.peak_power_rpm,
          r.peak_power_rpm);
    Check(TorqueAt(r, 2000) < TorqueAt(b, 2000), "bigger cam: less torque at 2000", "%.0f -> %.0f", TorqueAt(b, 2000),
          TorqueAt(r, 2000));
    Check(r.idle_rpm > b.idle_rpm, "bigger cam: higher idle", "%.0f -> %.0f", b.idle_rpm, r.idle_rpm);
    EngineSpec h = s;
    h.header = kLongTubeHeader;
    EngineSpec bh = base;
    bh.header = kLongTubeHeader;
    const double gain_big = Band(RunDyno(h), 5500, 6500) / Band(r, 5500, 6500);
    const double gain_small = Band(RunDyno(bh), 5500, 6500) / Band(b, 5500, 6500);
    Check(gain_big > gain_small, "headers help a big-overlap cam more", "%.3f vs %.3f", gain_big, gain_small);
  }
  {
    EngineSpec s = base;
    s.intake = kTunnelRam;
    s.carb_cfm = 1200;  // two four-barrels on a tunnel ram
    EngineSpec d = base;
    d.carb_cfm = 1200;
    const DynoResult r = RunDyno(s), rd = RunDyno(d);
    Check(Band(r, 1500, 3000) < Band(rd, 1500, 3000), "tunnel ram vs dual plane: less low (1500-3000)", "%.0f vs %.0f",
          Band(r, 1500, 3000), Band(rd, 1500, 3000));
    Check(Band(r, 6000, 7000) > Band(rd, 6000, 7000), "tunnel ram vs dual plane: more top (6000-7000)", "%.0f vs %.0f",
          Band(r, 6000, 7000), Band(rd, 6000, 7000));
    EngineSpec sp = base;
    sp.intake = kSinglePlane;
    const DynoResult rs = RunDyno(sp);
    Check(Band(rs, 1500, 3000) < Band(b, 1500, 3000) && Band(rs, 5500, 6500) > Band(b, 5500, 6500),
          "single plane vs dual plane: less low, more top");
  }
  {
    EngineSpec s = base;
    s.header = kLongTubeHeader;
    const DynoResult r = RunDyno(s);
    Check(Band(r, 5000, 6500) > Band(b, 5000, 6500) * 1.03, "long-tube headers: top end up > 3%", "%.3f",
          Band(r, 5000, 6500) / Band(b, 5000, 6500));
    Check(r.peak_power_kw > b.peak_power_kw, "long-tube headers: more peak power", "%.0f -> %.0f kW", b.peak_power_kw,
          r.peak_power_kw);
    EngineSpec o = base;
    o.muffler_restriction = 0.0;
    Check(Band(RunDyno(o), 5000, 6000) > Band(b, 5000, 6000), "open exhaust: more top end");
    EngineSpec single = base;
    single.dual_exhaust = 0;
    Check(Band(RunDyno(single), 5000, 6000) < Band(b, 5000, 6000) * 0.97, "single exhaust: top end down > 3%");
  }
  {
    EngineSpec s = base;
    s.carb_cfm = 390;
    const DynoResult r = RunDyno(s);
    Check(r.peak_power_kw < b.peak_power_kw * 0.93, "390 cfm carb: peak power capped (> 7% down)", "%.0f -> %.0f kW",
          b.peak_power_kw, r.peak_power_kw);
    Check(std::fabs(TorqueAt(r, 2000) / TorqueAt(b, 2000) - 1.0) < 0.03, "390 cfm carb: low end about the same",
          "%.3f", TorqueAt(r, 2000) / TorqueAt(b, 2000));
    EngineSpec big = base;
    big.carb_cfm = 1000;
    Check(TorqueAt(RunDyno(big), 1500) < TorqueAt(b, 1500), "oversized carb: less torque at 1500");
  }
  {
    EngineSpec lo = base, hi = base;
    lo.compression = 9.0; hi.compression = 11.0;
    lo.fuel_octane = hi.fuel_octane = 104.0;
    const DynoResult rl = RunDyno(lo), rh = RunDyno(hi);
    Check(rh.peak_torque_nm > rl.peak_torque_nm, "higher CR (good fuel): more torque", "%.0f -> %.0f", rl.peak_torque_nm,
          rh.peak_torque_nm);
    EngineSpec k95 = base, k12 = base;
    k95.compression = 9.5; k12.compression = 12.0;
    k95.fuel_octane = k12.fuel_octane = 91.0;
    const DynoResult a = RunDyno(k95), c = RunDyno(k12);
    Check(c.knock_margin < 0.0 && c.peak_torque_nm < a.peak_torque_nm, "12:1 on 91 octane: knock costs torque",
          "%.0f vs %.0f", c.peak_torque_nm, a.peak_torque_nm);
    Check(a.knock_margin > c.knock_margin, "knock margin falls with CR");
  }
  {
    EngineSpec s = base;
    s.lifter = kSolidRoller;
    const DynoResult r = RunDyno(s);
    Check(r.valve_float_rpm > b.valve_float_rpm + 1000.0, "solid roller: valve float rpm up", "%.0f -> %.0f",
          b.valve_float_rpm, r.valve_float_rpm);
    EngineSpec sf = base;
    sf.lifter = kSolidFlat;
    EngineSpec hr = base;
    hr.lifter = kHydraulicRoller;
    const double f0 = b.valve_float_rpm, f1 = RunDyno(sf).valve_float_rpm, f2 = RunDyno(hr).valve_float_rpm;
    Check(f0 < f1 && f1 < f2 && f2 < r.valve_float_rpm, "float order hyd flat < solid flat < hyd roller < solid roller");
    EngineSpec lift = base;
    lift.rocker_ratio = 1.7;
    Check(RunDyno(lift).valve_float_rpm < f0, "higher rocker ratio: float lower");
    EngineSpec spr = base;
    spr.valve_float_rpm = 7600;
    const DynoResult rs = RunDyno(spr);
    Check(rs.valve_float_rpm == 7600 && rs.safe_rpm == 7000, "springs part sets float; safe = min(float, rod limit)");
    Check(TorqueAt(b, b.valve_float_rpm + 1500) < 0.5 * TorqueAt(b, b.valve_float_rpm - 300),
          "torque falls hard above valve float");
  }
  {
    EngineSpec s = base;
    s.stroke_mm = 95.25;  // 383 stroker
    const DynoResult r = RunDyno(s);
    Check(Band(r, 2000, 4500) > Band(b, 2000, 4500) * 1.04, "stroker: low/mid torque up > 4%", "%.3f",
          Band(r, 2000, 4500) / Band(b, 2000, 4500));
  }
  {
    EngineSpec cd = base;
    cd.ignition = kCapacitiveDischarge;
    EngineSpec el = base;
    el.ignition = kElectronic;
    const DynoResult rc = RunDyno(cd), re = RunDyno(el);
    Check(TorqueAt(rc, 7000) > TorqueAt(b, 7000) * 1.02, "CD vs points at 7000: CD stronger", "%.0f vs %.0f",
          TorqueAt(rc, 7000), TorqueAt(b, 7000));
    Check(TorqueAt(re, 7000) > TorqueAt(b, 7000) && TorqueAt(rc, 7000) >= TorqueAt(re, 7000),
          "ignition order at 7000: points < electronic <= CD");
    Check(std::fabs(TorqueAt(rc, 2500) / TorqueAt(b, 2500) - 1.0) < 0.01, "ignition neutral at 2500");
  }
  {
    EngineSpec s = base;
    s.intake_valve_mm = 2.08 * 25.4; s.exhaust_valve_mm = 1.65 * 25.4; s.port_flow = 1.2;
    Check(Band(RunDyno(s), 5500, 6500) > Band(b, 5500, 6500) * 1.03, "ported heads, bigger valves: top end up");
    EngineSpec p = base;
    p.port_volume_cc = 240;
    Check(TorqueAt(RunDyno(p), 2000) < TorqueAt(b, 2000), "oversized port volume: less torque at 2000");
  }

  // --- Speed --------------------------------------------------------------------------------------------------------
  {
    const auto t0 = std::chrono::steady_clock::now();
    double sink = 0.0;
    for (int i = 0; i < 200; ++i) sink += RunDyno(base).peak_power_kw;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 200.0;
    Check(ms < 1.0 && sink > 0.0, "RunDyno under 1 ms", "%.4f ms", ms);
  }

  // --- Catalogue ---------------------------------------------------------------------------------------------------
  {
    Catalogue cat;
    const char* text =
        "# test\n"
        "F\tCHEVY_SB\tChevrolet small block\n"
        "E\t44\tCHEVY_SB\t302 Z28\tbore=101.6\tstroke=76.2\tcr=11\n"
        "P\t100\tcam\tCHEVY_SB\tAcme\tBig cam\t1967\t0\t2.0\tidur=248\tilift*=1.1\tlsa+=-2\n"
        "P\t101\tpistons\t*\tAcme\tDome\t1960\t0\t1.0\tcr+=1\n"
        "X\tbad\n";
    ParseCatalogue(text, "inline", cat);
    const StockEngine* e = cat.FindEngine(44);
    int32_t parts[kNumSlots] = {};
    parts[kCam] = 100;
    parts[kPistons] = 101;
    bool ok = e && cat.errors.size() == 1;
    if (e) {
      const EngineSpec s = BuildSpec(cat, *e, parts);
      ok = ok && s.intake_duration == 248 && std::fabs(s.intake_lift_mm - 11.0) < 1e-9 && s.lobe_separation == 110 &&
           s.compression == 12 && s.stroke_mm == 76.2;
    }
    Check(ok, "catalogue: parse and apply = += *=");
  }
  if (cat_path) {
    Catalogue cat;
    const bool loaded = LoadCatalogue(cat_path, cat);
    Check(loaded && cat.errors.empty(), "shipped catalogue parses without errors", "%.0f engines, %.0f parts",
          double(cat.engines.size()), double(cat.parts.size()));
    for (const std::string& err : cat.errors) std::printf("      %s\n", err.c_str());
    bool ok = true;
    for (const auto& [id, e] : cat.engines) {
      const DynoResult r = RunDyno(StockSpec(e));
      if (!AllFinite(r) || MaxJump(r) > 0.06 || r.peak_torque_nm <= 0.0) {
        ok = false;
        std::printf("      engine %d not sane\n", int(id));
      }
      for (const auto& [pid, p] : cat.parts) {
        if (!p.Fits(e.family)) continue;
        int32_t parts[kNumSlots] = {};
        parts[p.slot] = pid;
        const DynoResult rp = RunDyno(BuildSpec(cat, e, parts));
        if (!AllFinite(rp) || MaxJump(rp) > 0.08 || rp.peak_torque_nm <= 0.0) {
          ok = false;
          std::printf("      engine %d + part %d not sane\n", int(id), int(pid));
        }
      }
    }
    Check(ok, "every stock engine and single-part build is finite and smooth");
  }

  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
