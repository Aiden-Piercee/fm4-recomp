#!/usr/bin/env python3
# Generates assets/engine_builder/fm4_engine_parts.tsv. Cleanroom: all figures are public manufacturer specs or
# ordinary aftermarket part specs typed in by hand here; brands are fictional.
import sys

IN = 25.4
out = []
def row(*f): out.append("\t".join(str(x) for x in f))
def fmt(v):
    if isinstance(v, float):
        s = f"{v:.2f}".rstrip("0").rstrip(".")
        return s
    return str(v)
def mods(d):
    res = []
    for k, v in d.items():
        if k.endswith("+"): res.append(f"{k[:-1]}+={fmt(v)}")
        elif k.endswith("*"): res.append(f"{k[:-1]}*={fmt(v)}")
        else: res.append(f"{k}={fmt(v)}")
    return res
def nl(lift_in, rocker):
    """valve lift (in) at the given rocker -> catalogue lift (mm, quoted at the 1.5:1 nominal rocker)"""
    return round(lift_in * IN * 1.5 / rocker, 2)

# ---------------------------------------------------------------------------------------------- families
# id, display name, stock rocker, size class (s/m/b), first year, family number (for part ids)
FAM = [
    ("FORD_Y",         "Ford Y-block V8 (272/292/312)",                 1.54, "s", 1955, 1),
    ("CHEVY_SB",       "Chevrolet small-block V8, Gen I (265-400)",     1.50, "s", 1955, 2),
    ("CHEVY_W",        "Chevrolet W-series V8 (348/409/427 Z-11)",      1.75, "m", 1958, 3),
    ("CHEVY_BB",       "Chevrolet big-block V8, Mark IV (396-454)",     1.70, "b", 1965, 4),
    ("CHEVY_LT",       "Chevrolet small-block V8, Gen II (LT1/LT4)",    1.60, "s", 1992, 5),
    ("FORD_FE",        "Ford FE V8 (352/390/406/427/428)",              1.76, "m", 1958, 6),
    ("FORD_WINDSOR",   "Ford Windsor small-block V8 (260/289/302/351W)", 1.60, "s", 1962, 7),
    ("FORD_CLEVELAND", "Ford 335-series V8 (351C/351 Boss)",            1.73, "m", 1970, 8),
    ("FORD_385",       "Ford 385-series V8 (429/460)",                  1.73, "b", 1968, 9),
    ("FORD_BOSS429",   "Ford Boss 429 semi-hemi V8",                    1.50, "b", 1969, 10),
    ("MOPAR_B_RB",     "Chrysler B/RB wedge V8 (383/413/426W/440)",     1.50, "m", 1958, 11),
    ("MOPAR_HEMI",     "Chrysler 426 Hemi V8",                          1.50, "b", 1964, 12),
    ("PONTIAC_V8",     "Pontiac V8 (326-455)",                          1.50, "m", 1955, 13),
    ("OLDS_V8",        "Oldsmobile Rocket V8 (350-455)",                1.60, "m", 1955, 14),
    ("AMC_V8",         "AMC V8, Gen III (304-401)",                     1.60, "m", 1966, 15),
]
FAMD = {f[0]: f for f in FAM}

# ---------------------------------------------------------------------------------------------- engines
# Each: EngineID, family, name, dict of real-world figures. Lengths in inches here, converted to mm below.
def eng(eid, fam, name, bore, stroke, rod, cr, rmass, rodrpm, ivd, evd, port, idur, edur, il, el, lsa, lifter,
        rocker, intake, cfm, carb, filt, header, pipe_in, duals, muff, ign, octane, pdia=0, plen=0, tune=0, note=""):
    d = dict(bore=round(bore*IN, 2), stroke=round(stroke*IN, 2), cyl=8, rod=round(rod*IN, 1), cr=cr, rmass=rmass,
             rodrpm=rodrpm, vpc=2, ivd=round(ivd*IN, 1), evd=round(evd*IN, 1), port=port, idur=idur, edur=edur,
             ilift=nl(il, rocker), elift=nl(el, rocker), lsa=lsa, lifter=lifter, rocker=rocker, intake=intake,
             cfm=cfm, carb=carb, filter=filt, header=header)
    if pdia: d["pdia"] = pdia
    if plen: d["plen"] = plen
    d.update(pipe=round(pipe_in*IN, 1), duals=duals, muffler=muff, ign=ign, octane=octane)
    if tune: d["tune"] = tune  # smog-era spark and mixture calibration (1.0 = performance tune)
    return (eid, fam, name, d, note)

