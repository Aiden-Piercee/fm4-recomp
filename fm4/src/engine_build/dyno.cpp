// ---------------------------------------------------------------------------
// Engine builder: build-time engine model (Forza-4-Evolution). Interface: dyno.h.
// Spec: docs/ENGINE_BUILDER.md, "Engine model (behaviour spec)". Cleanroom: written from that spec and public engine
// theory only (Heywood, Internal Combustion Engine Fundamentals; Taylor, The Internal Combustion Engine in Theory and
// Practice; ordinary hot-rodding rules of thumb). Every constant below is our own and was tuned by hand so that
// typical period engines land near their published outputs and their stock FM4 curve shapes.
//
// Shape of the model (one quasi-steady cycle per rpm point, wide open throttle):
//
//   torque = (eta_i * VE * rho_air * LHV / AFR * exhaust * ignition * knock - fmep) * Vd / (4 pi)
//
//   VE is a product of independent factors, each a smooth function of rpm:
//     * cam filling   trapped fraction at intake valve closing (slider-crank geometry) at low rpm, rising to full
//                     (plus a little inertia ram for a late closing) above a "cam rpm" that grows with the closing
//                     angle and falls with charge velocity (piston speed over effective valve area);
//     * valve choke   Taylor's inlet Mach index Z = (piston area / effective valve area) x piston speed / sound
//                     speed; VE rolls off past Z ~ 0.6. Effective area = valves x Cd x curtain area (lift saturating
//                     at D/4) x port efficiency x a time-area factor from duration. The exhaust valve has the same
//                     form with a higher threshold;
//     * overlap       dilution at low rpm (worse with backpressure), scavenging gain at high rpm only with headers;
//     * intake tune   a log-rpm bell per manifold style centred on 1/runner length, plus a low-rpm loss for big
//                     plenums and a flow loss that grows with the square of airflow;
//     * restrictions  carburettor (rated cfm at 1.5 inHg, so depression = 1.5 inHg x (cfm/rated)^2, steepening past
//                     the rating), air filter, manifold. Airflow and depression depend on each other, so the
//                     operating point is solved by bisection (monotone, exact, smooth in rpm);
//     * header tune   a bell centred on the classic primary-length rule rpm = 850 x (180 + EVO) / (length_in + 3).
//   Forced induction (only when a blower is fitted; see "Forced induction" below) multiplies the trapped charge by
//   the compressor's density ratio. Nitrous adds a fixed power above its arming rpm.
//   Exhaust backpressure (manifold + pipe + muffler, each K x dynamic pressure of the hot gas) costs torque as
//   exp(-0.25 x backpressure/atm). Combustion: Otto efficiency at gamma 1.3 scaled to real engines (times spark_tune
//   for smog-era calibrations), a knock limit from compression (lowered by late intake closing) against fuel octane,
//   a spark loss for points / electronic at high rpm. Friction: fmep rising with mean piston speed plus valve spring
//   load. Valve float: a smooth Gaussian roll-off above the float rpm.
//
// Every non-smooth operation (max, step) is a softplus or logistic so the curve has no kinks; inputs are sanitised
// first so a zero, a NaN or an out-of-range catalogue value can not produce NaN or inf. 96 points take well
// under a millisecond.
//
// Forced induction and nitrous (Heywood ch. 6-7, Taylor vol. 1 ch. 13, Bell "Maximum Boost"; constants our own):
//   * boost vs rpm: Roots / screw (positive displacement) reach the set boost by ~1500 rpm, the drop below that is
//     rotor leakage, a Roots sags a little at very high rpm; centrifugal boost ~ rpm^2, softly capped at the set
//     boost; a turbo has no boost well below its spool rpm and rises over ~1000-1500 rpm (on rpm x core VE, i.e.
//     exhaust flow, quasi-steady with no lag) to the set boost, then the wastegate holds it;
//   * charge: PR = (14.7 + boost) / 14.7 on top of the depressed manifold pressure. Compressor rise
//     dT/T1 = (PR^((g-1)/g) - 1) / eta, the intercooler takes ic x dT, density ratio = PR x T1 / T2. A carburettor
//     is draw-through (upstream of the compressor): it passes the ambient-equivalent flow (VE x density ratio), so a
//     single four-barrel chokes a blown engine, and the fuel evaporating during compression takes ~40% of dT. EFI
//     injects after the compressor. Positive manifold pressure purges residuals (Heywood's ideal-cycle VE with
//     p_exh / p_int), scales the backpressure loss by p_exh / p_int, and does positive pumping work on the intake
//     stroke. Runner tuning is damped with boost;
//   * drive: mechanical blowers take m_dot cp dT (+ belt and bearing drag) as a mean effective pressure; a turbo
//     takes nothing from the crank but its turbine raises exhaust backpressure ~ boost;
//   * knock: octane requirement + 0.55 per psi (pressure) + 0.07 per K of charge temperature rise after the
//     intercooler (~1 octane / psi for a non-intercooled period blower), + 0.02 per nitrous kW; knock_margin is the
//     worst point in the range;
//   * nitrous: torque = kW / omega, ramped in over ~300 rpm above the arming rpm, tapering slightly at high rpm.
// With no blower and no nitrous none of this runs, and the result is bit-for-bit the naturally aspirated model.
// ---------------------------------------------------------------------------
#include "dyno.h"

