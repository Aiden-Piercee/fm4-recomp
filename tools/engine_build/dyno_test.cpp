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

// Largest point-to-point change as a fraction of peak torque, over points up to `upto` rpm (default: all).
double MaxJump(const eb::DynoResult& r, double upto = 1e30) {
  double j = 0.0;
  for (int i = 1; i < eb::DynoResult::kPoints && r.rpm_at(i - 1) <= upto; ++i)
    j = std::fmax(j, std::fabs(r.torque_nm[i] - r.torque_nm[i - 1]));
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

  // --- Fuzz: random specs across the catalogue's ranges -------------------------------------------------------------
  {
    unsigned state = 12345u;
    auto rnd = [&](double lo, double hi) {
      state = state * 1664525u + 1013904223u;
      return lo + (hi - lo) * ((state >> 8) & 0xFFFFFF) / double(0xFFFFFF);
    };
    int bad = 0, rough = 0, rough_all = 0;
    double worst = 0.0, worst_all = 0.0;
    for (int n = 0; n < 3000; ++n) {
      EngineSpec s;
      s.bore_mm = rnd(80, 115); s.stroke_mm = rnd(70, 110); s.cylinders = int(rnd(4, 12.99));
      s.rod_length_mm = rnd(0, 1) < 0.5 ? 0.0 : s.stroke_mm * rnd(1.5, 2.0);
      s.compression = rnd(7.5, 14); s.valves_per_cyl = rnd(0, 1) < 0.8 ? 2 : 4;
      s.intake_valve_mm = rnd(40, 60) * (s.valves_per_cyl == 4 ? 0.7 : 1.0); s.exhaust_valve_mm = s.intake_valve_mm * rnd(0.75, 0.88);
      s.port_flow = rnd(0.8, 1.35); s.port_volume_cc = rnd(0, 1) < 0.5 ? 0.0 : rnd(140, 330);
      s.intake_duration = rnd(180, 290); s.exhaust_duration = s.intake_duration + rnd(-4, 16);
      s.intake_lift_mm = rnd(8, 17); s.exhaust_lift_mm = s.intake_lift_mm * rnd(0.95, 1.08);
      s.lobe_separation = rnd(104, 118); s.intake_centerline = rnd(0, 1) < 0.5 ? 0.0 : s.lobe_separation + rnd(-8, 4);
      s.lifter = int(rnd(0, 3.99)); s.rocker_ratio = rnd(1.5, 1.8);
      s.valve_float_rpm = rnd(0, 1) < 0.7 ? 0.0 : rnd(5000, 9500);
      s.intake = int(rnd(0, 4.99)); s.runner_length_mm = rnd(0, 1) < 0.6 ? 0.0 : rnd(150, 600);
      s.carb_cfm = rnd(0, 1) < 0.15 ? 0.0 : rnd(250, 2000); s.carbureted = rnd(0, 1) < 0.8;
      s.air_filter_restriction = rnd(0, 0.15); s.header = int(rnd(0, 4.99));
      s.primary_diameter_mm = rnd(0, 1) < 0.6 ? 0.0 : rnd(35, 60); s.primary_length_mm = rnd(0, 1) < 0.6 ? 0.0 : rnd(250, 1100);
      s.pipe_diameter_mm = rnd(45, 90); s.dual_exhaust = rnd(0, 1) < 0.7; s.muffler_restriction = rnd(0, 1);
      s.ignition = int(rnd(0, 2.99)); s.fuel_octane = rnd(88, 110); s.rod_limit_rpm = rnd(5000, 9500);
      const DynoResult r = RunDyno(s, rnd(0, 1) < 0.5 ? 10000.0 : rnd(6000, 12000));
      if (!AllFinite(r) || r.peak_torque_nm <= 0.0) ++bad;
      // Well above valve float, where the torque is the small difference of falling imep and rising friction, the
      // roll-off is allowed to be steeper; the usable range (to 500 rpm past the safe rpm) must stay within 6%.
      const double j = MaxJump(r, r.safe_rpm + 500.0), j_all = MaxJump(r);
      worst = std::fmax(worst, j);
      worst_all = std::fmax(worst_all, j_all);
      if (j > 0.06) ++rough;
      if (j_all > 0.10) ++rough_all;
      if (j > 0.06 && verbose && rough < 12) {
        int at = 1;
        for (int i = 1; i < DynoResult::kPoints && r.rpm_at(i - 1) <= r.safe_rpm + 500.0; ++i)
          if (std::fabs(r.torque_nm[i] - r.torque_nm[i - 1]) >= j * r.peak_torque_nm - 1e-9) at = i;
        std::printf("      rough %.3f at %.0f rpm (max %.0f): safe %.0f float %.0f peak %.0f@%.0f cr %.1f oct %.0f cfm %.0f\n", j,
                    r.rpm_at(at), r.rpm_max, r.safe_rpm, r.valve_float_rpm, r.peak_torque_nm, r.peak_torque_rpm, s.compression,
                    s.fuel_octane, s.carb_cfm);
      }
    }
    Check(bad == 0, "fuzz: 3000 random specs finite, positive peak", "%.0f bad", bad);
    Check(rough == 0, "fuzz: max step < 6% of peak to safe rpm + 500", "worst %.3f (%.0f over)", worst, rough);
    Check(rough_all == 0, "fuzz: max step < 10% of peak everywhere", "worst %.3f (%.0f over)", worst_all, rough_all);
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

  // --- Forced induction and nitrous ----------------------------------------------------------------------------------
  {
    // NA unchanged: golden numbers of the baseline from the model before forced induction was added (relative 1e-12,
    // so a different libm still passes), and stray blower / nitrous fields without a blower or a shot change nothing.
    const double gold[7] = {0x1.dd862c0a33309p+8, 0x1.cd0797f4ad5bep+7, 0x1.666d3fea853afp+8, 0x1.d9f6656d78fc8p+8,
                            0x1.a08d2cd37bb32p+8, 0x1.7fc98f0c8a0d9p+7, 0x1.199999999998p+1};
    const double now[7] = {b.peak_torque_nm, b.peak_power_kw, b.torque_nm[10], b.torque_nm[30], b.torque_nm[50],
                           b.torque_nm[70], b.knock_margin};
    bool same = true;
    for (int i = 0; i < 7; ++i) same = same && std::fabs(now[i] - gold[i]) <= 1e-12 * std::fabs(gold[i]);
    Check(same, "NA baseline matches the pre-forced-induction model");
    EngineSpec stray = base;
    stray.boost_psi = 12; stray.boost_rpm = 4000; stray.blower_efficiency = 0.6; stray.intercooler = 0.7;
    stray.nitrous_rpm = 2500;
    const DynoResult rs = RunDyno(stray);
    Check(!std::memcmp(rs.torque_nm, b.torque_nm, sizeof b.torque_nm) && rs.knock_margin == b.knock_margin &&
              rs.peak_boost_psi == 0.0 && rs.blower_loss_kw == 0.0,
          "no blower, no nitrous: FI fields ignored, bit-identical");
  }
  {
    // Low-compression 350 with a pair of four-barrels (1200 cfm), as for a 6-71 street blower build.
    EngineSpec na = base;
    na.compression = 8.0;
    na.carb_cfm = 1200;
    const DynoResult rn = RunDyno(na);
    auto with = [&](int type, double psi, double brpm = 0.0) {
      EngineSpec s = na;
      s.blower = type; s.boost_psi = psi; s.boost_rpm = brpm;
      return s;
    };
    const DynoResult roots = RunDyno(with(kRoots, 7)), screw = RunDyno(with(kTwinScrew, 7));
    const DynoResult cent = RunDyno(with(kCentrifugal, 8)), turbo = RunDyno(with(kTurbo, 8, 3500));
    const double g_roots = roots.peak_power_kw / rn.peak_power_kw - 1.0;
    Check(g_roots > 0.30 && g_roots < 0.60, "Roots 7 psi on 8:1: peak power +30..60%", "%+.1f%% (%.0f hp)",
          100.0 * g_roots, roots.peak_power_kw / 0.7457);
    Check(TorqueAt(roots, 2000) > TorqueAt(rn, 2000) * 1.25, "Roots: big gain already at 2000", "%.3f",
          TorqueAt(roots, 2000) / TorqueAt(rn, 2000));
    Check(std::fabs(roots.peak_boost_psi - 7.0) < 0.3, "Roots: peak boost near the set boost", "%.2f psi",
          roots.peak_boost_psi);
    Check(roots.blower_loss_kw > 10.0 && roots.blower_loss_kw < 0.15 * roots.peak_power_kw,
          "Roots: drive loss recorded, 10 kW .. 15% of power", "%.1f kW", roots.blower_loss_kw);
    Check(screw.peak_power_kw > roots.peak_power_kw && screw.blower_loss_kw < roots.blower_loss_kw,
          "twin-screw (more efficient) beats Roots, less drive loss");
    Check(TorqueAt(roots, 1000) / TorqueAt(roots, 2500) < TorqueAt(rn, 1000) / TorqueAt(rn, 2500),
          "Roots: leakage droop at very low rpm");
    const double g_cent = cent.peak_power_kw / rn.peak_power_kw - 1.0;
    Check(TorqueAt(cent, 2000) / TorqueAt(rn, 2000) < TorqueAt(roots, 2000) / TorqueAt(rn, 2000) - 0.15,
          "centrifugal: much smaller low-rpm gain than Roots", "%.3f vs %.3f", TorqueAt(cent, 2000) / TorqueAt(rn, 2000),
          TorqueAt(roots, 2000) / TorqueAt(rn, 2000));
    Check(g_cent > 0.30 && std::fabs(g_cent - g_roots) < 0.15, "centrifugal 8 psi: similar peak gain (> 30%)",
          "%+.1f%% vs %+.1f%%", 100.0 * g_cent, 100.0 * g_roots);
    Check(std::fabs(TorqueAt(turbo, 2000) / TorqueAt(rn, 2000) - 1.0) < 0.01, "turbo: no effect well below spool",
          "%.4f", TorqueAt(turbo, 2000) / TorqueAt(rn, 2000));
    Check(Band(turbo, 4000, 5500) > Band(rn, 4000, 5500) * 1.25, "turbo: big gain above spool (> 25%)", "%.3f",
          Band(turbo, 4000, 5500) / Band(rn, 4000, 5500));
    Check(turbo.peak_power_kw > rn.peak_power_kw * 1.25 && turbo.blower_loss_kw == 0.0,
          "turbo: clearly more power, no shaft loss", "%+.1f%%", 100.0 * (turbo.peak_power_kw / rn.peak_power_kw - 1.0));
    const DynoResult early = RunDyno(with(kTurbo, 8, 2800));
    Check(TorqueAt(early, 3000) > TorqueAt(turbo, 3000) * 1.05, "turbo: earlier spool rpm, more at 3000");
    EngineSpec ic = with(kTurbo, 8, 3500);
    ic.intercooler = 0.7;
    const DynoResult ric = RunDyno(ic);
    Check(ric.peak_power_kw > turbo.peak_power_kw && ric.knock_margin > turbo.knock_margin,
          "intercooler: more power, more knock margin");
    EngineSpec lo = with(kRoots, 5), hi = with(kRoots, 10);
    lo.fuel_octane = hi.fuel_octane = 112;
    Check(RunDyno(hi).peak_power_kw > RunDyno(lo).peak_power_kw * 1.08, "more boost (good fuel): more power");
    // Draw-through carb passes the ambient-equivalent flow, so a single 600 on a blown engine chokes.
    EngineSpec small = with(kRoots, 7);
    small.carb_cfm = 600;
    EngineSpec na600 = na;
    na600.carb_cfm = 600;
    const double loss_blown = 1.0 - RunDyno(small).peak_power_kw / roots.peak_power_kw;
    const double loss_na = 1.0 - RunDyno(na600).peak_power_kw / rn.peak_power_kw;
    Check(loss_blown > 0.06 && loss_blown > 1.5 * loss_na, "600 cfm carb chokes a blown 350 (vs 1200)",
          "-%.1f%% blown vs -%.1f%% NA", 100.0 * loss_blown, 100.0 * loss_na);
    // Intake runner tuning matters much less with a blower.
    EngineSpec tr = na, trb = with(kRoots, 10);
    tr.intake = trb.intake = kTunnelRam;
    trb.fuel_octane = 112;
    EngineSpec dpb = with(kRoots, 10);
    dpb.fuel_octane = 112;
    const double tune_na = Band(RunDyno(tr), 6000, 7000) / Band(rn, 6000, 7000);
    const double tune_b = Band(RunDyno(trb), 6000, 7000) / Band(RunDyno(dpb), 6000, 7000);
    Check(tune_b < tune_na, "intake tuning damped with a blower", "%.3f vs %.3f NA", tune_b, tune_na);

    // Smoothness with a blower.
    double worst = 0.0;
    for (int type = kRoots; type <= kTurbo; ++type)
      for (double psi : {4.0, 8.0, 15.0}) {
        EngineSpec s = with(type, psi);
        s.fuel_octane = 110;
        const DynoResult r = RunDyno(s);
        worst = std::fmax(worst, MaxJump(r, r.safe_rpm + 500.0));
      }
    Check(worst < 0.06, "blowers 4-15 psi smooth (max step < 6% of peak)", "%.3f", worst);
  }
  {
    // Knock: boost raises the octane requirement (~1 / psi without an intercooler).
    EngineSpec hi = base, lo = base;
    hi.compression = 10.5; lo.compression = 8.0;
    hi.fuel_octane = lo.fuel_octane = 100;
    hi.carb_cfm = lo.carb_cfm = 1200;
    const DynoResult hna = RunDyno(hi);
    hi.blower = lo.blower = kRoots;
    hi.boost_psi = lo.boost_psi = 8;
    const DynoResult hb = RunDyno(hi), lb = RunDyno(lo);
    Check(hna.knock_margin > 0.0 && hb.knock_margin < -3.0, "10.5:1 + 8 psi on 100 octane knocks hard",
          "%+.1f NA -> %+.1f", hna.knock_margin, hb.knock_margin);
    Check(lb.knock_margin > 0.0, "8:1 + 8 psi on 100 octane is fine", "%+.1f", lb.knock_margin);
    Check(hb.peak_power_kw < lb.peak_power_kw, "knocking high-CR blown engine makes less than the 8:1",
          "%.0f vs %.0f kW", hb.peak_power_kw, lb.peak_power_kw);
    const double per_psi = (hna.knock_margin - hb.knock_margin) / 8.0;
    Check(per_psi > 0.8 && per_psi < 1.3, "about 1 octane per psi, non-intercooled", "%.2f", per_psi);
    hi.intercooler = 0.7;
    Check(RunDyno(hi).knock_margin > hb.knock_margin + 1.5, "intercooler lowers the requirement");
  }
  {
    // Nitrous: +kW above the arming rpm, ramped in, a little knock.
    EngineSpec n = base;
    n.nitrous_kw = 100;
    n.nitrous_rpm = 3000;
    const DynoResult rn = RunDyno(n);
    const double dkw = rn.peak_power_kw - b.peak_power_kw;
    Check(dkw > 85.0 && dkw < 101.0, "100 kW nitrous: +85..101 kW peak", "%+.1f kW", dkw);
    Check(std::fabs(TorqueAt(rn, 2500) / TorqueAt(b, 2500) - 1.0) < 1e-3, "nitrous: nothing below the arming rpm");
    Check(rn.knock_margin < b.knock_margin - 1.0 && rn.knock_margin > b.knock_margin - 4.0,
          "nitrous: knock margin down a little", "%+.1f -> %+.1f", b.knock_margin, rn.knock_margin);
    // Smooth ramp: the added torque rises monotonically through the ramp (3000-3300) and never exceeds kW / omega.
    bool mono = true, bounded = true;
    double prev = 0.0;
    const DynoResult fn = RunDyno(n, 4000), fb = RunDyno(base, 4000);
    for (double rpm = 2600; rpm <= 3800; rpm += 20) {
      const double d = TorqueAt(fn, rpm) - TorqueAt(fb, rpm);
      if (rpm <= 3300) mono = mono && d >= prev - 1e-6;  // through the ramp; above it kW / omega falls
      bounded = bounded && d <= 1.001 * 100e3 / (rpm * 2.0 * 3.14159265358979 / 60.0);
      prev = d;
    }
    Check(mono && bounded, "nitrous ramps in smoothly, at most kW / omega");
    EngineSpec big = n, huge = n;
    big.nitrous_kw = 400; huge.nitrous_kw = 5000;
    Check(RunDyno(huge).peak_power_kw == RunDyno(big).peak_power_kw, "nitrous clamped to 400 kW");
  }
  {
    // No NaN / inf for any boost 0-25 psi, any type, odd inputs; random blown engines stay finite and smooth enough.
    bool ok = true;
    for (int type = -1; type <= 6; ++type)
      for (double psi = 0.0; psi <= 25.0; psi += 0.5)
        for (double brpm : {0.0, 500.0, 2500.0, 6000.0}) {
          EngineSpec s = base;
          s.blower = type; s.boost_psi = psi; s.boost_rpm = brpm; s.nitrous_kw = psi * 10.0;
          const DynoResult r = RunDyno(s);
          ok = ok && AllFinite(r) && std::isfinite(r.peak_boost_psi) && std::isfinite(r.blower_loss_kw) &&
               r.peak_torque_nm > 0.0;
        }
    EngineSpec odd = base;
    odd.blower = kTurbo; odd.boost_psi = std::nan(""); odd.boost_rpm = -1e9; odd.blower_efficiency = 1e9;
    odd.intercooler = -5; odd.nitrous_kw = std::nan(""); odd.nitrous_rpm = std::nan("");
    EngineSpec zero;
    std::memset(static_cast<void*>(&zero), 0, sizeof(zero));
    zero.blower = kRoots; zero.boost_psi = 25; zero.nitrous_kw = 400; zero.nitrous_rpm = 0;
    for (const EngineSpec* s : {&odd, &zero})
      for (double rmax : {0.0, 3000.0, 20000.0}) ok = ok && AllFinite(RunDyno(*s, rmax));
    Check(ok, "boost 0-25 psi, every type, odd inputs: finite");

    unsigned state = 777u;
    auto rnd = [&](double lo, double hi) {
      state = state * 1664525u + 1013904223u;
      return lo + (hi - lo) * ((state >> 8) & 0xFFFFFF) / double(0xFFFFFF);
    };
    int bad = 0, rough = 0;
    double worst = 0.0;
    for (int i = 0; i < 1500; ++i) {
      EngineSpec s = base;
      s.compression = rnd(7.0, 11.0); s.fuel_octane = rnd(91, 112); s.carb_cfm = rnd(0, 1) < 0.3 ? 0.0 : rnd(400, 1600);
      s.carbureted = s.carb_cfm > 0.0; s.intake_duration = rnd(195, 260); s.exhaust_duration = s.intake_duration + 8;
      s.header = int(rnd(0, 4.99)); s.intake = int(rnd(0, 4.99)); s.lifter = int(rnd(0, 3.99));
      s.blower = int(rnd(1, 4.99)); s.boost_psi = rnd(0, 25); s.boost_rpm = rnd(0, 1) < 0.5 ? 0.0 : rnd(2000, 7000);
      s.blower_efficiency = rnd(0, 1) < 0.5 ? 0.0 : rnd(0.45, 0.8); s.intercooler = rnd(0, 0.8);
      const DynoResult r = RunDyno(s);
      if (!AllFinite(r) || r.peak_torque_nm <= 0.0) ++bad;
      const double j = MaxJump(r, r.safe_rpm + 500.0);
      worst = std::fmax(worst, j);
      rough += j > 0.08;
    }
    Check(bad == 0, "fuzz: 1500 random blown engines finite", "%.0f bad", bad);
    Check(rough == 0, "fuzz: blown max step < 8% of peak to safe rpm + 500", "worst %.3f", worst);
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
    // Every part on every engine it fits: finite, smooth, a build/stock ratio inside 0.5..1.8 over 1500 rpm .. stock
    // safe rpm, and no single part adding more than 45% peak power. Blower and nitrous parts are meant to add far more,
    // so they have their own rules: finite, a ratio within 0.5..3.2 (the physics clamp), a blower smooth (step < 8%;
    // the nitrous ramp is a deliberate ~300 rpm step), and their highest ratio, best gain and worst knock reported.
    bool ok = true, fi_ok = true;
    int ratio_bad = 0, gain_bad = 0, builds = 0, fi_builds = 0, fi_low = 0, fi_knock = 0;
    double rmin = 9.0, rmax = 0.0, gmax = 0.0, fi_rmin = 9.0, fi_rmax = 0.0, fi_gmax = 0.0, fi_jump = 0.0, fi_kmin = 99.0;
    std::string rmin_at, rmax_at, gmax_at, fi_rmin_at, fi_rmax_at, fi_gmax_at, fi_kmin_at;
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
        const std::string tag = std::to_string(id) + "+" + std::to_string(pid) + " " + p.name;
        const double g = rp.peak_power_kw / r.peak_power_kw - 1.0;
        if (p.slot == kBlowerSlot || p.slot == kNitrousSlot) {
          ++fi_builds;
          const double j = p.slot == kBlowerSlot ? MaxJump(rp, rp.safe_rpm + 500.0) : 0.0;
          fi_jump = std::fmax(fi_jump, j);
          if (!AllFinite(rp) || rp.peak_torque_nm <= 0.0 || j > 0.08) {
            fi_ok = false;
            std::printf("      engine %d + part %d not sane\n", int(id), int(pid));
          }
          bool low = false;
          for (double rpm = 1500.0; rpm <= r.safe_rpm; rpm += 100.0) {
            const double q = TorqueAt(rp, rpm) / TorqueAt(r, rpm);
            if (q < fi_rmin) { fi_rmin = q; fi_rmin_at = tag + " @" + std::to_string(int(rpm)); }
            if (q > fi_rmax) { fi_rmax = q; fi_rmax_at = tag + " @" + std::to_string(int(rpm)); }
            low = low || q < 0.5 || q > 3.2;
          }
          fi_low += low;
          fi_knock += rp.knock_margin < 0.0;
          if (rp.knock_margin < fi_kmin) { fi_kmin = rp.knock_margin; fi_kmin_at = tag; }
          if (g > fi_gmax) { fi_gmax = g; fi_gmax_at = tag; }
          continue;
        }
        ++builds;
        if (!AllFinite(rp) || MaxJump(rp, rp.safe_rpm + 500.0) > 0.06 || rp.peak_torque_nm <= 0.0) {
          ok = false;
          std::printf("      engine %d + part %d not sane\n", int(id), int(pid));
        }
        bool rb = false;
        for (double rpm = 1500.0; rpm <= r.safe_rpm; rpm += 100.0) {
          const double q = TorqueAt(rp, rpm) / TorqueAt(r, rpm);
          if (q < rmin) { rmin = q; rmin_at = tag + " @" + std::to_string(int(rpm)); }
          if (q > rmax) { rmax = q; rmax_at = tag + " @" + std::to_string(int(rpm)); }
          rb = rb || q < 0.5 || q > 1.8;
        }
        ratio_bad += rb;
        if (g > gmax) { gmax = g; gmax_at = tag; }
        gain_bad += g > 0.45;
      }
    }
    std::printf("      %d single-part builds; ratio range %.3f (%s) .. %.3f (%s); best power gain %+.1f%% (%s)\n",
                builds, rmin, rmin_at.c_str(), rmax, rmax_at.c_str(), 100.0 * gmax, gmax_at.c_str());
    Check(ratio_bad == 0, "single-part build/stock ratio within 0.5..1.8", "%.0f builds outside", ratio_bad);
    Check(gain_bad == 0, "no single part adds > 45% peak power", "%.0f parts over", gain_bad);
    Check(ok, "every stock engine and single-part build is finite and smooth");
    if (fi_builds) {
      std::printf("      %d blower / nitrous builds; ratio range %.3f (%s) .. %.3f (%s); best power gain %+.1f%% (%s); "
                  "worst blower step %.3f\n", fi_builds, fi_rmin, fi_rmin_at.c_str(), fi_rmax, fi_rmax_at.c_str(),
                  100.0 * fi_gmax, fi_gmax_at.c_str(), fi_jump);
      std::printf("      %d of them knock (margin < 0); worst %+.1f (%s)\n", fi_knock, fi_kmin, fi_kmin_at.c_str());
      Check(fi_ok, "blower / nitrous builds finite, blowers smooth (< 8%)");
      Check(fi_low == 0, "blower / nitrous build/stock ratio within 0.5..3.2", "%.0f builds outside", fi_low);
    }
  }

  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