E = [
 eng(743, "FORD_Y", "Ford 312 V8 (Thunderbird F-code, supercharged)", 3.80, 3.44, 6.29, 8.5, 34, 5800,
     1.92, 1.51, 0.85, 204, 204, .405, .405, 112, 1, 1.54, 0, 450, 1, 0.08, 0, 2.0, 1, 0.6, 0, 98,
     note="FM4 car is supercharged; boost is applied by FM4 on top"),
 eng(541, "CHEVY_SB", "Chevrolet 283 V8 (fuel injection, 290 hp)", 3.875, 3.00, 5.70, 11.0, 30, 6800,
     1.72, 1.50, 0.95, 222, 222, .394, .399, 110, 1, 1.5, 3, 550, 0, 0.05, 1, 2.0, 1, 0.5, 0, 100,
     note="Rochester mechanical injection: carb=0, ram-tube plenum modelled as long-runner"),
 eng(760, "CHEVY_W", "Chevrolet 409 V8 (425 hp, dual 4-barrel)", 4.3125, 3.50, 6.135, 11.0, 38, 6200,
     2.19, 1.72, 0.95, 232, 232, .440, .440, 112, 1, 1.75, 0, 900, 1, 0.05, 1, 2.25, 1, 0.5, 0, 100),
 eng(60, "FORD_FE", "Ford 427 V8 (Cobra, medium riser)", 4.233, 3.784, 6.49, 10.4, 42, 6800,
     2.09, 1.66, 1.0, 232, 232, .500, .500, 114, 1, 1.76, 0, 1100, 1, 0.03, 3, 2.25, 1, 0.3, 0, 100, pdia=48, plen=760),
 eng(609, "FORD_WINDSOR", "Ford 289 V8 (Cobra Daytona race)", 4.00, 2.87, 5.155, 10.8, 27, 7800,
     1.78, 1.45, 1.1, 248, 248, .500, .500, 108, 1, 1.6, 1, 850, 1, 0.0, 3, 2.25, 1, 0.15, 0, 102, pdia=41, plen=860),
 eng(388, "FORD_FE", "Ford 427 V8 (GT40 Mk II race)", 4.236, 3.781, 6.49, 10.5, 42, 7000,
     2.19, 1.73, 1.1, 248, 248, .540, .540, 110, 1, 1.76, 0, 1100, 1, 0.02, 3, 2.5, 1, 0.1, 0, 102, pdia=51, plen=860),
 eng(66, "CHEVY_BB", "Chevrolet 427 V8 (L71, 435 hp tri-power)", 4.251, 3.76, 6.135, 11.0, 42, 6500,
     2.19, 1.72, 1.05, 242, 254, .520, .520, 114, 1, 1.7, 0, 950, 1, 0.04, 1, 2.5, 1, 0.45, 0, 100),
 eng(35, "MOPAR_B_RB", "Chrysler 383 V8 (Barracuda Formula S)", 4.25, 3.375, 6.358, 10.0, 40, 5800,
     2.08, 1.74, 0.95, 208, 216, .450, .458, 112, 0, 1.5, 0, 650, 1, 0.05, 1, 2.25, 1, 0.5, 0, 100),
 eng(192, "FORD_FE", "Ford 428 Cobra Jet V8 (GT500KR)", 4.13, 3.98, 6.488, 10.7, 44, 5800,
     2.09, 1.66, 0.95, 212, 220, .490, .490, 115, 0, 1.73, 0, 735, 1, 0.04, 1, 2.25, 1, 0.5, 0, 100),
 eng(43, "CHEVY_BB", "Chevrolet 396 V8 (L78, 375 hp)", 4.094, 3.76, 6.135, 11.0, 41, 6500,
     2.19, 1.72, 1.05, 242, 254, .520, .520, 114, 1, 1.7, 0, 800, 1, 0.05, 1, 2.25, 1, 0.5, 0, 100),
 eng(44, "CHEVY_SB", "Chevrolet 302 V8 (Z/28)", 4.00, 3.00, 5.70, 11.0, 28, 7500,
     2.02, 1.60, 1.0, 254, 254, .459, .459, 114, 1, 1.5, 0, 780, 1, 0.05, 1, 2.25, 1, 0.5, 0, 100),
 eng(267, "MOPAR_B_RB", "Chrysler 440 Magnum V8 (Charger R/T)", 4.32, 3.75, 6.768, 10.0, 45, 5600,
     2.08, 1.74, 0.95, 212, 220, .450, .465, 112, 0, 1.5, 0, 650, 1, 0.05, 1, 2.25, 1, 0.5, 0, 100),
 eng(508, "MOPAR_HEMI", "Chrysler 426 Hemi V8 (1969, solid lifter)", 4.25, 3.75, 6.86, 10.25, 46, 6800,
     2.25, 1.94, 1.1, 236, 236, .480, .480, 112, 1, 1.5, 0, 1100, 1, 0.04, 1, 2.5, 1, 0.5, 0, 100),
 eng(769, "PONTIAC_V8", "Pontiac 400 Ram Air III V8 (GTO Judge)", 4.12, 3.75, 6.625, 10.8, 42, 5800,
     2.11, 1.77, 1.0, 212, 222, .470, .470, 113, 0, 1.5, 0, 750, 1, 0.03, 1, 2.25, 1, 0.5, 0, 100),
 eng(796, "OLDS_V8", "Oldsmobile 455 V8 (4-4-2 W-30)", 4.126, 4.25, 6.735, 10.5, 47, 5400,
     2.07, 1.63, 0.95, 218, 228, .474, .474, 114, 0, 1.6, 0, 750, 1, 0.04, 1, 2.25, 1, 0.5, 0, 100,
     note="FM4 displacement 7446 cc is the 455"),
 eng(797, "PONTIAC_V8", "Pontiac 400 Ram Air III V8 (Firebird Trans Am)", 4.12, 3.75, 6.625, 10.75, 42, 5800,
     2.11, 1.77, 1.0, 212, 222, .470, .470, 113, 0, 1.5, 0, 750, 1, 0.03, 1, 2.25, 1, 0.5, 0, 100),
 eng(53, "CHEVY_BB", "Chevrolet 454 V8 (LS6, Chevelle SS)", 4.251, 4.00, 6.135, 11.25, 46, 6200,
     2.19, 1.88, 1.05, 242, 250, .520, .520, 114, 1, 1.7, 0, 780, 1, 0.05, 1, 2.5, 1, 0.5, 0, 100),
 eng(69, "CHEVY_SB", "Chevrolet 350 LT-1 V8 (Corvette ZR1)", 4.00, 3.48, 5.70, 11.0, 31, 7000,
     2.02, 1.60, 1.0, 242, 254, .459, .485, 115, 1, 1.5, 0, 780, 1, 0.05, 1, 2.25, 1, 0.5, 0, 100),
 eng(158, "FORD_BOSS429", "Ford Boss 429 V8", 4.36, 3.59, 6.605, 10.5, 44, 6800,
     2.28, 1.90, 1.05, 228, 228, .500, .500, 114, 1, 1.5, 0, 735, 1, 0.04, 1, 2.25, 1, 0.5, 0, 100),
 eng(418, "MOPAR_HEMI", "Chrysler 426 Hemi V8 (Challenger, hydraulic)", 4.25, 3.75, 6.86, 10.25, 46, 6800,
     2.25, 1.94, 1.1, 222, 234, .490, .481, 113, 0, 1.5, 0, 1100, 1, 0.04, 1, 2.5, 1, 0.5, 0, 100),
 eng(740, "FORD_FE", "Ford 428 Cobra Jet V8 (Cougar Eliminator)", 4.13, 3.98, 6.488, 10.6, 44, 5800,
     2.09, 1.66, 0.95, 212, 220, .490, .490, 115, 0, 1.73, 0, 735, 1, 0.04, 1, 2.25, 1, 0.5, 0, 100,
     note="FM4 6997 cc; the Eliminator's big-block option was the 428 CJ"),
 eng(751, "CHEVY_BB", "Chevrolet 454 V8 (LS6, El Camino SS)", 4.251, 4.00, 6.135, 11.25, 46, 6200,
     2.19, 1.88, 1.05, 242, 250, .520, .520, 114, 1, 1.7, 0, 780, 1, 0.05, 1, 2.5, 1, 0.5, 0, 100),
 eng(790, "CHEVY_SB", "Chevrolet 350 LT-1 V8 (Camaro Z/28)", 4.00, 3.48, 5.70, 11.0, 31, 7000,
     2.02, 1.60, 1.0, 242, 254, .459, .485, 115, 1, 1.5, 0, 780, 1, 0.05, 1, 2.25, 1, 0.5, 0, 100),
 eng(727, "AMC_V8", "AMC 401 V8 (Javelin AMX)", 4.165, 3.68, 5.857, 9.5, 40, 5600,
     2.025, 1.68, 0.95, 206, 214, .477, .477, 112, 0, 1.6, 0, 600, 1, 0.05, 0, 2.25, 1, 0.55, 0, 98),
 eng(737, "MOPAR_HEMI", "Chrysler 426 Hemi V8 ('Cuda, hydraulic)", 4.25, 3.75, 6.86, 10.2, 46, 6800,
     2.25, 1.94, 1.1, 222, 234, .490, .481, 113, 0, 1.5, 0, 1100, 1, 0.04, 1, 2.5, 1, 0.5, 0, 100),
 eng(780, "FORD_385", "Ford 429 Super Cobra Jet V8 (Mustang Mach 1)", 4.36, 3.59, 6.605, 11.3, 44, 6200,
     2.242, 1.722, 1.0, 228, 228, .509, .509, 115, 1, 1.73, 0, 780, 1, 0.04, 1, 2.25, 1, 0.5, 0, 100),
 eng(453, "PONTIAC_V8", "Pontiac 455 Super Duty V8 (Trans Am)", 4.15, 4.21, 6.625, 8.4, 47, 5600,
     2.11, 1.77, 1.1, 214, 226, .407, .413, 114, 0, 1.5, 0, 800, 1, 0.04, 1, 2.25, 1, 0.55, 0, 95),
 eng(738, "FORD_CLEVELAND", "Ford 351 Cleveland 4V V8 (XB Falcon GT)", 4.00, 3.50, 5.78, 9.1, 32, 6200,
     2.19, 1.71, 1.05, 202, 214, .427, .444, 116, 0, 1.73, 0, 600, 1, 0.06, 1, 2.25, 1, 0.55, 0, 97),
 eng(736, "PONTIAC_V8", "Pontiac 400 V8 (T/A 6.6, W72)", 4.12, 3.75, 6.625, 8.2, 42, 5400,
     2.11, 1.77, 0.95, 200, 210, .414, .413, 114, 0, 1.5, 0, 800, 1, 0.08, 0, 2.5, 0, 0.65, 1, 91, tune=0.85,
     note="FM4 6600 cc; the 1977 T/A 6.6 was the Pontiac 400 W72 (the Olds 403 6.6 is the alternative)"),
 eng(513, "FORD_WINDSOR", "Ford 302 V8 (2-barrel, King Cobra)", 4.00, 3.00, 5.09, 8.4, 27, 5500,
     1.78, 1.45, 0.85, 188, 196, .382, .397, 114, 0, 1.6, 0, 350, 1, 0.08, 0, 2.25, 0, 0.7, 1, 91, tune=0.85),
 eng(511, "CHEVY_SB", "Chevrolet 350 V8 (LM1, Camaro Z28)", 4.00, 3.48, 5.70, 8.2, 31, 5500,
     1.94, 1.50, 0.9, 194, 202, .390, .410, 114, 0, 1.5, 0, 750, 1, 0.07, 0, 2.25, 0, 0.65, 1, 91, tune=0.85),
 eng(489, "CHEVY_SB", "Chevrolet 350 TPI V8 (L98, Trans Am GTA)", 4.00, 3.48, 5.70, 9.3, 31, 5800,
     1.94, 1.50, 0.95, 202, 206, .403, .415, 115, 2, 1.5, 3, 650, 0, 0.05, 1, 2.25, 0, 0.55, 1, 95,
     note="EFI: cfm is the throttle body + runner airflow rating"),
 eng(477, "CHEVY_SB", "Chevrolet 350 TPI V8 (L98, Camaro IROC-Z)", 4.00, 3.48, 5.70, 9.3, 31, 5800,
     1.94, 1.50, 0.95, 202, 206, .403, .415, 115, 2, 1.5, 3, 650, 0, 0.05, 1, 2.25, 0, 0.55, 1, 95),
 eng(481, "FORD_WINDSOR", "Ford 5.0 HO V8 (SVT Cobra R, GT40 heads)", 4.00, 3.00, 5.09, 9.0, 26, 6000,
     1.84, 1.54, 1.0, 204, 210, .479, .479, 115, 2, 1.7, 3, 700, 0, 0.04, 2, 2.25, 1, 0.45, 1, 95, pdia=41, plen=380),
 eng(64, "CHEVY_LT", "Chevrolet 350 LT4 V8 (Corvette Grand Sport)", 4.00, 3.48, 5.70, 10.8, 30, 6500,
     2.00, 1.55, 1.15, 218, 228, .525, .525, 114, 2, 1.6, 3, 900, 0, 0.03, 2, 2.5, 1, 0.4, 1, 98, pdia=41, plen=400),
]