#include <algorithm>
#include <cmath>

namespace eb {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kAtmPa = 101325.0;
constexpr double kRhoAir = 1.184;       // kg/m^3, 25 C sea level
constexpr double kRhoExhaust = 0.55;    // kg/m^3, hot exhaust in the pipes
constexpr double kSoundSpeed = 346.0;   // m/s at the inlet
constexpr double kFuelLhv = 44.0e6;     // J/kg, gasoline
constexpr double kPowerAfr = 12.6;      // best-power air/fuel ratio
constexpr double kCcPerCubicFoot = 28316.85;
constexpr double kAtmPsi = 14.696;
constexpr double kInletK = 298.0;       // compressor inlet temperature, K
constexpr double kCpAir = 1005.0;       // J/(kg K)
constexpr double kGammaExp = 0.4 / 1.4; // (gamma - 1) / gamma for air

// Smooth max(x, 0) with knee width k.
double SoftPos(double x, double k) {
  const double t = x / k;
  if (t > 30.0) return x;
  if (t < -30.0) return 0.0;
  return k * std::log1p(std::exp(t));
}
double Logistic(double x) {
  if (x > 40.0) return 1.0;
  if (x < -40.0) return 0.0;
  return 1.0 / (1.0 + std::exp(-x));
}
double Bell(double x) { return std::exp(-x * x); }
// x^2 / (1 + x^2): 0 at 0, 0.5 at 1, -> 1.
double Rise(double x) { return x * x / (1.0 + x * x); }
// Sharper version for the cam's filling transition: x^3 / (1 + x^3).
double Rise3(double x) { return x * x * x / (1.0 + x * x * x); }

double Sane(double v, double lo, double hi, double fallback) {
  if (!std::isfinite(v)) return fallback;
  return std::min(hi, std::max(lo, v));
}
int SaneInt(int v, int lo, int hi, int fallback) { return (v < lo || v > hi) ? fallback : v; }

// Per-style tables. Index = enum value in dyno.h.
struct IntakeStyle {
  double runner_mm;    // typical effective runner length
  double tune_rpm;     // tuned rpm at that length
  double width;        // bell width in ln(rpm)
  double gain;         // VE gain at the tuned rpm
  double low_loss;     // VE loss at very low rpm (big plenum, weak signal, poor distribution)
  double low_rpm;      // where that loss has halved
  double flow_k;       // fractional depression at the engine's 6000 rpm airflow
};
constexpr IntakeStyle kIntakes[5] = {
    {440.0, 3500.0, 0.62, 0.050, 0.000, 2500.0, 0.040},  // dual plane: broad and low, restrictive at the top
    {300.0, 5600.0, 0.50, 0.060, 0.035, 2800.0, 0.028},  // single plane
    {240.0, 7000.0, 0.38, 0.085, 0.090, 3600.0, 0.012},  // tunnel ram: highest, narrowest, soft low down
    {430.0, 3900.0, 0.55, 0.085, 0.000, 2500.0, 0.045},  // long-runner EFI: broad mid-range
    {200.0, 3000.0, 0.70, 0.010, 0.020, 2500.0, 0.110},  // log manifold: poor
};

struct HeaderStyle {
  double primary_mm;   // typical primary (or runner) length; 0 = untuned
  double length_mult;  // effective length factor for the tuning rule (Tri-Y merges act longer)
  double width;        // bell width in ln(rpm)
  double gain;         // VE gain at the tuned rpm
  double scavenge;     // 0..1, how well the exhaust pulls the cylinder during overlap
  double flow_k;       // loss coefficient on the reference dynamic pressure
  double dia_mult;     // typical primary diameter relative to the ideal for the cylinder size
};
constexpr HeaderStyle kHeaders[5] = {
    {200.0, 1.0, 0.80, 0.000, 0.12, 1.70, 0.85},  // cast log
    {330.0, 1.0, 0.65, 0.015, 0.35, 1.05, 0.92},  // cast performance (ram's horn, tubular-look cast)
    {420.0, 1.0, 0.70, 0.030, 0.60, 0.60, 1.00},  // shorty header
    {810.0, 1.0, 0.45, 0.065, 1.00, 0.38, 1.00},  // long-tube header
    {760.0, 1.3, 0.62, 0.050, 0.85, 0.48, 1.00},  // tri-Y: broad mid-range
};

// Valve float base by lifter at 11.4 mm valve lift and a 51 mm intake valve.
constexpr double kFloatBase[4] = {6300.0, 7000.0, 7300.0, 8400.0};
// Valve spring load factor (friction) by lifter.
constexpr double kSpringLoad[4] = {1.0, 1.35, 1.25, 1.75};
// Cam/lifter contact friction relief for rollers, kPa.
constexpr double kRollerRelief[4] = {0.0, 0.0, 6.0, 6.0};

// Everything that does not depend on rpm.
struct Engine {
  double bore, stroke, rod, cr;           // m, m, m, -
  int cyl;
  double piston_area, vd_cyl, vd_total;   // m^2, m^3, m^3
  double clearance;                       // m^3 per cylinder
  double area_ratio_in, area_ratio_ex;    // piston area / effective valve area
  double charge_vel_ref;                  // relative charge velocity per rpm (1 = reference small block)
  double ivc_adv, evo_adv, overlap;       // degrees (advertised IVC ABDC, EVO BBDC; overlap at 0.050 in)
  double trap_low;                        // VE limit at very low rpm from intake closing
  double cam_rpm, ram;                    // where inertia filling takes over, ram gain
  double overlap_pos;                     // softplus of the overlap that matters
  double port_ratio;                      // port volume / port sized for the valve
  const IntakeStyle* intake;
  double intake_tune_rpm;
  const HeaderStyle* header;
  double header_tune_rpm, header_k;
  double carb_cfm, demand_cfm_6000;       // rated, and this engine's 6000 rpm airflow at VE 1
  bool carbureted;
  double filter;
  double pipe_area, side_split;           // m^2 per side, fraction of flow per pipe
  double muffler;
  double eta_i;                           // indicated efficiency
  double knock_margin;
  int ignition, lifter;
  double lift_in;                         // intake valve lift (mm) at the valve
  double fmep_rod_mult;
  double float_rpm, safe_rpm, idle_rpm;
  // Forced induction and nitrous.
  int blower;
  double boost, boost_rpm, blower_eta, ic, evap;  // psi, rpm, -, -, - (draw-through fuel cooling share of dT)
  double spool_width, ve_core_ref;                // turbo: rpm over which boost builds; core VE at boost_rpm
  double n2o_kw, n2o_rpm;
  bool boosted() const { return blower != kNoBlower; }
  bool forced() const { return blower != kNoBlower || n2o_kw > 0.0; }
};

// Per-rpm extras the summary needs (only filled on the forced-induction / nitrous path).
struct PointInfo {
  double boost_psi = 0.0, drive_kw = 0.0, knock_margin = 0.0;
};

double CylVolume(const Engine& e, double crank_deg) {
  const double r = 0.5 * e.stroke, l = e.rod;
  const double th = crank_deg * kPi / 180.0;
  const double sn = std::sin(th);
  const double x = r * std::cos(th) + std::sqrt(std::max(l * l - r * r * sn * sn, 1e-12));
  return e.clearance + e.piston_area * (r + l - x);
}

// Effective lift (mm) saturating at a quarter of the valve diameter, where curtain area equals valve area.
double EffectiveLift(double lift, double dia) {
  const double sat = 0.25 * dia;
  return lift / std::cbrt(1.0 + std::pow(lift / sat, 3.0));
}

double VeCore(const Engine& e, double rpm);

Engine Prepare(const EngineSpec& in) {
  Engine e{};
  const double bore_mm = Sane(in.bore_mm, 40.0, 160.0, 101.6);
  const double stroke_mm = Sane(in.stroke_mm, 40.0, 160.0, 88.4);
  e.cyl = SaneInt(in.cylinders, 1, 16, 8);
  double rod_mm = Sane(in.rod_length_mm, 0.0, 400.0, 0.0);
  if (rod_mm < 1.2 * stroke_mm) rod_mm = 1.7 * stroke_mm;  // 0 (or nonsense) = typical
  e.bore = bore_mm * 1e-3;
  e.stroke = stroke_mm * 1e-3;
  e.rod = rod_mm * 1e-3;
  e.cr = Sane(in.compression, 6.0, 16.0, 9.5);
  e.piston_area = 0.25 * kPi * e.bore * e.bore;
  e.vd_cyl = e.piston_area * e.stroke;
  e.vd_total = e.vd_cyl * e.cyl;
  e.clearance = e.vd_cyl / (e.cr - 1.0);

  // Valves and ports.
  const int vpc = SaneInt(in.valves_per_cyl, 1, 5, 2);
  const double ivd = Sane(in.intake_valve_mm, 15.0, 80.0, 49.0);
  const double evd = Sane(in.exhaust_valve_mm, 12.0, 70.0, 0.8 * ivd);
  const double port = Sane(in.port_flow, 0.5, 1.8, 1.0);
  const double rocker = Sane(in.rocker_ratio, 1.0, 2.5, 1.5);
  const double rocker_nom = Sane(in.nominal_rocker_ratio, 1.0, 2.5, 1.5);
  const double ilift = Sane(in.intake_lift_mm, 3.0, 25.0, 10.0) * rocker / rocker_nom;
  const double elift = Sane(in.exhaust_lift_mm, 3.0, 25.0, ilift) * rocker / rocker_nom;
  const double idur = Sane(in.intake_duration, 140.0, 330.0, 200.0);
  const double edur = Sane(in.exhaust_duration, 140.0, 340.0, idur + 8.0);
  const double nominal_port_cc = 0.072 * ivd * ivd * std::sqrt(4.0 / (vpc + 2.0));  // ~190 cc for a 2.02 in valve
  const double port_cc = Sane(in.port_volume_cc, 0.0, 600.0, 0.0);
  e.port_ratio = port_cc > 1.0 ? std::min(1.7, std::max(0.6, port_cc / nominal_port_cc)) : 1.0;
  e.lift_in = ilift;

  // Time-area factor: the valve spends longer open with more duration (advertised ~ 0.050 in figure + 50).
  const double tin = std::pow((idur + 50.0) / 272.0, 1.2);
  const double tex = std::pow((edur + 50.0) / 280.0, 1.2);
  constexpr double kCd = 0.36;  // mean flow coefficient over the event, on curtain area
  const double a_in = vpc * kCd * port * std::pow(e.port_ratio, 0.2) * kPi * ivd * EffectiveLift(ilift, ivd) * tin;
  const double a_ex = vpc * kCd * kPi * evd * EffectiveLift(elift, evd) * tex;
  e.area_ratio_in = e.piston_area * 1e6 / a_in;
  e.area_ratio_ex = e.piston_area * 1e6 / a_ex;
  // Charge velocity per rpm relative to a 350 small block on 2.02 valves (area ratio ~ 7.3, stroke 88.4).
  e.charge_vel_ref = (e.area_ratio_in * stroke_mm / std::pow(e.port_ratio, 0.5)) / (7.3 * 88.4);

  // Cam timing.
  const double lsa = Sane(in.lobe_separation, 100.0, 124.0, 112.0);
  double icl = Sane(in.intake_centerline, 0.0, 130.0, 0.0);
  if (icl < 90.0) icl = lsa - 4.0;
  const double ecl = 2.0 * lsa - icl;
  const double ivc050 = icl + 0.5 * idur - 180.0;
  const double evo050 = ecl + 0.5 * edur - 180.0;
  e.ivc_adv = Sane(ivc050 + 25.0, 15.0, 120.0, 60.0);
  e.evo_adv = Sane(evo050 + 25.0, 20.0, 130.0, 70.0);
  e.overlap = 0.5 * (idur + edur) - 2.0 * lsa;
  e.overlap_pos = SoftPos(e.overlap + 12.0, 5.0);
  e.trap_low = CylVolume(e, 180.0 + e.ivc_adv) / CylVolume(e, 180.0);
  // A later closing needs more charge momentum, so the cam "comes in" higher; a faster charge brings it in lower.
  e.cam_rpm = 68.0 * e.ivc_adv / std::pow(std::max(0.3, e.charge_vel_ref), 0.5);
  e.ram = 0.11 * SoftPos(e.ivc_adv - 30.0, 5.0) / 32.0;

  // Induction.
  const int intake = SaneInt(in.intake, 0, 4, kDualPlane);
  e.intake = &kIntakes[intake];
  const double runner = Sane(in.runner_length_mm, 0.0, 1200.0, 0.0);
  e.intake_tune_rpm = e.intake->tune_rpm * (runner > 50.0 ? e.intake->runner_mm / runner : 1.0);
  e.carbureted = in.carbureted != 0;
  e.carb_cfm = Sane(in.carb_cfm, 0.0, 4000.0, 0.0);
  if (e.carb_cfm < 50.0) e.carb_cfm = 0.0;  // unrestricted
  e.demand_cfm_6000 = e.vd_total * 1e6 * 3000.0 / kCcPerCubicFoot;
  e.filter = Sane(in.air_filter_restriction, 0.0, 1.0, 0.03);

  // Exhaust.
  const int header = SaneInt(in.header, 0, 4, kCastLog);
  e.header = &kHeaders[header];
  const double ideal_pdia = 44.5 * std::sqrt(e.vd_cyl * 1e6 / 716.0);
  double pdia = Sane(in.primary_diameter_mm, 0.0, 90.0, 0.0);
  if (pdia < 15.0) pdia = ideal_pdia * e.header->dia_mult;
  const double dia_ratio = pdia / ideal_pdia;
  double plen = Sane(in.primary_length_mm, 0.0, 2000.0, 0.0);
  if (plen < 50.0) plen = e.header->primary_mm;
  const double plen_in = plen * e.header->length_mult / 25.4;
  e.header_tune_rpm = 850.0 * (180.0 + e.evo_adv) / (plen_in + 3.0) * std::sqrt(dia_ratio);
  e.header_k = e.header->flow_k / std::pow(dia_ratio, 4.0);
  const double pipe_mm = Sane(in.pipe_diameter_mm, 25.0, 130.0, 57.0);
  e.pipe_area = 0.25 * kPi * pipe_mm * pipe_mm * 1e-6;
  e.side_split = in.dual_exhaust ? 0.5 : 1.0;
  e.muffler = Sane(in.muffler_restriction, 0.0, 2.0, 0.5);

  // Combustion and knock. Requirement rises with compression and bore, falls with a late intake closing.
  const double otto = 1.0 - std::pow(e.cr, -0.3);
  e.eta_i = 0.70 * otto;
  const double octane_req = 64.0 + 3.2 * e.cr - 0.15 * (e.ivc_adv - 62.0) + 0.10 * (bore_mm - 101.6);
  e.knock_margin = Sane(in.fuel_octane, 70.0, 120.0, 98.0) - octane_req;
  e.eta_i *= Sane(in.spark_tune, 0.5, 1.1, 1.0);
  e.ignition = SaneInt(in.ignition, 0, 2, kPoints);
  e.lifter = SaneInt(in.lifter, 0, 3, kHydraulicFlat);
  e.fmep_rod_mult = 1.0 + 0.15 * std::min(0.4, std::max(-0.4, 1.7 - e.rod / e.stroke));

  // Valvetrain limit and idle.
  const double flt = Sane(in.valve_float_rpm, 0.0, 20000.0, 0.0);
  e.float_rpm = flt > 1000.0 ? flt
                             : kFloatBase[e.lifter] * std::pow(11.4 / ilift, 0.35) * std::pow(51.0 / ivd, 0.25) *
                                   std::pow(1.5 / rocker, 0.2);
  e.safe_rpm = std::min(e.float_rpm, Sane(in.rod_limit_rpm, 2000.0, 20000.0, 7000.0));
  e.idle_rpm = 600.0 + 9.0 * e.overlap_pos;

  // Forced induction and nitrous.
  e.blower = SaneInt(in.blower, 0, 4, kNoBlower);
  e.boost = Sane(in.boost_psi, 0.0, 30.0, 0.0);
  e.boost_rpm = Sane(in.boost_rpm, 0.0, 15000.0, 0.0);
  if (e.boost_rpm < 1000.0) {
    if (e.blower == kCentrifugal) e.boost_rpm = 0.9 * e.safe_rpm;
    else if (e.blower == kTurbo) e.boost_rpm = std::max(2000.0, 0.5 * e.safe_rpm);
    else e.boost_rpm = 0.0;
  }
  constexpr double kTypicalEta[5] = {1.0, 0.55, 0.70, 0.72, 0.70};
  const double eta = Sane(in.blower_efficiency, 0.0, 0.9, 0.0);
  e.blower_eta = eta >= 0.3 ? eta : kTypicalEta[e.blower];
  e.ic = Sane(in.intercooler, 0.0, 0.95, 0.0);
  e.evap = e.carbureted && e.carb_cfm > 0.0 ? 0.4 : 0.0;  // draw-through: fuel evaporates during compression
  // Turbo: boost builds over ~1000-1500 rpm below the spool rpm; more boost needs more exhaust energy, so longer.
  e.spool_width = std::min(2000.0, std::max(900.0, 0.28 * e.boost_rpm * (1.0 + e.boost / 15.0)));
  e.ve_core_ref = e.blower == kTurbo ? std::max(0.1, VeCore(e, e.boost_rpm)) : 1.0;
  e.n2o_kw = Sane(in.nitrous_kw, 0.0, 400.0, 0.0);
  e.n2o_rpm = Sane(in.nitrous_rpm, 1000.0, 15000.0, 3000.0);
  return e;
}

// Intake depression (fraction of an atmosphere) at a given airflow in cfm.
// `cfm` is ambient-equivalent; `dr` is the blower's density ratio (exactly 1 naturally aspirated).
double IntakeDepression(const Engine& e, double cfm, double dr) {
  const double qd = cfm / e.demand_cfm_6000;
  double dp = e.intake->flow_k * qd * qd / dr;
  double ref = e.demand_cfm_6000;
  if (e.carb_cfm > 0.0) {
    const double qc = cfm / e.carb_cfm;
    dp += 0.05 * qc * qc * (1.0 + 0.8 * qc * qc);  // rated at 1.5 inHg; venturi steepens past its rating
    ref = e.carb_cfm;
  } else {
    dp += 0.025 * qd * qd;  // throttle body sized to the engine
  }
  const double qf = cfm / ref;
  dp += 0.5 * e.filter * qf * qf;
  return dp;
}

// Core VE: cam filling, valve choking and port size (everything before manifold and exhaust tuning).
double VeCore(const Engine& e, double rpm) {
  const double sp = 2.0 * e.stroke * rpm / 60.0;  // mean piston speed m/s

  // Cam filling: trapped fraction at intake closing, rising to full + ram above the cam rpm.
  const double xc = rpm / e.cam_rpm;
  double ve = e.trap_low + (1.0 + e.ram - e.trap_low) * Rise3(xc);

  // Valve choking (Taylor's Mach index).
  const double z_in = e.area_ratio_in * sp / kSoundSpeed;
  const double z_ex = e.area_ratio_ex * sp / kSoundSpeed;
  ve *= std::pow(1.0 + std::pow(z_in / 0.45, 4.0), -0.25);
  ve *= std::pow(1.0 + std::pow(z_ex / 0.75, 4.0), -0.25);

  // Big port: slow charge at low rpm.
  ve *= 1.0 - 0.10 * SoftPos(e.port_ratio - 1.0, 0.05) / (1.0 + (rpm / 3000.0) * (rpm / 3000.0));
  return ve;
}

// u / (1 + u^8)^(1/8): ~u below 1, smoothly capped at 1 above (u >= 0).
double SoftCap(double u) { return u / std::pow(1.0 + std::pow(u, 8.0), 0.125); }

// Gauge boost (psi) at an rpm.
double BoostAt(const Engine& e, double rpm) {
  switch (e.blower) {
    case kRoots:
    case kTwinScrew: {
      // Positive displacement: delivery per rev is fixed, rotor leakage (about fixed per second) matters only slowly.
      const double r0 = e.boost_rpm > 0.0 ? 0.5 * e.boost_rpm : (e.blower == kRoots ? 800.0 : 650.0);
      const double x = rpm / r0;
      double b = e.boost * (1.0 - std::exp(-x * x));
      if (e.blower == kRoots) b *= 1.0 - 0.08 * Rise3(rpm / 7500.0);  // hot, leaky rotors far past their sweet spot
      return b;
    }
    case kCentrifugal: {
      const double x = rpm / e.boost_rpm;
      return e.boost * SoftCap(x * x);
    }
    case kTurbo: {
      // Exhaust flow ~ rpm x VE: an "equivalent rpm" relative to the core VE at the spool rpm.
      const double rpm_flow = rpm * VeCore(e, rpm) / e.ve_core_ref;
      const double g = SoftPos(rpm_flow - (e.boost_rpm - e.spool_width), 150.0) / e.spool_width;
      return e.boost * SoftCap(1.12 * g * g);  // turbine work grows ~ flow^2 until the wastegate opens
    }
    default:
      return 0.0;
  }
}

double Torque(const Engine& e, double rpm, PointInfo* info = nullptr) {
  rpm = std::max(0.0, rpm);
  const double sp = 2.0 * e.stroke * rpm / 60.0;  // mean piston speed m/s
  double ve = VeCore(e, rpm);

  // Forced induction: boost and charge density at this rpm (independent of the restriction solve below).
  double boost = 0.0, pr = 1.0, dt_comp = 0.0, dt_charge = 0.0, dr = 1.0;
  if (e.boosted()) {
    boost = BoostAt(e, rpm);
    pr = (kAtmPsi + boost) / kAtmPsi;
    dt_comp = kInletK * (std::pow(pr, kGammaExp) - 1.0) / e.blower_eta;
    dt_charge = dt_comp * (1.0 - e.evap) * (1.0 - e.ic);
    dr = pr * kInletK / (kInletK + dt_charge);
  }

  // Intake manifold tuning (pressure waves in the runners matter much less once a blower fills the manifold).
  const IntakeStyle& is = *e.intake;
  const double li = std::log(std::max(rpm, 200.0) / e.intake_tune_rpm) / is.width;
  if (e.boosted()) ve *= 1.0 + is.gain * (1.0 - 0.7 * boost / (boost + 2.0)) * Bell(li);
  else ve *= 1.0 + is.gain * Bell(li);
  ve *= 1.0 - is.low_loss / (1.0 + (rpm / is.low_rpm) * (rpm / is.low_rpm));

  // Header tuning.
  const HeaderStyle& hs = *e.header;
  const double lh = std::log(std::max(rpm, 200.0) / e.header_tune_rpm) / hs.width;
  ve *= 1.0 + hs.gain * Bell(lh);

  // Overlap: dilution low down, scavenging up top only with a good exhaust.
  const double xr = rpm / 2300.0;
  ve *= 1.0 - 0.0026 * e.overlap_pos / (1.0 + xr * xr) * (1.4 - 0.6 * hs.scavenge);
  ve *= 1.0 + 0.0012 * e.overlap_pos * (hs.scavenge - 0.35) * Rise(rpm / 4500.0);

  // Valve float: smooth, steep roll-off above the float rpm.
  const double over = SoftPos(rpm - e.float_rpm, 250.0) / 2300.0;
  ve *= Bell(over);

  // Restrictions: solve VE_actual = VE_open * exp(-depression(airflow(VE_actual))) by bisection. With a blower the
  // carb and filter pass the ambient-equivalent flow (x density ratio; dr is exactly 1 without one); the manifold
  // flows the denser charge, so its loss is divided by dr.
  const double ve_open = ve;
  const double cfm_per_ve = e.vd_total * 1e6 * 0.5 * rpm / kCcPerCubicFoot;
  double lo = 0.0, hi = ve;
  if (cfm_per_ve > 1e-9) {
    for (int i = 0; i < 40; ++i) {
      const double mid = 0.5 * (lo + hi);
      const double f = mid - ve * std::exp(-IntakeDepression(e, mid * cfm_per_ve * dr, dr));
      (f > 0.0 ? hi : lo) = mid;
    }
    ve = 0.5 * (lo + hi);
  }
  // Oversized carb: weak venturi signal at low airflow, poorer mixture.
  if (e.carbureted && e.carb_cfm > 0.0) ve *= 1.0 - 0.06 * std::exp(-(ve * dr * cfm_per_ve / e.carb_cfm) / 0.2);
  const double p_in = ve_open > 1e-9 ? std::min(1.0, ve / ve_open) : 1.0;  // manifold pressure before the blower, atm

  // Exhaust backpressure: K x dynamic pressure of the hot gas in a reference 57 mm pipe (manifold) and the real pipe.
  const double mdot = ve * dr * kRhoAir * e.vd_total * rpm / 120.0 * (1.0 + 1.0 / kPowerAfr);
  const double side = mdot * e.side_split;
  const double ref_area = 0.25 * kPi * 0.057 * 0.057;
  const double v_ref = side / kRhoExhaust / ref_area;
  const double v_pipe = side / kRhoExhaust / e.pipe_area;
  const double q_ref = 0.5 * kRhoExhaust * v_ref * v_ref, q_pipe = 0.5 * kRhoExhaust * v_pipe * v_pipe;
  const double back_pipes = (e.header_k * q_ref + (1.4 + 7.0 * e.muffler) * q_pipe) / kAtmPa;
  // Turbine: exhaust manifold pressure rises with boost (about 0.9 x boost for a sensibly matched period turbo).
  const double back_turbine = e.blower == kTurbo ? 0.9 * boost / kAtmPsi : 0.0;
  const double back = e.blower == kTurbo ? back_pipes + back_turbine : back_pipes;
  // Backpressure costs pumping work and dilution in proportion to exhaust over intake pressure, so boost offsets it.
  double eff = e.eta_i * std::exp(-0.25 * (e.boosted() ? back / pr : back));

  // Trapped charge with a blower: density ratio, plus fewer residuals when the manifold is above exhaust pressure
  // (Heywood's ideal-cycle VE, 1 - (p_e / p_i - 1) / (gamma (r - 1)), as a ratio against no boost).
  double ve_charge = ve;
  if (e.boosted()) {
    const double pe0 = 1.0 + back_pipes, pe1 = 1.0 + back;
    const double purge = 1.0 + (pe0 / p_in - pe1 / (p_in * pr)) / (1.3 * (e.cr - 1.0));
    ve_charge = ve * dr * Sane(purge, 0.8, 1.2, 1.0);
  }

  // Combustion: heat loss at low speed, spark energy at high rpm, knock.
  const double xl = rpm / 1500.0;
  eff *= 1.0 - 0.30 / (1.0 + xl * xl);
  if (e.ignition == kPoints) eff *= 1.0 - 0.075 * Logistic((rpm - 6100.0) / 650.0);
  else if (e.ignition == kElectronic) eff *= 1.0 - 0.025 * Logistic((rpm - 8000.0) / 700.0);
  const double knock_weight = 1.15 - 0.3 * Rise(rpm / 4000.0);
  double knock = 1.0, n2o_on = 0.0, margin = e.knock_margin;
  if (e.forced()) {
    if (e.n2o_kw > 0.0) n2o_on = Logistic((rpm - e.n2o_rpm - 150.0) / 65.0);  // 10-90% over ~290 rpm
    margin -= 0.55 * boost + 0.07 * dt_charge + 0.02 * e.n2o_kw * n2o_on;
    knock = std::exp(-0.03 * SoftPos(-margin, 1.0) * knock_weight);
    eff *= knock;
  } else {
    eff *= std::exp(-0.03 * SoftPos(-e.knock_margin, 1.0) * knock_weight);
  }

  double imep = eff * ve_charge * kRhoAir * kFuelLhv / kPowerAfr;  // Pa
  double drive_mep = 0.0;
  if (e.boosted()) {
    imep += (pr - 1.0) * p_in * kAtmPa;  // positive pumping work: the manifold pushes on the intake stroke
    if (e.blower != kTurbo) {
      // Shaft power m_dot cp dT as a mean effective pressure (m_dot / rpm is fixed, so no division by rpm), plus a
      // little belt and bearing drag that grows with blower speed.
      const double belt = 1.0 + 0.04 * Rise(boost / 2.0) * (rpm / 6000.0);
      drive_mep = ve_charge * kRhoAir * kCpAir * dt_comp * belt;
    }
  }

  // Friction (fmep, kPa): rubbing + pumping vs piston speed, valve springs.
  const double spring = (e.lift_in / 11.4) * kSpringLoad[e.lifter] * (8.0 + 0.6 * sp) - kRollerRelief[e.lifter] * (1.0 + sp / 20.0);
  const double fmep = ((50.0 + 5.5 * sp + 0.30 * sp * sp) * e.fmep_rod_mult + spring) * 1e3;

  if (!e.forced()) {
    const double t = (imep - fmep) * e.vd_total / (4.0 * kPi);
    return SoftPos(t, 2.0);
  }
  double t = (imep - fmep - drive_mep) * e.vd_total / (4.0 * kPi);
  if (n2o_on > 0.0) {
    // Fixed N2O and fuel jets: about constant power, so torque = P / omega; distribution suffers a little up top.
    const double w = 2.0 * kPi * rpm / 60.0, w_min = 2.0 * kPi * 600.0 / 60.0;
    t += e.n2o_kw * 1e3 * n2o_on * (1.0 - 0.08 * Rise3(rpm / 8000.0)) * knock / std::sqrt(w * w + w_min * w_min);
  }
  if (info) {
    info->boost_psi = boost;
    info->drive_kw = drive_mep * e.vd_total * rpm / 120.0 * 1e-3;
    info->knock_margin = margin;
  }
  return SoftPos(t, 2.0);
}

}  // namespace

