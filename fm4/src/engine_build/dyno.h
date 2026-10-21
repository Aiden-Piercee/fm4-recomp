// ---------------------------------------------------------------------------
// Engine builder: build-time engine model (Forza-4-Evolution).
// Spec: docs/ENGINE_BUILDER.md (section "Engine model"). Cleanroom: written from that spec and public engine theory
// only, with no code, tables or constants from any other game.
//
// Pure C++17, no game or SDK headers, so it builds and tests on any host (tools/engine_build/dyno_test.cpp).
// The model is only used as a RATIO against the same model's stock build (see EngineBuilderTorqueRatio), so it has to
// get the direction and size of a change right, not reproduce a stock curve exactly.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>

namespace eb {

// Lifter / valvetrain style. Sets how hard the valvetrain can be pushed and the shape of the lift curve.
enum Lifter : int { kHydraulicFlat = 0, kSolidFlat = 1, kHydraulicRoller = 2, kSolidRoller = 3 };
// Intake manifold style.
enum Intake : int { kDualPlane = 0, kSinglePlane = 1, kTunnelRam = 2, kLongRunnerEfi = 3, kLogManifold = 4 };
// Exhaust manifold style.
enum Header : int { kCastLog = 0, kCastPerformance = 1, kShortyHeader = 2, kLongTubeHeader = 3, kTriYHeader = 4 };
// Ignition system.
enum Ignition : int { kPoints = 0, kElectronic = 1, kCapacitiveDischarge = 2 };

// A fully resolved engine: the stock geometry with every fitted part applied. Units are metric; angles in crank
// degrees. Every field has a plausible default so a partly filled spec still runs.
struct EngineSpec {
  // Bottom end
  double bore_mm = 101.6;
  double stroke_mm = 88.4;
  int cylinders = 8;
  double rod_length_mm = 0.0;     // 0 = 1.7 x stroke
  double compression = 9.5;       // static ratio
  double rotating_mass_kg = 35.0; // crank + rods + pistons (inertia, informational in the MVP)
  double rod_limit_rpm = 7000.0;  // where the rods start to be at risk (informational in the MVP)

  // Heads
  int valves_per_cyl = 2;
  double intake_valve_mm = 49.0;
  double exhaust_valve_mm = 39.0;
  double port_flow = 1.0;         // port efficiency, 1.0 = good production head, 1.3 = full race port
  double port_volume_cc = 0.0;    // 0 = sized for the valve; a large port flows more but slows the charge at low rpm

  // Cam and valvetrain
  double intake_duration = 200.0;  // degrees at 0.050 in (1.27 mm) lift
  double exhaust_duration = 210.0;
  double intake_lift_mm = 10.0;    // at the valve
  double exhaust_lift_mm = 10.3;
  double lobe_separation = 112.0;  // degrees (cam degrees, as cams are specified)
  double intake_centerline = 0.0;  // 0 = lobe_separation - 4 (typical 4 degrees advance)
  int lifter = kHydraulicFlat;
  double rocker_ratio = 1.5;       // lift above is quoted at this ratio; a higher ratio scales lift
  double nominal_rocker_ratio = 1.5;
  double valve_float_rpm = 0.0;    // 0 = derived from lifter, lift and spring; springs parts set it directly

  // Induction
  int intake = kDualPlane;
  double runner_length_mm = 0.0;   // 0 = typical for the intake style
  double carb_cfm = 600.0;         // total airflow rating at 1.5 inHg; 0 = unrestricted (EFI sized to the engine)
  int carbureted = 1;
  double air_filter_restriction = 0.03;  // 0 = open, 0.1 = restrictive stock air cleaner

  // Exhaust
  int header = kCastLog;
  double primary_diameter_mm = 0.0;  // 0 = typical for the style
  double primary_length_mm = 0.0;    // 0 = typical for the style
  double pipe_diameter_mm = 57.0;    // each side
  int dual_exhaust = 1;
  double muffler_restriction = 0.5;  // 0 = open pipes, 1 = very restrictive

  // Ignition and fuel
  int ignition = kPoints;
  double fuel_octane = 98.0;  // research octane of the fuel the engine is tuned for (period premium leaded ~ 100)
};

// Result: torque along evenly spaced rpm points from 0 to rpm_max, plus summary numbers.
struct DynoResult {
  static constexpr int kPoints = 96;
  double rpm_max = 0.0;
  double torque_nm[kPoints] = {};  // wide open throttle, brake torque (after friction)
  double peak_torque_nm = 0.0, peak_torque_rpm = 0.0;
  double peak_power_kw = 0.0, peak_power_rpm = 0.0;
  double valve_float_rpm = 0.0;    // where valve control is lost (torque falls off hard above)
  double safe_rpm = 0.0;           // min(valve float, rod limit): a sensible rev limit for this build
  double idle_rpm = 0.0;           // rough idle speed implied by the cam (overlap)
  double knock_margin = 0.0;       // < 0 means the compression is too high for the fuel (torque is reduced)
  double rpm_at(int i) const { return rpm_max * i / (kPoints - 1); }
};

// Runs the model. rpm_max sets the range of the result (the model works to at least 12000 rpm).
DynoResult RunDyno(const EngineSpec& spec, double rpm_max = 10000.0);

// Linear interpolation of a result at any rpm (clamped to the range; 0 below 0).
double TorqueAt(const DynoResult& r, double rpm);

// Displacement in cubic centimetres.
double DisplacementCc(const EngineSpec& spec);

}  // namespace eb