# ---------------------------------------------------------------------------------------------- parts
SLOTCODE = dict(heads=11, cam=12, valvetrain=13, pistons=14, rotating=15, intake=16, carb=17, headers=18,
                exhaust=19, ignition=20)
seq = {}
P = []
def part(slot, fams, brand, name, year, price, mass, m, famno=0):
    key = (slot, famno)
    seq[key] = seq.get(key, 0) + 1
    pid = SLOTCODE[slot] * 10000 + famno * 100 + seq[key]
    P.append((pid, slot, fams, brand, name, year, price, mass, m))

# Fictional brands
B_CAM1, B_CAM2 = "Ridgecrest Cams", "Marlow Racing Cams"
B_ROLL = "Ironwood Performance"
B_HEADS, B_ALU = "Blackstone Cylinder Heads", "Ironwood Performance"
B_INT = "Thunder Valley Manifolds"
B_CARB = "Kessler Induction"
B_HDR = "Arrowhead Headers"
B_EXH = "Coastline Exhaust"
B_IGN = "Pulsar Ignition"
B_VT = "Brandt Valvetrain"
B_PIST = "Sterling Piston Co."
B_ROT = "Velocity Forge"
B_EFI = "Delmont Fuel Systems"
F = "Factory"

def vmm(x): return round(x * IN, 1)