double DisplacementCc(const EngineSpec& spec) {
  const double b = Sane(spec.bore_mm, 40.0, 160.0, 101.6), s = Sane(spec.stroke_mm, 40.0, 160.0, 88.4);
  return 0.25 * kPi * b * b * s * SaneInt(spec.cylinders, 1, 16, 8) * 1e-3;
}

DynoResult RunDyno(const EngineSpec& spec, double rpm_max) {
  DynoResult r;
  r.rpm_max = Sane(rpm_max, 1000.0, 20000.0, 10000.0);
  const Engine e = Prepare(spec);
  r.knock_margin = e.knock_margin;
  for (int i = 0; i < DynoResult::kPoints; ++i) {
    const double rpm = r.rpm_at(i);
    PointInfo info;
    const double t = Torque(e, rpm, e.forced() ? &info : nullptr);
    r.torque_nm[i] = t;
    if (t > r.peak_torque_nm) { r.peak_torque_nm = t; r.peak_torque_rpm = rpm; }
    const double kw = t * rpm * 2.0 * kPi / 60.0 * 1e-3;
    if (kw > r.peak_power_kw) { r.peak_power_kw = kw; r.peak_power_rpm = rpm; r.blower_loss_kw = info.drive_kw; }
    if (e.forced()) {
      r.peak_boost_psi = std::max(r.peak_boost_psi, info.boost_psi);
      r.knock_margin = std::min(r.knock_margin, info.knock_margin);
    }
  }
  r.valve_float_rpm = e.float_rpm;
  r.safe_rpm = e.safe_rpm;
  r.idle_rpm = e.idle_rpm;
  return r;
}

double TorqueAt(const DynoResult& r, double rpm) {
  if (!(rpm >= 0.0) || r.rpm_max <= 0.0) return 0.0;  // also catches NaN
  const double x = std::min(rpm, r.rpm_max) / r.rpm_max * (DynoResult::kPoints - 1);
  const int i = std::min(DynoResult::kPoints - 2, int(x));
  const double f = x - i;
  return r.torque_nm[i] * (1.0 - f) + r.torque_nm[i + 1] * f;
}

}  // namespace eb