# ---- heads (hand-authored per family): (brand, name, year, price, mass, ivd_in, evd_in, port, cr+)
HEADS = {
 "FORD_Y": [
  (F, "Factory 312 heads (stock replacement)", 1956, 600, 0, 1.92, 1.51, 0.85, 0),
  (F, "Factory 1957 competition heads", 1957, 1100, 0, 2.02, 1.54, 0.95, 0.3),
  (B_HEADS, "Ported iron Y-block heads", 1960, 1700, 0, 2.02, 1.60, 1.05, 0.3),
  (B_ALU, "Aluminum race Y-block heads", 1968, 4200, -16, 2.05, 1.60, 1.15, 0.5)],
 "CHEVY_SB": [
  (F, "Factory 1.94 'fuelie' heads", 1961, 900, 0, 1.94, 1.50, 0.98, 0.3),
  (F, "Factory 2.02 double-hump heads", 1964, 1200, 0, 2.02, 1.60, 1.0, 0.3),
  (B_HEADS, "Competition ported iron 2.05/1.60", 1962, 2600, 0, 2.05, 1.60, 1.15, 0.3),
  (B_HEADS, "Street ported iron 2.02/1.60", 1966, 1800, 0, 2.02, 1.60, 1.08, 0),
  (B_ALU, "Aluminum street heads 2.02/1.60", 1978, 3200, -20, 2.02, 1.60, 1.12, 0.3),
  (F, "Factory aluminum L98 heads 1.94/1.50", 1986, 1600, -20, 1.94, 1.50, 1.02, 0),
  (B_ALU, "Aluminum 18-degree race heads 2.08/1.60", 1988, 5800, -20, 2.08, 1.60, 1.28, 0.5)],
 "CHEVY_W": [
  (F, "Factory 409 heads (2-barrel)", 1961, 700, 0, 2.07, 1.72, 0.88, -0.5),
  (F, "Factory Z-11 427 heads", 1963, 2200, 0, 2.20, 1.72, 1.08, 0.5),
  (B_HEADS, "Ported race 409 heads 2.25/1.75", 1964, 2800, 0, 2.25, 1.75, 1.15, 0.3),
  (B_ALU, "Aluminum race W heads 2.25/1.75", 1967, 5200, -28, 2.25, 1.75, 1.20, 0.5)],
 "CHEVY_BB": [
  (F, "Factory oval-port heads 2.06/1.72", 1965, 700, 0, 2.06, 1.72, 0.92, -0.3),
  (F, "Factory rectangle-port iron heads 2.19/1.72", 1965, 1500, 0, 2.19, 1.72, 1.05, 0),
  (F, "Factory L88 aluminum heads 2.19/1.84", 1967, 3800, -30, 2.19, 1.84, 1.10, 0),
  (F, "Factory open-chamber aluminum heads 2.19/1.88", 1969, 4800, -30, 2.19, 1.88, 1.15, -0.3),
  (B_HEADS, "Ported rectangle-port iron 2.25/1.88", 1970, 2600, 0, 2.25, 1.88, 1.15, 0),
  (B_ALU, "Aluminum race heads 2.30/1.88", 1978, 6000, -30, 2.30, 1.88, 1.28, 0.3),
  (B_ALU, "Aluminum oval-port street heads 2.19/1.88", 1985, 3600, -30, 2.19, 1.88, 1.08, 0.3)],
 "CHEVY_LT": [
  (F, "Factory LT1 aluminum heads 1.94/1.50", 1992, 1200, 0, 1.94, 1.50, 1.05, -0.4),
  (F, "Factory LT4 heads 2.00/1.55", 1996, 2000, 0, 2.00, 1.55, 1.15, 0),
  (B_HEADS, "CNC-ported LT4 heads 2.02/1.60", 1996, 3600, 0, 2.02, 1.60, 1.25, 0.3),
  (B_ALU, "Aluminum race LT heads 2.08/1.60", 1995, 5600, 0, 2.08, 1.60, 1.30, 0.5)],
 "FORD_FE": [
  (F, "Factory 390 GT heads 2.03/1.55", 1966, 800, 0, 2.03, 1.55, 0.95, 0),
  (F, "Factory 427 low-riser heads 2.09/1.66", 1963, 1400, 0, 2.09, 1.66, 1.0, 0),
  (F, "Factory 427 medium-riser heads 2.09/1.66", 1964, 1800, 0, 2.09, 1.66, 1.05, 0),
  (F, "Factory 427 high-riser heads 2.19/1.73", 1964, 3200, 0, 2.19, 1.73, 1.15, 0.3),
  (F, "Factory 427 tunnel-port heads 2.25/1.73", 1968, 4400, 0, 2.25, 1.73, 1.22, 0.3),
  (B_HEADS, "Ported Cobra Jet iron 2.09/1.66", 1969, 1900, 0, 2.09, 1.66, 1.08, 0),
  (B_ALU, "Aluminum street FE heads 2.09/1.66", 1985, 3400, -28, 2.09, 1.66, 1.12, 0.3)],
 "FORD_WINDSOR": [
  (F, "Factory 289 HiPo heads 1.78/1.45", 1963, 700, 0, 1.78, 1.45, 0.98, 0.3),
  (F, "Factory 351W heads 1.84/1.54", 1969, 700, 0, 1.84, 1.54, 1.0, -0.3),
  (F, "Factory GT40 iron heads 1.84/1.54", 1993, 1100, 0, 1.84, 1.54, 1.05, 0),
  (B_HEADS, "Ported iron heads 1.94/1.60", 1968, 1800, 0, 1.94, 1.60, 1.10, 0),
  (B_ALU, "Le Mans-style aluminum race heads 1.94/1.60", 1966, 4800, -16, 1.94, 1.60, 1.22, 0.5),
  (B_ALU, "Aluminum street heads 1.90/1.60", 1980, 3000, -16, 1.90, 1.60, 1.15, 0.3)],
 "FORD_CLEVELAND": [
  (F, "Factory 351C 2V open-chamber heads", 1970, 600, 0, 2.04, 1.65, 0.95, -0.6),
  (F, "Factory 351C 4V closed-chamber heads", 1970, 1000, 0, 2.19, 1.71, 1.05, 0),
  (F, "Factory Boss 351 4V heads", 1971, 1500, 0, 2.19, 1.71, 1.08, 0.3),
  (F, "Factory Australian 2V closed-chamber heads", 1972, 900, 0, 2.04, 1.65, 1.0, 0.4),
  (B_HEADS, "Ported 4V iron heads 2.19/1.71", 1973, 2200, 0, 2.19, 1.71, 1.15, 0),
  (B_ALU, "Aluminum race heads 2.20/1.71", 1980, 5600, -26, 2.20, 1.71, 1.28, 0.3)],
 "FORD_385": [
  (F, "Factory 429 heads (closed chamber) 2.09/1.66", 1968, 700, 0, 2.09, 1.66, 0.95, 0.3),
  (F, "Factory 429 Cobra Jet heads 2.24/1.72", 1970, 1400, 0, 2.242, 1.722, 1.05, 0),
  (B_HEADS, "Ported Cobra Jet iron 2.25/1.76", 1972, 2400, 0, 2.25, 1.76, 1.13, 0),
  (B_ALU, "Aluminum race heads 2.30/1.80", 1978, 5500, -30, 2.30, 1.80, 1.25, 0.3),
  (B_ALU, "Aluminum street heads 2.19/1.76", 1988, 3600, -30, 2.19, 1.76, 1.12, 0)],
 "FORD_BOSS429": [
  (F, "Factory Boss 429 heads (stock replacement)", 1969, 2400, 0, 2.28, 1.90, 1.05, 0),
  (F, "Factory Boss 429 race-prep heads", 1969, 3800, 0, 2.28, 1.90, 1.15, 0.3),
  (B_HEADS, "Ported Boss 429 aluminum 2.30/1.94", 1971, 5200, 0, 2.30, 1.94, 1.22, 0.3),
  (B_ALU, "Semi-hemi race heads 2.35/1.94", 1980, 7200, -4, 2.35, 1.94, 1.30, 0.5)],
 "MOPAR_B_RB": [
  (F, "Factory 906 heads 2.08/1.74", 1967, 800, 0, 2.08, 1.74, 1.0, 0),
  (F, "Factory Max Wedge heads 2.08/1.88", 1962, 2600, 0, 2.08, 1.88, 1.12, 0.3),
  (F, "Factory 452 open-chamber heads", 1972, 600, 0, 2.08, 1.74, 0.95, -0.5),
  (B_HEADS, "Ported 906 iron 2.14/1.81", 1970, 2200, 0, 2.14, 1.81, 1.12, 0),
  (B_ALU, "Aluminum race wedge heads 2.14/1.81", 1978, 5400, -28, 2.14, 1.81, 1.25, 0.3),
  (B_ALU, "Aluminum street wedge heads 2.14/1.81", 1990, 3400, -28, 2.14, 1.81, 1.15, 0.3)],
 "MOPAR_HEMI": [
  (F, "Factory 426 street Hemi heads", 1966, 2400, 0, 2.25, 1.94, 1.10, 0),
  (F, "Factory aluminum race Hemi heads", 1965, 4800, -30, 2.25, 1.94, 1.15, 0.3),
  (B_HEADS, "Ported iron Hemi heads 2.30/1.94", 1970, 3400, 0, 2.30, 1.94, 1.22, 0),
  (B_ALU, "Aluminum race Hemi heads 2.35/1.94", 1978, 7000, -30, 2.35, 1.94, 1.32, 0.3)],
 "PONTIAC_V8": [
  (F, "Factory #670 D-port heads 2.11/1.77", 1967, 800, 0, 2.11, 1.77, 1.0, 0),
  (F, "Factory Ram Air IV round-port heads", 1969, 1800, 0, 2.11, 1.77, 1.10, 0),
  (F, "Factory 455 HO round-port heads", 1971, 1400, 0, 2.11, 1.77, 1.08, -0.5),
  (F, "Factory Super Duty heads", 1973, 2200, 0, 2.11, 1.77, 1.12, 0),
  (B_HEADS, "Ported D-port iron 2.11/1.77", 1970, 1800, 0, 2.11, 1.77, 1.08, 0),
  (B_ALU, "Aluminum race heads 2.19/1.77", 1980, 5200, -28, 2.19, 1.77, 1.25, 0.3),
  (B_ALU, "Aluminum street D-port heads 2.11/1.77", 1990, 3400, -28, 2.11, 1.77, 1.15, 0.3)],
 "OLDS_V8": [
  (F, "Factory 455 'C' heads 2.07/1.63", 1968, 700, 0, 2.07, 1.63, 0.95, 0),
  (F, "Factory W-30 heads 2.07/1.63", 1970, 1100, 0, 2.07, 1.63, 1.02, 0),
  (F, "Factory 1972 open-chamber heads", 1972, 600, 0, 2.07, 1.63, 0.95, -0.6),
  (B_HEADS, "Ported W-30 iron 2.07/1.67", 1972, 2000, 0, 2.07, 1.67, 1.10, 0),
  (B_ALU, "Aluminum Rocket race heads 2.14/1.67", 1976, 5200, -28, 2.14, 1.67, 1.25, 0.3),
  (B_ALU, "Aluminum street heads 2.07/1.63", 1990, 3200, -28, 2.07, 1.63, 1.12, 0.3)],
 "AMC_V8": [
  (F, "Factory 390 heads (small chamber)", 1968, 700, 0, 2.02, 1.68, 0.92, 0.4),
  (F, "Factory 401 heads 2.025/1.68", 1970, 800, 0, 2.025, 1.68, 0.95, 0),
  (B_HEADS, "Ported iron heads 2.08/1.68", 1972, 1800, 0, 2.08, 1.68, 1.08, 0),
  (B_ALU, "Aluminum race heads 2.08/1.68", 1980, 4800, -26, 2.08, 1.68, 1.20, 0.3),
  (B_ALU, "Aluminum street heads 2.08/1.68", 1992, 3200, -26, 2.08, 1.68, 1.12, 0.3)],
}

# ---- cams: generic tiers per size class, lift quoted at the family's stock rocker
CAM_TIERS = [  # brand, label, year, price, idur, edur, il, el, lsa, lifter
 (B_CAM1, "RV Torque Hydraulic", 1955, 380, 194, 204, .420, .443, 112, 0),
 (B_CAM1, "Street Hydraulic", 1958, 420, 214, 224, .465, .480, 112, 0),
 (B_CAM2, "Street/Strip Solid", 1958, 520, 236, 242, .520, .530, 110, 1),
 (B_CAM2, "Competition Solid", 1960, 650, 256, 262, .560, .570, 106, 1),
 (B_ROLL, "Hydraulic Roller Street", 1985, 1150, 218, 224, .510, .520, 112, 2),
 (B_ROLL, "Solid Roller Race", 1966, 1450, 264, 272, .620, .630, 106, 3),
]
LIFTER_NAME = {0: "hydraulic", 1: "solid", 2: "hydraulic roller", 3: "solid roller"}
FACTORY_CAMS = {  # name, year, price, idur, edur, il, el (at stock rocker unless r given), lsa, lifter, rocker
 "FORD_Y": [("Factory 1957 competition solid", 1957, 400, 218, 218, .430, .430, 110, 1, None)],
 "CHEVY_SB": [("Factory Duntov solid", 1957, 350, 222, 222, .394, .399, 110, 1, None),
              ("Factory 30-30 solid (Z/28 302)", 1967, 450, 254, 254, .459, .459, 114, 1, None),
              ("Factory LT-1 solid", 1970, 450, 242, 254, .459, .485, 115, 1, None)],
 "CHEVY_W": [("Factory Z-11 solid", 1963, 500, 242, 242, .510, .510, 112, 1, None)],
 "CHEVY_BB": [("Factory L88 solid", 1967, 550, 256, 264, .560, .580, 114, 1, None)],
 "CHEVY_LT": [("Factory LT1 hydraulic roller", 1992, 700, 202, 207, .447, .459, 117, 2, 1.5),
              ("Factory LT4 hydraulic roller", 1996, 800, 218, 228, .525, .525, 114, 2, None)],
 "FORD_FE": [("Factory 428 Cobra Jet hydraulic", 1968, 400, 212, 220, .490, .490, 115, 0, 1.73),
             ("Factory 427 high-riser solid", 1964, 550, 256, 256, .540, .540, 110, 1, None)],
 "FORD_WINDSOR": [("Factory 289 HiPo solid", 1963, 400, 226, 226, .457, .457, 115, 1, None),
                  ("Factory 5.0 HO hydraulic roller", 1985, 700, 196, 210, .444, .444, 116, 2, None)],
 "FORD_CLEVELAND": [("Factory Boss 351 solid", 1971, 450, 228, 228, .491, .491, 116, 1, None)],
 "FORD_385": [("Factory 429 Cobra Jet hydraulic", 1970, 400, 212, 222, .472, .472, 115, 0, None)],
 "FORD_BOSS429": [("Factory Boss 429 hydraulic", 1969, 450, 214, 214, .490, .490, 114, 0, None)],
 "MOPAR_B_RB": [("Factory Max Wedge solid", 1962, 500, 240, 240, .520, .520, 108, 1, None),
                ("Factory 440 Six Pack hydraulic", 1969, 400, 214, 222, .471, .481, 112, 0, None)],
 "MOPAR_HEMI": [("Factory 426 street solid", 1966, 500, 236, 236, .480, .480, 112, 1, None),
                ("Factory 426 race solid", 1964, 600, 260, 260, .520, .520, 110, 1, None)],
 "PONTIAC_V8": [("Factory Ram Air IV hydraulic", 1969, 450, 228, 236, .470, .470, 113, 0, None)],
 "OLDS_V8": [("Factory W-30 hydraulic", 1970, 400, 218, 228, .474, .474, 114, 0, None)],
 "AMC_V8": [("Factory 'Machine' hydraulic", 1970, 400, 212, 220, .477, .477, 112, 0, None)],
}

# ---- rocker / spring kits per family: (brand, name, year, price, rocker, float)
ROCKERS = {
 "FORD_Y": [(B_VT, "1.6 shaft roller rockers + dual springs", 1975, 650, 1.6, 6600)],
 "CHEVY_SB": [(B_VT, "1.52 full roller rockers + stud girdle", 1972, 520, 1.52, 7600),
              (B_VT, "1.6 roller rockers + dual springs", 1975, 480, 1.6, 7000)],
 "CHEVY_W": [(B_VT, "1.75 roller rockers + race springs", 1975, 700, 1.75, 7200)],
 "CHEVY_BB": [(B_VT, "1.8 roller rockers + dual springs", 1975, 560, 1.8, 6800)],
 "CHEVY_LT": [(B_VT, "1.7 roller rockers + dual springs", 1994, 600, 1.7, 6900)],
 "FORD_FE": [(B_VT, "1.76 roller shaft rockers + race springs", 1970, 750, 1.76, 7300)],
 "FORD_WINDSOR": [(B_VT, "1.7 roller rockers + dual springs", 1980, 520, 1.7, 7000)],
 "FORD_CLEVELAND": [(B_VT, "1.8 roller rockers + dual springs", 1978, 560, 1.8, 7000)],
 "FORD_385": [(B_VT, "1.8 roller rockers + dual springs", 1978, 560, 1.8, 6800)],
 "FORD_BOSS429": [(B_VT, "1.6 roller rockers + race springs", 1975, 800, 1.6, 7300)],
 "MOPAR_B_RB": [(B_VT, "1.6 shaft roller rockers + dual springs", 1975, 650, 1.6, 6800)],
 "MOPAR_HEMI": [(B_VT, "1.6 shaft roller rockers + race springs", 1975, 900, 1.6, 7300)],
 "PONTIAC_V8": [(B_VT, "1.65 roller rockers + dual springs", 1976, 520, 1.65, 6600)],
 "OLDS_V8": [(B_VT, "1.7 roller rockers + dual springs", 1976, 540, 1.7, 6400)],
 "AMC_V8": [(B_VT, "1.7 roller rockers + dual springs", 1978, 540, 1.7, 6500)],
}

# ---- rotating (crank and rods): (brand, name, year, price, mass, mods)
def R(brand, name, year, price, mass, stroke_in=None, rod_in=None, rmass=0, rodrpm=None):
    m = {}
    if stroke_in: m["stroke"] = round(stroke_in * IN, 2)
    if rod_in: m["rod"] = round(rod_in * IN, 1)
    if rmass: m["rmass+"] = rmass
    if rodrpm: m["rodrpm"] = rodrpm
    return (brand, name, year, price, mass, m)
ROT = {
 "FORD_Y": [R(B_ROT, "Forged crank + rods (3.44 in)", 1957, 3000, 0, rodrpm=6500),
            R(B_ROT, "3.80 in stroker crank + rods", 1990, 5800, 2, 3.80, 6.0, 2, 6000),
            R(B_ROT, "Lightweight race rotating assembly", 1962, 9500, -4, rmass=-4, rodrpm=7200)],
 "CHEVY_SB": [R(F, "Factory forged 3.00 crank + 5.7 rods (302)", 1967, 3200, -1, 3.00, 5.7, -1, 8000),
              R(F, "Factory forged 3.48 crank + 5.7 rods", 1967, 2600, 0, 3.48, 5.7, 0, 7000),
              R(F, "Factory 400 crank, 3.75 (cast)", 1970, 1600, 2, 3.75, 5.7, 3, 6000),
              R(B_ROT, "3.75 forged stroker + 6.0 rods (383)", 1985, 6500, 2, 3.75, 6.0, 2, 7000),
              R(B_ROT, "Lightweight race rotating assembly (3.48)", 1975, 10500, -5, 3.48, 5.7, -5, 8500)],
 "CHEVY_W": [R(F, "Factory 427 Z-11 crank, 3.65", 1963, 4200, 1, 3.65, 6.135, 1, 6600),
             R(B_ROT, "Forged crank + rods (3.50)", 1962, 3400, 0, rodrpm=7000),
             R(B_ROT, "Lightweight race rotating assembly", 1966, 10000, -5, rmass=-5, rodrpm=7600)],
 "CHEVY_BB": [R(F, "Factory forged crank + L88 rods (stock stroke)", 1967, 3800, 0, rodrpm=7000),
              R(F, "Factory 454 forged crank, 4.00", 1970, 4200, 2, 4.00, 6.135, 2, 6500),
              R(B_ROT, "4.25 forged stroker + 6.535 rods (496)", 1985, 7800, 3, 4.25, 6.535, 3, 6600),
              R(B_ROT, "Lightweight race rotating assembly (4.00)", 1975, 11500, -6, 4.00, 6.135, -6, 7800)],
 "CHEVY_LT": [R(B_ROT, "Forged crank + rods (3.48)", 1993, 3600, 0, rodrpm=7000),
              R(B_ROT, "3.75 forged stroker + 5.7 rods (383)", 1995, 6800, 2, 3.75, 5.7, 2, 6800),
              R(B_ROT, "Lightweight race rotating assembly", 1994, 10500, -5, rmass=-5, rodrpm=8200)],
 "FORD_FE": [R(F, "Factory 427 side-oiler crank + cross-bolt rods", 1965, 4200, 0, 3.784, 6.49, 0, 7000),
             R(F, "Factory 428 crank, 3.98", 1966, 2400, 1, 3.98, 6.488, 1, 6000),
             R(B_ROT, "4.25 forged stroker + 6.7 rods", 1988, 7600, 3, 4.25, 6.7, 3, 6400),
             R(B_ROT, "Lightweight race rotating assembly", 1968, 11000, -6, rmass=-6, rodrpm=7600)],
 "FORD_WINDSOR": [R(F, "Factory HiPo crank + rods (289)", 1963, 2600, 0, 2.87, 5.155, 0, 7200),
                  R(F, "Factory 302 crank, 3.00", 1968, 1400, 0, 3.00, 5.09, 0, 6200),
                  R(B_ROT, "3.25 forged stroker + 5.4 rods (331)", 1985, 5600, 1, 3.25, 5.4, 1, 6800),
                  R(B_ROT, "3.40 forged stroker + 5.4 rods (347)", 1987, 5800, 1, 3.40, 5.4, 1, 6600),
                  R(B_ROT, "Lightweight race rotating assembly", 1966, 9800, -4, rmass=-4, rodrpm=8500)],
 "FORD_CLEVELAND": [R(F, "Factory Boss 351 crank + rods", 1971, 3200, 0, rodrpm=7000),
                    R(B_ROT, "3.85 forged stroker + 6.0 rods (393)", 1988, 6400, 2, 3.85, 6.0, 2, 6600),
                    R(B_ROT, "Lightweight race rotating assembly", 1974, 10000, -5, rmass=-5, rodrpm=8000)],
 "FORD_385": [R(F, "Factory 460 crank, 3.85", 1968, 1800, 2, 3.85, 6.605, 2, 5800),
              R(B_ROT, "Forged crank + rods (3.59)", 1970, 3800, 0, rodrpm=7000),
              R(B_ROT, "4.15 forged stroker + 6.8 rods (514)", 1988, 7900, 3, 4.15, 6.8, 3, 6300)],
 "FORD_BOSS429": [R(F, "Factory 460 crank, 3.85", 1969, 2200, 2, 3.85, 6.605, 2, 6200),
                  R(B_ROT, "4.15 forged stroker + 6.8 rods", 1988, 8400, 3, 4.15, 6.8, 3, 6400),
                  R(B_ROT, "Lightweight race rotating assembly", 1970, 11500, -6, rmass=-6, rodrpm=7800)],
 "MOPAR_B_RB": [R(B_ROT, "Forged crank + rods (stock stroke)", 1962, 3400, 0, rodrpm=6800),
                R(B_ROT, "3.75 stroker crank (383 to 426)", 1972, 4600, 2, 3.75, 6.768, 2, 6000),
                R(B_ROT, "4.15 forged stroker + 7.1 rods", 1988, 7800, 3, 4.15, 7.1, 3, 6200),
                R(B_ROT, "Lightweight race rotating assembly", 1966, 10800, -6, rmass=-6, rodrpm=7600)],
 "MOPAR_HEMI": [R(F, "Factory race crank + rods", 1964, 4500, 0, rodrpm=7500),
                R(B_ROT, "4.15 forged stroker + 7.1 rods (472)", 1988, 8600, 3, 4.15, 7.1, 3, 6800),
                R(B_ROT, "Lightweight race rotating assembly", 1970, 12000, -6, rmass=-6, rodrpm=8200)],
 "PONTIAC_V8": [R(F, "Factory forged Ram Air IV crank + rods", 1969, 3000, 0, rodrpm=6600),
                R(F, "Factory 455 crank, 4.21", 1970, 1800, 2, 4.21, 6.625, 2, 5600),
                R(B_ROT, "4.25 forged stroker + 6.8 rods", 1990, 7400, 3, 4.25, 6.8, 3, 6200),
                R(B_ROT, "Lightweight race rotating assembly", 1972, 10500, -6, rmass=-6, rodrpm=7200)],
 "OLDS_V8": [R(F, "Factory forged W-30 crank + rods", 1970, 3000, 0, rodrpm=6200),
             R(B_ROT, "4.50 forged stroker + 6.8 rods", 1992, 7800, 3, 4.50, 6.8, 3, 5800),
             R(B_ROT, "Lightweight race rotating assembly", 1975, 10500, -6, rmass=-6, rodrpm=7000)],
 "AMC_V8": [R(B_ROT, "Forged crank + rods (3.68)", 1972, 3000, 0, rodrpm=6400),
            R(B_ROT, "4.00 forged stroker + 6.125 rods", 1995, 7200, 2, 4.00, 6.125, 2, 6000),
            R(B_ROT, "Lightweight race rotating assembly", 1976, 9800, -5, rmass=-5, rodrpm=7200)],
}

# ---- intakes: generic tiers per family + family specials (brand, name, year, price, mass, mods)
INTAKE_SPECIAL = {
 "FORD_Y": [(F, "Factory dual 4-barrel manifold", 1957, 500, 0, {"intake": 0, "port*": 1.02})],
 "CHEVY_SB": [(F, "Factory aluminum cross-ram dual 4-barrel", 1968, 900, -8, {"intake": 1, "runner": 300, "port*": 1.05}),
              (F, "Factory tuned-port injection (TPI)", 1985, 1400, 0, {"intake": 3, "carb": 0, "cfm": 650, "filter": 0.05}),
              (B_EFI, "Large-runner TPI kit", 1988, 1900, -2, {"intake": 3, "carb": 0, "cfm": 780, "port*": 1.04, "filter": 0.04}),
              (B_EFI, "Short-runner EFI mini-ram", 1990, 2200, -4, {"intake": 1, "carb": 0, "cfm": 900, "port*": 1.06, "filter": 0.03})],
 "CHEVY_W": [(F, "Factory aluminum dual 4-barrel (409/425)", 1962, 600, -9, {"intake": 0, "port*": 1.02})],
 "CHEVY_BB": [(F, "Factory L88 aluminum high-rise", 1967, 700, -10, {"intake": 0, "port*": 1.04})],
 "CHEVY_LT": [(F, "Factory LT1/LT4 EFI intake", 1992, 900, 0, {"intake": 3, "carb": 0, "cfm": 900}),
              (B_EFI, "Ported LT4 EFI intake", 1996, 1400, 0, {"intake": 3, "carb": 0, "cfm": 1000, "port*": 1.03}),
              (B_EFI, "Short-runner EFI race intake", 1995, 2400, -2, {"intake": 1, "carb": 0, "cfm": 1150, "port*": 1.06})],
 "FORD_FE": [(F, "Factory medium-riser dual 4-barrel", 1964, 700, -9, {"intake": 0, "port*": 1.03}),
             (F, "Factory high-riser single 4-barrel", 1964, 800, -9, {"intake": 1, "port*": 1.05})],
 "FORD_WINDSOR": [(F, "Factory 5.0 HO EFI intake", 1986, 900, 0, {"intake": 3, "carb": 0, "cfm": 650, "filter": 0.05}),
                  (F, "Factory GT40 tubular EFI intake", 1993, 1300, -2, {"intake": 3, "carb": 0, "cfm": 760, "port*": 1.03, "filter": 0.04}),
                  (B_EFI, "Large-plenum EFI intake", 1989, 1600, -3, {"intake": 3, "carb": 0, "cfm": 850, "port*": 1.04, "filter": 0.04})],
 "FORD_CLEVELAND": [(F, "Factory Boss 351 aluminum high-rise", 1971, 600, -9, {"intake": 0, "port*": 1.03})],
 "FORD_385": [(F, "Factory Super Cobra Jet aluminum", 1971, 600, -10, {"intake": 0, "port*": 1.02})],
 "FORD_BOSS429": [(F, "Factory Boss 429 aluminum (stock replacement)", 1969, 600, 0, {"intake": 0})],
 "MOPAR_B_RB": [(F, "Factory Six Pack aluminum (3x2-barrel)", 1969, 700, -9, {"intake": 0, "port*": 1.02}),
                (F, "Factory Max Wedge cross-ram", 1962, 900, 0, {"intake": 1, "runner": 380, "port*": 1.05})],
 "MOPAR_HEMI": [(F, "Factory race cross-ram dual 4-barrel", 1964, 1200, -6, {"intake": 1, "runner": 380, "port*": 1.05})],
 "PONTIAC_V8": [(F, "Factory Ram Air IV aluminum", 1969, 600, -10, {"intake": 0, "port*": 1.03})],
 "OLDS_V8": [(F, "Factory W-30 aluminum", 1970, 600, -10, {"intake": 0, "port*": 1.02})],
 "AMC_V8": [(F, "Factory 'Machine' aluminum high-rise", 1970, 550, -9, {"intake": 0, "port*": 1.02})],
}

# ---- headers by size class: (pdia small/mid/big in inches)
PDIA = {"s": (1.50, 1.625, 1.75), "m": (1.75, 1.875, 2.00), "b": (1.875, 2.00, 2.125)}
def frac(x):
    return {1.5: "1-1/2", 1.625: "1-5/8", 1.75: "1-3/4", 1.875: "1-7/8", 2.0: "2", 2.125: "2-1/8"}[x]

LOG_FAMILIES = {e[1] for e in E if e[3]['header'] == 0}

def gen_family_parts(fid, fname, rock, size, fyear, fno):
    yr = lambda y: max(y, fyear)
    # heads
    for (b, n, y, pr, m, iv, ev, po, crp) in HEADS[fid]:
        d = {"ivd": vmm(iv), "evd": vmm(ev), "port": po}
        if crp: d["cr+"] = crp
        part("heads", fid, b, n, yr(y), pr, m, d, fno)
    # cams
    big = size in ("m", "b")
    for (b, lab, y, pr, idur, edur, il, el, lsa, lft) in CAM_TIERS:
        if big: idur, edur, il, el = idur + 4, edur + 4, il + .030, el + .030
        if fid == "CHEVY_LT" and lft in (0, 1):  # Gen II has roller blocks; flat-tappet cams make no sense
            continue
        name = f"{lab} {idur}/{edur} (.{round(il*1000):03d}/.{round(el*1000):03d})"
        part("cam", fid, b, name, yr(y), pr, 0,
             {"idur": idur, "edur": edur, "ilift": nl(il, rock), "elift": nl(el, rock), "lsa": lsa, "lifter": lft}, fno)
    for (n, y, pr, idur, edur, il, el, lsa, lft, r) in FACTORY_CAMS.get(fid, []):
        r = r or rock
        part("cam", fid, F, n, yr(y), pr, 0,
             {"idur": idur, "edur": edur, "ilift": nl(il, r), "elift": nl(el, r), "lsa": lsa, "lifter": lft}, fno)
    # valvetrain
    for (b, n, y, pr, rk, fl) in ROCKERS[fid]:
        part("valvetrain", fid, b, n, yr(y), pr, 0, {"rocker": rk, "float": fl}, fno)
    # rotating
    for (b, n, y, pr, m, d) in ROT[fid]:
        part("rotating", fid, b, n, yr(y), pr, m, d, fno)
    # intake
    carb_family = fid != "CHEVY_LT"
    if carb_family:
        part("intake", fid, B_INT, "Aluminum high-rise dual plane", yr(1958), 290, -9, {"intake": 0, "port*": 1.02}, fno)
        part("intake", fid, B_INT, "Aluminum single plane, 4-barrel", yr(1964), 360, -9, {"intake": 1, "port*": 1.04}, fno)
        part("intake", fid, B_INT, "Tunnel ram, 2x4-barrel", yr(1966), 650, -4, {"intake": 2, "port*": 1.06}, fno)
    for (b, n, y, pr, m, d) in INTAKE_SPECIAL.get(fid, []):
        part("intake", fid, b, n, yr(y), pr, m, d, fno)
    # headers
    p1, p2, p3 = PDIA[size]
    if fid in LOG_FAMILIES:
        part("headers", fid, F, "Factory high-performance cast manifolds", fyear, 300, 0, {"header": 1}, fno)
    part("headers", fid, B_HDR, f"Long-tube street headers, {frac(p2)} in", yr(1958), 620, -9,
         {"header": 3, "pdia": vmm(p2), "plen": 760}, fno)
    part("headers", fid, B_HDR, f"Long-tube race headers, {frac(p3)} in", yr(1962), 940, -11,
         {"header": 3, "pdia": vmm(p3), "plen": 860}, fno)
    part("headers", fid, B_HDR, f"Shorty headers, {frac(p1)} in", yr(1972), 420, -6,
         {"header": 2, "pdia": vmm(p1), "plen": 380}, fno)
    if size == "s":
        part("headers", fid, B_HDR, f"Tri-Y headers, {frac(p2)} in", yr(1962), 720, -8,
             {"header": 4, "pdia": vmm(p2), "plen": 800}, fno)
    if fid in ("CHEVY_SB", "FORD_WINDSOR", "CHEVY_LT"):
        part("headers", fid, B_HDR, f"Stainless long-tube headers, {frac(p2)} in (smog-legal)", yr(1988), 1100, -10,
             {"header": 3, "pdia": vmm(p2), "plen": 760}, fno)
    # EFI throttle bodies (carb slot) for families with EFI engines
    if fid == "CHEVY_SB":
        part("carb", fid, B_EFI, "Twin 52 mm throttle body + 85 mm MAF (TPI)", 1987, 650, 0, {"carb": 0, "cfm": 780}, fno)
    if fid == "FORD_WINDSOR":
        part("carb", fid, F, "Factory 60 mm throttle body (5.0 HO)", 1986, 300, 0, {"carb": 0, "cfm": 650}, fno)
        part("carb", fid, B_EFI, "70 mm throttle body + 77 mm MAF", 1990, 700, 0, {"carb": 0, "cfm": 850}, fno)
    if fid == "CHEVY_LT":
        part("carb", fid, B_EFI, "58 mm throttle body + 85 mm MAF", 1994, 700, 0, {"carb": 0, "cfm": 1050}, fno)

for f in FAM:
    gen_family_parts(*f)

# ---- generic parts (fit every family)
G = "*"
# valvetrain springs (float = valve float rpm the springs allow)
for (b, n, y, pr, fl) in [
    (F, "Stock replacement springs", 1955, 60, 5600),
    (B_VT, "Heavy-duty single springs + dampers", 1955, 120, 6200),
    (B_VT, "Dual springs, chromoly retainers", 1960, 220, 6800),
    (B_VT, "High-rpm dual springs, titanium retainers", 1970, 420, 7600),
    (B_VT, "Race dual springs (solid roller), titanium retainers", 1976, 560, 8400),
    (B_VT, "Beehive springs (hydraulic roller)", 1994, 380, 7000)]:
    part("valvetrain", G, b, n, y, pr, 0, {"float": fl})
# pistons (relative to the stock bore and compression)
for (b, n, y, pr, m, d) in [
    (F, "Cast replacement pistons, .030 over", 1955, 260, 0, {"bore+": 0.76, "cr+": 0.1}),
    (B_PIST, "Forged flat-top pistons, .030 over", 1958, 520, -0.5, {"bore+": 0.76, "cr+": 0.8}),
    (B_PIST, "Forged high-dome race pistons, .030 over", 1960, 720, 0.4, {"bore+": 0.76, "cr+": 2.0}),
    (B_PIST, "Forged flat-top pistons, .060 over", 1962, 560, -0.3, {"bore+": 1.52, "cr+": 1.0}),
    (B_PIST, "Forged dished pistons for boost (8.0:1)", 1970, 560, -0.3, {"bore+": 0.76, "cr": 8.0}),
    (B_PIST, "Lightweight forged race pistons, .030 over", 1980, 860, -1.2, {"bore+": 0.76, "cr+": 1.5}),
    (B_PIST, "Hypereutectic flat-top pistons, .030 over", 1985, 380, -0.2, {"bore+": 0.76, "cr+": 0.6})]:
    part("pistons", G, b, n, y, pr, m, d)
# carburettors (cfm at 1.5 inHg; aftermarket carbs come with an open-element cleaner)
for (b, n, y, pr, cfm, filt) in [
    (F, "Factory 2-barrel, 350 cfm", 1955, 120, 350, 0.08),
    (B_CARB, "4-barrel, 450 cfm", 1957, 220, 450, 0.03),
    (B_CARB, "4-barrel vacuum secondary, 600 cfm", 1962, 280, 600, 0.03),
    (B_CARB, "4-barrel double pumper, 650 cfm", 1965, 330, 650, 0.03),
    (B_CARB, "4-barrel vacuum secondary, 750 cfm", 1966, 340, 750, 0.03),
    (B_CARB, "4-barrel double pumper, 780 cfm", 1967, 380, 780, 0.03),
    (F, "Factory spread-bore 4-barrel, 800 cfm", 1967, 260, 800, 0.05),
    (B_CARB, "4-barrel double pumper, 850 cfm", 1969, 420, 850, 0.02),
    (B_CARB, "Triple 2-barrel, 1000 cfm", 1958, 520, 1000, 0.03),
    (B_CARB, "Race 4-barrel, big flange, 1050 cfm", 1970, 700, 1050, 0.01),
    (B_CARB, "Dual 4-barrel, 2x 600 cfm", 1962, 640, 1200, 0.02),
    (B_CARB, "Dual 4-barrel, 2x 750 cfm", 1966, 780, 1500, 0.01),
    (B_CARB, "Street 4-barrel with electric choke, 950 cfm", 1980, 520, 950, 0.03)]:
    part("carb", G, b, n, y, pr, 0, {"carb": 1, "cfm": cfm, "filter": filt})
# exhaust system (pipe mm each side, muffler 0 = open .. 1 = very restrictive)
for (b, n, y, pr, m, pipe_in, duals, muf) in [
    (F, "Factory single 2-1/4 in, reverse-flow muffler", 1955, 150, 0, 2.25, 0, 0.7),
    (F, "Factory dual 2-1/4 in, turbo mufflers", 1955, 260, 3, 2.25, 1, 0.45),
    (B_EXH, "Dual 2-1/2 in glasspacks", 1955, 320, -2, 2.5, 1, 0.3),
    (B_EXH, "Dual 2-1/2 in side pipes", 1965, 520, -4, 2.5, 1, 0.22),
    (B_EXH, "Dual 2-1/2 in chambered mufflers", 1966, 380, 0, 2.5, 1, 0.35),
    (B_EXH, "Open dual 3 in race pipes (no mufflers)", 1955, 240, -10, 3.0, 1, 0.0),
    (B_EXH, "Dual 3 in race chambered", 1970, 540, -2, 3.0, 1, 0.22),
    (F, "Factory dual 2-1/2 in with catalytic converters", 1975, 450, 4, 2.5, 1, 0.55),
    (B_EXH, "Dual 3 in straight-through, H-pipe", 1985, 620, -3, 3.0, 1, 0.25),
    (B_EXH, "Stainless dual 2-1/2 in, X-pipe", 1990, 780, -5, 2.5, 1, 0.32)]:
    part("exhaust", G, b, n, y, pr, m, {"pipe": vmm(pipe_in), "duals": duals, "muffler": muf})
# ignition (0 points, 1 electronic, 2 capacitive discharge)
for (b, n, y, pr, ign) in [
    (F, "Points distributor, tune-up kit", 1955, 60, 0),
    (B_IGN, "Race magneto", 1955, 650, 2),
    (F, "Factory transistorized ignition", 1964, 320, 1),
    (B_IGN, "Capacitive discharge ignition box", 1970, 380, 2),
    (B_IGN, "Magnetic-trigger electronic conversion", 1972, 240, 1),
    (F, "Factory large-cap electronic distributor", 1974, 220, 1),
    (B_IGN, "Multi-spark CD ignition + high-output coil", 1984, 460, 2)]:
    # Aftermarket systems come with a performance advance curve: they undo a smog-era spark calibration (tune).
    part("ignition", G, b, n, y, pr, 0, {"ign": ign, "tune": 1} if b != F else {"ign": ign})
part("ignition", G, B_IGN, "Distributor recurve kit (performance advance curve)", 1960, 90, 0, {"tune": 1})

# ---------------------------------------------------------------------------------------------- write
HDR = """# Forza Motorsport 4 engine builder: parts catalogue (Forza-4-Evolution).
# Format: docs/ENGINE_BUILDER.md, "Catalogue format". Tab separated, UTF-8, '#' starts a comment line.
#
#   F  <family> <display name>
#   E  <EngineID> <family> <display name> <key=value ...>        stock geometry of an FM4 engine (by EngineID)
#   P  <PartId> <slot> <families|*> <brand> <name> <year> <price> <mass_kg> <key=value | key+=value | key*=value ...>
#
# CLEANROOM: authored for this project from public real-world knowledge only (manufacturer engine specifications and
# ordinary aftermarket part specs). Nothing here comes from Motor City Online or any other game's data. Every brand
# except "Factory" is fictional. FM4 data used: EngineID, displacement, compression ratio and carbureted flag of each
# pilot engine and the car it sits in (to pick the factory engine); no FM4 torque curves are reproduced.
#
# Units and conventions
#   bore, stroke, rod, ivd, evd, pdia, plen, pipe, runner: millimetres (pipe is the diameter of each side)
#   cr: static compression ratio. rmass: crank+rods+pistons kg. rodrpm, float: rpm.
#   idur, edur: degrees of crank rotation at 0.050 in (1.27 mm) tappet lift. lsa: lobe separation, cam degrees.
#   ilift, elift: valve lift in mm QUOTED AT THE NOMINAL 1.5:1 ROCKER (= lobe lift x 1.5). The model scales it by
#       rocker/1.5, so the real valve lift is ilift x rocker / 1.5. Part names quote the real valve lift in inches
#       at that family's stock rocker ratio (e.g. a Ford FE cam named .490 at 1.73:1 has ilift = 10.79).
#   rocker: real rocker ratio. vpc: valves per cylinder. port: port efficiency, 1.0 = good production head.
#   cfm: carburettor airflow at 1.5 inHg; for EFI (carb=0) the throttle body + runner airflow rating.
#   filter: air cleaner restriction 0..0.1. muffler: 0 = open pipes .. 1 = very restrictive. octane: RON.
#   Enums (dyno.h): lifter 0 hydraulic flat, 1 solid flat, 2 hydraulic roller, 3 solid roller;
#       intake 0 dual plane, 1 single plane, 2 tunnel ram, 3 long-runner EFI, 4 log;
#       header 0 cast log, 1 cast performance, 2 shorty, 3 long tube, 4 tri-Y; ign 0 points, 1 electronic, 2 CD.
#   year: first model year the part is on sale. price: FM4 credits. mass_kg: change against the stock part.
#   Parts apply in slot order heads, cam, valvetrain, pistons, rotating, intake, carb, headers, exhaust, ignition,
#   so piston cr+= adds to the head's ratio. Piston parts are relative (bore+=, cr+=) so they fit every family.
#   A cam always sets its lifter type. Valve springs set float (valve float rpm) directly.
#
# Part ids: SSFFNN. SS = slot (11 heads, 12 cam, 13 valvetrain, 14 pistons, 15 rotating, 16 intake, 17 carb,
#   18 headers, 19 exhaust, 20 ignition), FF = family number below (00 = generic, fits every family), NN = sequence.
#   Ids are saved in player profiles: never renumber or reuse one, only add.
"""
lines = [HDR.rstrip("\n")]
lines.append("")
lines.append("# ------------------------------------------------------------------ families (number = FF in part ids)")
for (fid, name, rock, size, fyear, fno) in FAM:
    lines.append(f"# {fno:02d} {fid}: stock rocker {rock}, from {fyear}")
for (fid, name, *_ ) in FAM:
    lines.append(f"F\t{fid}\t{name}")
lines.append("")
lines.append("# ------------------------------------------------------------------ stock engines (FM4 EngineID)")
for (eid, fam, name, d, note) in E:
    if note: lines.append(f"# {eid}: {note}")
    lines.append("\t".join(["E", str(eid), fam, name] + mods(d)))
lines.append("")
lines.append("# ------------------------------------------------------------------ parts")
last = None
for (pid, slot, fams, b, n, y, pr, m, d) in sorted(P, key=lambda p: p[0]):
    if b == F and n.startswith("Factory "): n = n[len("Factory "):]  # the brand column already says Factory
    if slot != last:
        lines.append(f"# ---- {slot}")
        last = slot
    lines.append("\t".join(["P", str(pid), slot, fams, b, n, str(y), str(pr), fmt(float(m)) if m else "0"] + mods(d)))
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
print(len(P), "parts", len(E), "engines", len(FAM), "families")
