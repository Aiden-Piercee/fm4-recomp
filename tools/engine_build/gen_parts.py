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
                exhaust=19, ignition=20, blower=21, nitrous=22)
seq = {}
P = []
# Part id buckets: (slot, famno) holds parts NN = 1..99 in authoring order. famno 0 = generic (*), 1..15 = FAM below.
# A bucket that would pass 99 continues in famno + SPARE_BUCKET (51..65 for a family, 50 for generic). None is in
# use yet; the generator prints the fullest bucket after writing.
SPARE_BUCKET = 50
def part(slot, fams, brand, name, year, price, mass, m, famno=0):
    key = (slot, famno)
    seq[key] = seq.get(key, 0) + 1
    if seq[key] > 99:  # NN is two digits: start a spare bucket (famno + 50, see "Part id buckets" below) instead
        raise SystemExit(f"part bucket {slot}/{famno} passed 99 parts at '{name}': use famno + {SPARE_BUCKET}")
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

# ==============================================================================================================
# CATALOGUE EXPANSION (v2). Everything below is APPENDED after the v1 parts above, so every v1 part keeps its id:
# each (slot, famno) bucket continues its sequence here. Never insert a part above this line, and never reorder,
# remove or edit a part in either section (player builds are saved by PartId); only add at the end of a list here.
# ==============================================================================================================
B_HEADS2 = "Halvorsen Racing Heads"
B_CAM3 = "Copperline Cams"
B_BLW = "Stormcrow Superchargers"
B_TRB = "Ashgrove Turbo Systems"
B_N2O = "Bluewater Nitrous Systems"

# ---- heads, v2 (same tuple as HEADS)
HEADS2 = {
 "FORD_Y": [
  (F, "Factory 1955 272 heads 1.78/1.51", 1955, 400, 0, 1.78, 1.51, 0.82, -0.3),
  (F, "Factory 1956 312 high-compression heads 1.92/1.51", 1956, 700, 0, 1.92, 1.51, 0.88, 0.4),
  (B_HEADS2, "Competition ported 1957 heads 2.02/1.60", 1958, 2000, 0, 2.02, 1.60, 1.10, 0.5)],
 "CHEVY_SB": [
  (F, "Factory 1955 265 heads 1.72/1.50 (stock replacement)", 1955, 400, 0, 1.72, 1.50, 0.92, 0),
  (F, "Factory 1957 283 power-pack heads 1.72/1.50", 1957, 600, 0, 1.72, 1.50, 0.95, 0.3),
  (B_HEADS, "Port-and-polish 283 heads 1.72/1.50", 1956, 900, 0, 1.72, 1.50, 1.04, 0),
  (B_HEADS, "Oversize-valve ported 283 heads 1.84/1.50", 1957, 1300, 0, 1.84, 1.50, 1.07, 0.3),
  (F, "Factory 1958 fuel-injection heads 1.72/1.50", 1958, 800, 0, 1.72, 1.50, 0.97, 0.5),
  (B_HEADS2, "Competition ported iron 1.94/1.50", 1959, 1900, 0, 1.94, 1.50, 1.10, 0.3),
  (F, "Factory angle-plug 2.02/1.60 heads", 1969, 1400, 0, 2.02, 1.60, 1.05, 0.3),
  (F, "Factory 1971 low-compression heads 1.94/1.50 (76 cc)", 1971, 500, 0, 1.94, 1.50, 0.92, -0.6),
  (F, "Factory heavy-duty iron race heads 2.02/1.60", 1977, 2400, 0, 2.02, 1.60, 1.15, 0.3),
  (B_ALU, "Aluminum 23-degree street heads 2.02/1.60, 64 cc", 1985, 3000, -20, 2.02, 1.60, 1.15, 0.5),
  (B_HEADS2, "Aluminum 23-degree CNC heads 2.055/1.60", 1995, 4200, -20, 2.055, 1.60, 1.22, 0.5),
  (F, "Factory 1996 swirl-port truck iron heads 1.94/1.50", 1996, 700, 0, 1.94, 1.50, 1.08, 0.3),
  (B_HEADS, "CNC-ported swirl-port iron heads 2.02/1.60", 1998, 1800, 0, 2.02, 1.60, 1.16, 0.3),
  (B_HEADS2, "Aluminum 18-degree CNC race heads 2.15/1.62", 1996, 7200, -20, 2.15, 1.62, 1.35, 0.5)],
 "CHEVY_W": [
  (F, "Factory 1958 348 heads 1.94/1.66", 1958, 500, 0, 1.94, 1.66, 0.85, -0.5),
  (F, "Factory 1961 409 high-performance heads 2.07/1.72", 1961, 1100, 0, 2.07, 1.72, 0.95, 0),
  (F, "Factory 1962 409 dual-quad heads 2.19/1.72", 1962, 1600, 0, 2.19, 1.72, 1.0, 0.3)],
 "CHEVY_BB": [
  (F, "Factory 1966 'peanut port' heads 1.94/1.66", 1966, 400, 0, 1.94, 1.66, 0.85, -0.5),
  (F, "Factory L72 rectangle-port closed-chamber heads", 1966, 1600, 0, 2.19, 1.72, 1.05, 0.3),
  (B_HEADS, "Port-and-polish oval-port iron 2.06/1.72", 1966, 1100, 0, 2.06, 1.72, 1.02, 0),
  (B_HEADS2, "Competition ported rectangle-port iron 2.19/1.88", 1967, 2800, 0, 2.19, 1.88, 1.15, 0),
  (F, "Factory ZL1 aluminum open-chamber heads 2.19/1.88", 1969, 5200, -30, 2.19, 1.88, 1.16, -0.3),
  (F, "Factory 1971 aluminum heads 2.19/1.88", 1971, 4200, -30, 2.19, 1.88, 1.14, -0.3),
  (F, "Factory 1972 open-chamber smog heads 2.06/1.72", 1972, 500, 0, 2.06, 1.72, 0.92, -0.8),
  (F, "Factory heavy-duty iron rectangle-port race heads", 1977, 2600, 0, 2.19, 1.88, 1.15, 0),
  (B_HEADS2, "Aluminum spread-port race heads 2.40/1.90", 1988, 8400, -30, 2.40, 1.90, 1.38, 0.5),
  (B_ALU, "Aluminum oval-port race heads 2.25/1.88", 1990, 4400, -30, 2.25, 1.88, 1.20, 0.3),
  (F, "Factory 1991 truck iron heads 2.06/1.72", 1991, 600, 0, 2.06, 1.72, 1.0, -0.3),
  (B_HEADS2, "Aluminum CNC rectangle-port heads 2.30/1.88", 1996, 5800, -30, 2.30, 1.88, 1.32, 0.3),
  (F, "Factory 1997 aluminum crate-engine heads 2.19/1.88", 1997, 3200, -30, 2.19, 1.88, 1.12, 0.3)],
 "CHEVY_LT": [
  (B_HEADS, "Port-and-polish LT1 heads 1.94/1.50", 1993, 1400, 0, 1.94, 1.50, 1.12, 0),
  (B_HEADS2, "Aluminum CNC LT heads 2.05/1.60", 1998, 4600, 0, 2.05, 1.60, 1.28, 0.3)],
 "FORD_FE": [
  (F, "Factory 1958 352 heads 1.97/1.56", 1958, 500, 0, 1.97, 1.56, 0.88, -0.3),
  (F, "Factory 1960 352 high-performance heads 2.03/1.55", 1960, 900, 0, 2.03, 1.55, 0.95, 0.3),
  (F, "Factory 1961 390 high-performance heads 2.03/1.55", 1961, 1000, 0, 2.03, 1.55, 0.98, 0.3),
  (B_HEADS2, "Aluminum race FE heads 2.25/1.76", 1990, 5800, -28, 2.25, 1.76, 1.28, 0.5)],
 "FORD_WINDSOR": [
  (F, "Factory 221/260 heads 1.59/1.39", 1962, 400, 0, 1.59, 1.39, 0.88, -0.3),
  (F, "Factory 289 4V heads 1.67/1.45", 1963, 500, 0, 1.67, 1.45, 0.92, 0),
  (B_HEADS, "Port-and-polish 289 heads 1.78/1.45", 1964, 1000, 0, 1.78, 1.45, 1.04, 0),
  (F, "Factory 1968 302 heads 1.78/1.45", 1968, 500, 0, 1.78, 1.45, 0.95, -0.3),
  (F, "Factory Boss 302 canted-valve heads 2.23/1.71", 1969, 2400, 0, 2.23, 1.71, 1.10, -0.3),
  (F, "Factory 1977 low-compression 302 heads 1.69/1.45", 1977, 400, 0, 1.69, 1.45, 0.88, -0.5),
  (F, "Factory performance-parts iron heads 1.94/1.54", 1984, 1500, 0, 1.94, 1.54, 1.10, 0),
  (F, "Factory 1987 5.0 HO iron heads 1.78/1.45", 1987, 500, 0, 1.78, 1.45, 0.90, 0),
  (B_HEADS2, "Aluminum 20-degree race heads 2.05/1.60", 1990, 6400, -16, 2.05, 1.60, 1.32, 0.5),
  (B_ALU, "Aluminum street heads 2.02/1.60, 58 cc", 1992, 3200, -16, 2.02, 1.60, 1.20, 0.5),
  (B_HEADS, "CNC-ported GT40-type iron heads 1.94/1.54", 1995, 1900, 0, 1.94, 1.54, 1.15, 0),
  (F, "Factory 1996 GT40P iron heads 1.84/1.54", 1996, 900, 0, 1.84, 1.54, 1.06, 0.3)],
 "FORD_CLEVELAND": [
  (B_ALU, "Aluminum street heads 2.08/1.65", 1990, 3400, -26, 2.08, 1.65, 1.12, 0.3)],
 "FORD_385": [
  (F, "Factory 1973 460 low-compression heads 2.08/1.66", 1973, 500, 0, 2.08, 1.66, 0.92, -0.8)],
 "FORD_BOSS429": [
  (B_HEADS2, "Aluminum CNC semi-hemi heads 2.40/1.94", 1995, 8800, -4, 2.40, 1.94, 1.36, 0.5)],
 "MOPAR_B_RB": [
  (F, "Factory 1958 B-engine heads 1.95/1.60", 1958, 400, 0, 1.95, 1.60, 0.92, 0),
  (F, "Factory 1959 413 RB heads 2.08/1.60", 1959, 600, 0, 2.08, 1.60, 0.95, 0),
  (B_HEADS, "Port-and-polish B/RB heads 2.08/1.74", 1959, 1000, 0, 2.08, 1.74, 1.04, 0),
  (F, "Factory 1960 ram-induction 413 heads", 1960, 900, 0, 2.08, 1.74, 1.0, 0.3),
  (F, "Factory 1963 Max Wedge Stage III heads 2.08/1.88", 1963, 3000, 0, 2.08, 1.88, 1.15, 0.5),
  (B_HEADS2, "Competition ported Max Wedge iron 2.14/1.88", 1963, 2900, 0, 2.14, 1.88, 1.15, 0.3),
  (F, "Factory 2-barrel heads 1.95/1.60", 1966, 400, 0, 1.95, 1.60, 0.92, 0),
  (F, "Factory 915 closed-chamber heads 2.08/1.74", 1966, 700, 0, 2.08, 1.74, 1.0, 0.3),
  (F, "Factory 346 heads 2.08/1.74 (440 HP)", 1968, 700, 0, 2.08, 1.74, 1.0, 0),
  (F, "Factory performance-parts iron race heads 2.14/1.81", 1988, 2400, 0, 2.14, 1.81, 1.15, 0),
  (B_HEADS2, "Aluminum CNC wedge race heads 2.20/1.81", 1996, 6800, -28, 2.20, 1.81, 1.32, 0.5),
  (B_ALU, "Aluminum Max Wedge-type heads 2.14/1.88", 1998, 4600, -28, 2.14, 1.88, 1.25, 0.3)],
 "MOPAR_HEMI": [
  (F, "Factory 1964 race Hemi iron heads", 1964, 3600, 0, 2.25, 1.94, 1.12, 0.3),
  (B_HEADS2, "Aluminum CNC Hemi heads 2.40/1.94", 1995, 8600, -30, 2.40, 1.94, 1.40, 0.5)],
 "PONTIAC_V8": [
  (F, "Factory 1955 287 heads 1.78/1.50", 1955, 400, 0, 1.78, 1.50, 0.82, -0.6),
  (F, "Factory 1957 347 high-performance heads 1.92/1.66", 1957, 700, 0, 1.92, 1.66, 0.90, 0),
  (F, "Factory 1959 389 Tri-Power heads 1.92/1.66", 1959, 800, 0, 1.92, 1.66, 0.95, 0),
  (F, "Factory 1962 Super Duty heads 2.02/1.77", 1962, 1600, 0, 2.02, 1.77, 1.05, 0.3)],
 "OLDS_V8": [
  (F, "Factory 1965 400 heads 2.07/1.63", 1965, 600, 0, 2.07, 1.63, 0.92, 0)],
 "AMC_V8": [
  (F, "Factory 1966 290/343 heads 1.79/1.40", 1966, 400, 0, 1.79, 1.40, 0.82, -0.5)],
}

# ---- cams, v2: more generic tiers (big-port families get +4 degrees and +.030 lift, as in v1), then factory cams
CAM_TIERS2 = [
 (B_CAM1, "Mild Hydraulic", 1955, 300, 204, 214, .440, .460, 112, 0),
 (B_CAM1, "Three-Quarter Race Solid", 1955, 360, 228, 228, .460, .460, 112, 1),
 (B_CAM2, "Hot Street Hydraulic", 1958, 450, 224, 230, .480, .495, 110, 0),
 (B_CAM2, "Full Race Solid", 1958, 540, 248, 248, .540, .540, 108, 1),
 (B_CAM3, "Strip Hydraulic", 1962, 480, 230, 236, .495, .510, 110, 0),
 (B_CAM2, "Drag Solid", 1962, 680, 266, 272, .580, .590, 106, 1),
 (B_ROLL, "Street Solid Roller", 1970, 1250, 250, 256, .580, .590, 108, 3),
 (B_ROLL, "Drag Solid Roller", 1972, 1550, 272, 280, .680, .690, 106, 3),
 (B_CAM1, "Boost Hydraulic (wide LSA)", 1980, 480, 214, 222, .470, .490, 114, 0),
 (B_ROLL, "Pro Race Solid Roller", 1985, 1800, 280, 288, .720, .730, 106, 3),
 (B_ROLL, "Mild Hydraulic Roller", 1986, 1050, 206, 212, .480, .490, 114, 2),
 (B_CAM3, "Hot Hydraulic Roller", 1988, 1200, 224, 230, .540, .550, 112, 2),
 (B_ROLL, "Boost Hydraulic Roller (wide LSA)", 1990, 1250, 222, 230, .530, .550, 116, 2),
 (B_ROLL, "Big Hydraulic Roller", 1995, 1300, 236, 242, .580, .590, 108, 2),
]
FACTORY_CAMS2 = {  # same tuple as FACTORY_CAMS
 "CHEVY_SB": [("Factory 283 hydraulic (stock replacement)", 1957, 150, 180, 190, .390, .410, 112, 0, None),
              ("Factory 350 hp 327 hydraulic", 1965, 300, 195, 202, .447, .459, 114, 0, None),
              ("Factory L98 hydraulic roller", 1987, 450, 202, 207, .403, .415, 115, 2, None),
              ("Factory crate-engine hydraulic roller", 1996, 550, 208, 221, .474, .510, 112, 2, None)],
 "CHEVY_BB": [("Factory 396 hydraulic (325 hp)", 1965, 250, 195, 205, .398, .398, 114, 0, None),
              ("Factory L72 solid", 1966, 450, 242, 254, .520, .520, 114, 1, None),
              ("Factory LS5 hydraulic", 1970, 300, 208, 221, .480, .480, 114, 0, None),
              ("Factory crate-engine hydraulic roller", 1995, 600, 211, 230, .527, .544, 113, 2, None)],
 "FORD_WINDSOR": [("Factory 289 2V hydraulic", 1963, 200, 190, 198, .368, .380, 114, 0, None),
                  ("Factory 351W hydraulic", 1969, 250, 196, 206, .418, .448, 114, 0, None),
                  ("Factory Boss 302 solid", 1969, 450, 236, 236, .477, .477, 116, 1, None),
                  ("Factory performance-parts hydraulic roller", 1987, 650, 224, 232, .542, .542, 112, 2, None)],
 "MOPAR_B_RB": [("Factory 383 hydraulic (2-barrel)", 1959, 200, 186, 196, .390, .406, 112, 0, None),
                ("Factory 440 HP hydraulic", 1967, 300, 208, 216, .450, .458, 112, 0, None),
                ("Factory performance-parts solid", 1970, 450, 246, 254, .528, .536, 108, 1, None)],
 "FORD_FE": [("Factory 390 GT hydraulic", 1966, 250, 202, 210, .440, .440, 115, 0, None)],
 "PONTIAC_V8": [("Factory 389 Tri-Power hydraulic", 1959, 250, 196, 204, .410, .410, 113, 0, None)],
}

# ---- valvetrain, v2: rocker kits generated from the family's stock ratio (brand, name, year, price, rocker, float)
def rockers2(r):
    up = round(r + 0.1, 2)
    return [(F, f"Factory {r} rockers + heavy-duty springs", 1955, 120, r, 6000),
            (B_VT, f"{r} roller-tip rockers + dual springs", 1962, 320, r, 6800),
            (B_VT, f"{up} long-slot rockers + dual springs", 1966, 300, up, 6600),
            (B_VT, f"{r} full roller rockers + race springs", 1970, 560, r, 7600),
            (B_VT, f"{up} full roller rockers + race springs", 1985, 620, up, 7400),
            (B_VT, f"{r} shaft-mount roller rockers + race springs", 1990, 1100, r, 8000)]

# ---- rotating, v2 (R tuples)
ROT2 = {
 "CHEVY_SB": [R(F, "Factory forged 3.00 crank (283)", 1957, 1200, 0, 3.00, 5.7, 0, 7000),
              R(F, "Factory forged 3.25 crank (327)", 1962, 1400, 0, 3.25, 5.7, 0, 7200),
              R(F, "Factory cast 3.48 crank (350)", 1968, 700, 1, 3.48, 5.7, 1, 6200),
              R(B_ROT, "Aluminum-rod drag assembly (3.00)", 1970, 6800, -4, 3.00, 5.7, -4, 8800),
              R(B_ROT, "3.75 forged crank + 5.7 rods (383 street)", 1982, 5200, 2, 3.75, 5.7, 2, 6600),
              R(B_ROT, "Forged 3.48 crank + 6.0 rods (long-rod 350)", 1985, 4800, 0, 3.48, 6.0, 0, 7400),
              R(B_ROT, "3.875 forged stroker + 6.0 rods (396 small-block)", 1995, 7200, 2, 3.875, 6.0, 2, 6800),
              R(B_ROT, "4.00 forged stroker + 6.0 rods (406)", 1996, 7600, 3, 4.00, 6.0, 3, 6400),
              R(B_ROT, "Billet 3.48 crank + titanium rods (race)", 1995, 14000, -6, 3.48, 5.7, -6, 9000)],
 "CHEVY_W": [R(F, "Factory 348 forged crank + rods", 1958, 1400, 0, rodrpm=6400)],
 "CHEVY_BB": [R(F, "Factory 396 forged crank, 3.76", 1965, 1800, 0, 3.76, 6.135, 0, 6500),
              R(F, "Factory 454 cast crank, 4.00", 1973, 1400, 3, 4.00, 6.135, 3, 5800),
              R(B_ROT, "3.76 billet crank + aluminum rods (race 427)", 1975, 12500, -6, 3.76, 6.135, -6, 8200),
              R(B_ROT, "Forged 4.00 crank + 6.385 rods (long-rod 454)", 1985, 6200, 1, 4.00, 6.385, 1, 6800),
              R(F, "Factory 1991 forged 4.00 crank + rods (502 crate)", 1991, 3600, 2, 4.00, 6.135, 2, 6600),
              R(B_ROT, "4.375 forged stroker + 6.535 rods (509)", 1995, 8800, 4, 4.375, 6.535, 4, 6200),
              R(B_ROT, "4.50 forged stroker + 6.385 rods (540)", 1996, 9800, 4, 4.50, 6.385, 4, 6200)],
 "FORD_FE": [R(F, "Factory 1958 forged crank + rods (stock stroke)", 1958, 1400, 0, rodrpm=6400)],
 "FORD_WINDSOR": [R(F, "Factory 260 crank, 2.87 + 5.155 rods", 1962, 600, 0, 2.87, 5.155, 0, 6400),
                  R(F, "Factory Boss 302 forged crank + rods", 1969, 2800, 0, 3.00, 5.155, 0, 7600),
                  R(B_ROT, "Aluminum-rod drag assembly (2.87)", 1968, 6400, -4, 2.87, 5.155, -4, 8800),
                  R(B_ROT, "3.25 forged stroker + 5.315 rods (331 street)", 1990, 4800, 1, 3.25, 5.315, 1, 6600),
                  R(B_ROT, "3.00 forged crank + 5.4 rods (long-rod 302)", 1990, 4200, 0, 3.00, 5.4, 0, 7600),
                  R(B_ROT, "3.00 billet crank + titanium rods (race)", 1995, 13000, -5, 3.00, 5.315, -5, 9000),
                  R(B_ROT, "3.40 billet stroker + 5.4 H-beam rods (347 race)", 1996, 9800, 0, 3.40, 5.4, 0, 7400)],
 "MOPAR_B_RB": [R(F, "Factory 383 forged crank, 3.375", 1959, 900, 0, 3.375, 6.358, 0, 6000),
                R(F, "Factory 440 forged crank, 3.75", 1966, 1600, 1, 3.75, 6.768, 1, 5800),
                R(F, "Factory 400 cast crank, 3.38", 1972, 700, 0, 3.38, 6.768, 0, 5600),
                R(B_ROT, "Forged 3.75 crank + 7.1 rods (long-rod RB)", 1990, 5200, 0, 3.75, 7.1, 0, 6400),
                R(B_ROT, "3.91 forged stroker + 6.76 rods (RB 470)", 1992, 6800, 2, 3.91, 6.76, 2, 6000),
                R(B_ROT, "4.25 forged stroker + 6.76 rods (RB 512)", 1996, 8200, 3, 4.25, 6.76, 3, 6000)],
 "MOPAR_HEMI": [R(F, "Factory 1966 street Hemi forged crank + rods", 1966, 3200, 0, rodrpm=7000)],
 "PONTIAC_V8": [R(F, "Factory 1960 Super Duty forged crank + rods", 1960, 2600, 0, rodrpm=6200)],
}

# ---- intakes, v2: generated set for carburetted families (brand, name, year, price, mass, mods), then specials
def intakes2(size):
    efi = {"s": 750, "m": 900, "b": 1000}[size]
    return [(F, "Factory cast-iron 2-barrel manifold", 1955, 80, 4, {"intake": 0, "port*": 0.96}),
            (F, "Factory cast-iron 4-barrel manifold", 1955, 120, 3, {"intake": 0}),
            (B_INT, "Aluminum dual 4-barrel manifold (2x4)", 1955, 340, -8, {"intake": 0, "port*": 1.01}),
            (B_INT, "Aluminum 3x2-barrel manifold", 1955, 320, -8, {"intake": 0, "port*": 1.01}),
            (B_INT, "Aluminum low-rise dual plane, 4-barrel", 1956, 240, -9, {"intake": 0}),
            (B_INT, "Aluminum 6x2-barrel log manifold", 1956, 380, -6, {"intake": 4, "port*": 0.98}),
            (B_INT, "Street single plane, mid-rise 4-barrel", 1972, 330, -9, {"intake": 1, "port*": 1.03}),
            (B_INT, "Tunnel ram, single 4-barrel", 1972, 520, -5, {"intake": 2, "port*": 1.04}),
            (B_INT, "Race single plane, high-rise 4-barrel", 1976, 420, -9, {"intake": 1, "port*": 1.06}),
            (B_INT, "Port-matched dual plane (street)", 1980, 360, -9, {"intake": 0, "port*": 1.03}),
            (B_INT, "Sheet-metal race tunnel ram, 2x4-barrel", 1985, 1500, -7, {"intake": 2, "port*": 1.10}),
            (B_INT, "Air-gap dual plane, 4-barrel", 1993, 380, -9, {"intake": 0, "port*": 1.04}),
            (B_EFI, "Multi-port EFI long-runner intake kit", 1995, 1800, -6,
             {"intake": 3, "carb": 0, "cfm": efi, "filter": 0.04})]
INTAKE_SPECIAL2 = {
 "CHEVY_SB": [(F, "Factory aluminum dual 4-barrel (Corvette)", 1956, 450, -8, {"intake": 0, "port*": 1.01}),
              (F, "Factory mechanical fuel injection (ram-tube plenum)", 1957, 1100, 0,
               {"intake": 3, "carb": 0, "cfm": 550, "filter": 0.05}),
              (F, "Factory aluminum high-rise (Z/28, LT-1)", 1969, 500, -9, {"intake": 0, "port*": 1.03}),
              (B_INT, "Aftermarket cross-ram dual 4-barrel", 1970, 800, -6, {"intake": 1, "runner": 330, "port*": 1.04}),
              (F, "Factory cast-iron spread-bore manifold (low compression)", 1971, 150, 3, {"intake": 0, "port*": 0.97})],
 "CHEVY_BB": [(F, "Factory cast-iron oval-port 4-barrel", 1965, 150, 4, {"intake": 0}),
              (F, "Factory aluminum 3x2-barrel (L71)", 1967, 600, -9, {"intake": 0, "port*": 1.01}),
              (B_INT, "Cross-ram dual 4-barrel (big-block)", 1968, 900, -6, {"intake": 1, "runner": 380, "port*": 1.04}),
              (F, "Factory aluminum open-plenum high-rise (ZL1)", 1969, 800, -10, {"intake": 1, "port*": 1.05}),
              (F, "Factory throttle-body injection truck intake", 1987, 500, 2,
               {"intake": 0, "carb": 0, "cfm": 700, "filter": 0.06, "port*": 0.95})],
 "CHEVY_LT": [(B_EFI, "Port-matched factory LT1 EFI intake", 1994, 1100, 0, {"intake": 3, "carb": 0, "cfm": 950, "port*": 1.02}),
              (B_INT, "Carburetted single-plane conversion, 4-barrel", 1995, 900, -4,
               {"intake": 1, "carb": 1, "cfm": 750, "filter": 0.03, "port*": 1.03}),
              (B_EFI, "Large-plenum LT EFI intake", 1997, 1700, -1, {"intake": 3, "carb": 0, "cfm": 1050, "port*": 1.04}),
              (B_EFI, "Sheet-metal EFI race intake", 1998, 2600, -3, {"intake": 1, "carb": 0, "cfm": 1200, "port*": 1.08})],
 "FORD_WINDSOR": [(F, "Factory 289 cast-iron 4V manifold", 1963, 120, 3, {"intake": 0}),
                  (F, "Factory 289 aluminum dual 4-barrel", 1963, 500, -8, {"intake": 0, "port*": 1.02}),
                  (F, "Factory Boss 302 aluminum high-rise", 1969, 550, -9, {"intake": 1, "port*": 1.04}),
                  (F, "Factory Boss 302 cross-ram dual 4-barrel (race)", 1969, 1200, -6,
                   {"intake": 1, "runner": 330, "port*": 1.05}),
                  (B_EFI, "Tall long-runner EFI box intake", 1992, 1500, -2,
                   {"intake": 3, "carb": 0, "cfm": 800, "port*": 1.04, "filter": 0.04})],
 "MOPAR_B_RB": [(F, "Factory cast-iron 2-barrel (383)", 1959, 80, 4, {"intake": 0, "port*": 0.96}),
                (F, "Factory 1960 long-ram cross-ram (2x4)", 1960, 900, 6, {"intake": 1, "runner": 760}),
                (F, "Factory 1961 short-ram cross-ram (2x4)", 1961, 950, 6, {"intake": 1, "runner": 380, "port*": 1.02}),
                (F, "Factory cast-iron 4-barrel (440 HP)", 1966, 140, 4, {"intake": 0}),
                (F, "Factory performance-parts aluminum high-rise", 1975, 450, -9, {"intake": 1, "port*": 1.04})],
 "FORD_FE": [(F, "Factory 1960 aluminum 4-barrel (352 HP)", 1960, 400, -8, {"intake": 0, "port*": 1.01})],
 "PONTIAC_V8": [(F, "Factory 1957 aluminum 3x2-barrel", 1957, 450, -8, {"intake": 0, "port*": 1.01})],
 "CHEVY_W": [(F, "Factory 1958 348 3x2-barrel manifold", 1958, 400, 0, {"intake": 0, "port*": 1.0})],
}

# ---- headers, v2 per family (generated from the size class), plus family factory manifolds
def headers2(size):
    p1, p2, p3 = PDIA[size]
    L = [(F, "Factory cast-iron exhaust manifolds (stock replacement)", 1955, 120, 0, {"header": 0}),
         (B_HDR, f"Lakes-style tube headers, {frac(p2)} in", 1955, 380, -6, {"header": 3, "pdia": vmm(p2), "plen": 600}),
         (B_HDR, f"Zoomie open headers, {frac(p3)} in (race)", 1955, 300, -12, {"header": 3, "pdia": vmm(p3), "plen": 450}),
         (B_HDR, f"Fenderwell tube headers, {frac(p2)} in", 1958, 520, -8, {"header": 3, "pdia": vmm(p2), "plen": 700}),
         (B_HDR, f"Long-tube street headers, {frac(p1)} in", 1960, 560, -9, {"header": 3, "pdia": vmm(p1), "plen": 760}),
         (B_HDR, f"180-degree race headers, {frac(p3)} in", 1966, 2400, -8, {"header": 3, "pdia": vmm(p3), "plen": 900}),
         (B_HDR, f"Block-hugger headers, {frac(p2)} in", 1970, 480, -6, {"header": 2, "pdia": vmm(p2), "plen": 450}),
         (B_HDR, f"Ceramic-coated long-tube headers, {frac(p2)} in", 1985, 900, -9, {"header": 3, "pdia": vmm(p2), "plen": 760}),
         (B_HDR, f"Stepped long-tube race headers, {frac(p2)} to {frac(p3)} in", 1985, 1300, -11,
          {"header": 3, "pdia": vmm(p3), "plen": 860}),
         (B_HDR, f"Mid-length headers, {frac(p2)} in", 1992, 700, -8, {"header": 3, "pdia": vmm(p2), "plen": 560}),
         (B_HDR, f"Stainless long-tube race headers, {frac(p3)} in", 1995, 1500, -11, {"header": 3, "pdia": vmm(p3), "plen": 860})]
    if size == "s":
        L.append((B_HDR, f"Tri-Y street headers, {frac(p1)} in", 1965, 640, -8, {"header": 4, "pdia": vmm(p1), "plen": 760}))
    return L
HEADERS_SPECIAL2 = {
 "CHEVY_BB": [(F, "Factory L88 high-performance cast manifolds", 1967, 300, 0, {"header": 1})],
 "MOPAR_B_RB": [(F, "Factory Max Wedge cast-iron headers", 1962, 600, 0, {"header": 1}),
                (F, "Factory 440 high-performance cast manifolds", 1966, 300, 0, {"header": 1})],
 "FORD_WINDSOR": [(F, "Factory 5.0 HO tubular shorty headers", 1986, 350, -3, {"header": 2, "pdia": vmm(1.5), "plen": 380})],
 "FORD_FE": [(F, "Factory 427 high-performance cast manifolds", 1963, 300, 0, {"header": 1})],
}

# ---- blower kits per family (mounting, manifold and drive differ per block). Tuple: brand, name, year, price, mass,
# type (1 Roots, 2 twin-screw, 3 centrifugal, 4 turbo), boost psi, boost rpm, adiabatic efficiency, intercooler.
def blowers2(size):
    small = size == "s"
    cr1, cr2 = (5500, 6000) if small else (5000, 5500)
    L = []
    if small:
        L += [(B_BLW, "4-71 Roots blower, 2x4-barrel, 5 psi", 1955, 1400, 32, 1, 5, 0, 0.50, 0),
              (B_BLW, "4-71 Roots blower, 2x4-barrel, 8 psi (overdrive pulley)", 1955, 1450, 32, 1, 8, 0, 0.50, 0)]
    L += [(B_BLW, "6-71 Roots blower, 2x4-barrel, 6 psi", 1956, 1700, 38, 1, 6, 0, 0.52, 0),
          (B_BLW, "6-71 Roots blower, 2x4-barrel, 10 psi", 1957, 1750, 38, 1, 10, 0, 0.52, 0),
          (B_BLW, "Belt-drive centrifugal supercharger, 5 psi", 1957, 1200, 17, 3, 5, cr1, 0.62, 0),
          (B_BLW, "Belt-drive centrifugal supercharger, 7 psi (small pulley)", 1958, 1250, 17, 3, 7, cr1, 0.62, 0),
          (B_BLW, "Street Roots blower, single 4-barrel, 4 psi", 1960, 1100, 30, 1, 4, 0, 0.52, 0)]
    if small:
        L += [(B_BLW, "8-71 Roots race blower, 2x4-barrel, 14 psi", 1965, 2600, 42, 1, 14, 0, 0.53, 0)]
    else:
        L += [(B_BLW, "8-71 Roots blower, 2x4-barrel, 8 psi", 1962, 2100, 42, 1, 8, 0, 0.53, 0),
              (B_BLW, "8-71 Roots blower, 2x4-barrel, 12 psi", 1962, 2150, 42, 1, 12, 0, 0.53, 0),
              (B_BLW, "10-71 Roots race blower, 2x4-barrel, 15 psi", 1974, 3200, 44, 1, 15, 0, 0.54, 0),
              (B_BLW, "14-71 Roots drag blower, 2x4-barrel, 18 psi", 1980, 4800, 45, 1, 18, 0, 0.55, 0)]
    L += [(B_BLW, "Belt-drive centrifugal race supercharger, 10 psi", 1966, 1500, 18, 3, 10, cr2, 0.63, 0),
          (B_TRB, "Single turbo kit, draw-through carb, 6 psi", 1980, 2600, 24, 4, 6, 3500, 0.65, 0),
          (B_TRB, "Single turbo kit, blow-through, 8 psi", 1983, 3000, 24, 4, 8, 3600, 0.66, 0),
          (B_TRB, "Twin-turbo kit, 8 psi", 1984, 4800, 34, 4, 8, 3200, 0.66, 0),
          (B_BLW, "6-71 Roots blower, polished street kit, 7 psi", 1985, 2400, 36, 1, 7, 0, 0.55, 0),
          (B_BLW, "Gear-drive centrifugal supercharger, 8 psi", 1987, 2600, 18, 3, 8, cr1, 0.72, 0),
          (B_TRB, "Intercooled single turbo kit, 10 psi", 1988, 4200, 30, 4, 10, 3800, 0.70, 0.65),
          (B_BLW, "Gear-drive centrifugal supercharger, 12 psi", 1990, 2900, 18, 3, 12, cr2, 0.72, 0),
          (B_TRB, "Intercooled twin-turbo kit, 12 psi", 1990, 6200, 40, 4, 12, 3400, 0.70, 0.70),
          (B_BLW, f"Twin-screw supercharger, {'2.2' if small else '3.3'} L, 8 psi", 1991, 2800, 28, 2, 8, 0, 0.68, 0),
          (B_BLW, f"Twin-screw supercharger, {'3.3' if small else '4.0'} L, 12 psi", 1995, 3800, 32, 2, 12, 0, 0.68, 0),
          (B_BLW, "Intercooled gear-drive centrifugal, 14 psi", 1995, 4200, 20, 3, 14, cr2, 0.74, 0.65),
          (B_BLW, "Roots blower with EFI hat, 2x throttle body, 8 psi", 1995, 3400, 38, 1, 8, 0, 0.56, 0),
          (B_TRB, "Intercooled twin-turbo race kit, 18 psi", 1996, 8800, 42, 4, 18, 3800, 0.72, 0.75),
          (B_BLW, f"Intercooled twin-screw supercharger, {'3.3' if small else '4.0'} L, 14 psi", 1998, 5200, 38, 2, 14, 0, 0.70, 0.60),
          (B_TRB, "Large single turbo race kit, intercooled, 20 psi", 1998, 7600, 36, 4, 20, 4500, 0.72, 0.75)]
    return L

def gen_family_parts2(fid, fname, rock, size, fyear, fno):
    yr = lambda y: max(y, fyear)
    for (b, n, y, pr, m, iv, ev, po, crp) in HEADS2.get(fid, []):
        d = {"ivd": vmm(iv), "evd": vmm(ev), "port": po}
        if crp: d["cr+"] = crp
        part("heads", fid, b, n, yr(y), pr, m, d, fno)
    big = size in ("m", "b")
    for (b, lab, y, pr, idur, edur, il, el, lsa, lft) in CAM_TIERS2:
        if big: idur, edur, il, el = idur + 4, edur + 4, il + .030, el + .030
        if fid == "CHEVY_LT" and lft in (0, 1):
            continue
        name = f"{lab} {idur}/{edur} (.{round(il*1000):03d}/.{round(el*1000):03d})"
        part("cam", fid, b, name, yr(y), pr, 0,
             {"idur": idur, "edur": edur, "ilift": nl(il, rock), "elift": nl(el, rock), "lsa": lsa, "lifter": lft}, fno)
    for (n, y, pr, idur, edur, il, el, lsa, lft, r) in FACTORY_CAMS2.get(fid, []):
        r = r or rock
        part("cam", fid, F, n, yr(y), pr, 0,
             {"idur": idur, "edur": edur, "ilift": nl(il, r), "elift": nl(el, r), "lsa": lsa, "lifter": lft}, fno)
    for (b, n, y, pr, rk, fl) in rockers2(rock):
        part("valvetrain", fid, b, n, yr(y), pr, 0, {"rocker": rk, "float": fl}, fno)
    for (b, n, y, pr, m, d) in ROT2.get(fid, []):
        part("rotating", fid, b, n, yr(y), pr, m, d, fno)
    if fid != "CHEVY_LT":
        for (b, n, y, pr, m, d) in intakes2(size):
            part("intake", fid, b, n, yr(y), pr, m, d, fno)
    for (b, n, y, pr, m, d) in INTAKE_SPECIAL2.get(fid, []):
        part("intake", fid, b, n, yr(y), pr, m, d, fno)
    for (b, n, y, pr, m, d) in headers2(size) + HEADERS_SPECIAL2.get(fid, []):
        part("headers", fid, b, n, yr(y), pr, m, d, fno)
    for (b, n, y, pr, m, typ, psi, brpm, eff, ic) in blowers2(size):
        d = {"blower": typ, "boost": psi, "boostrpm": brpm, "beff": eff}
        if ic: d["ic"] = ic
        part("blower", fid, b, n, yr(y), pr, m, d, fno)

for f in FAM:
    gen_family_parts2(*f)

# ---- generic parts, v2 (fit every family)
# heads: work on the stock castings (relative, so they fit every family)
for (b, n, y, pr, d) in [
    (B_HEADS, "Valve job + bowl blend (stock castings)", 1955, 180, {"port*": 1.03}),
    (B_HEADS, "Port and polish (stock castings)", 1955, 400, {"port*": 1.06}),
    (B_HEADS, "Milled .030 for compression (stock castings)", 1955, 120, {"cr+": 0.4}),
    (B_HEADS2, "Oversize valves +.060 + port job (stock castings)", 1956, 650, {"ivd+": 1.5, "evd+": 1.5, "port*": 1.08}),
    (B_HEADS2, "Full competition port job + milled .040", 1957, 900, {"port*": 1.12, "cr+": 0.5}),
    (B_HEADS, "CNC port job (stock castings)", 1995, 1200, {"port*": 1.12})]:
    part("heads", G, b, n, y, pr, 0, d)
# valve springs
for (b, n, y, pr, fl) in [
    (B_VT, "Heavy-duty springs + hardened pushrods", 1956, 140, 6000),
    (B_VT, "Dual springs + steel retainers (solid cam)", 1957, 180, 6500),
    (B_VT, "Dual springs, titanium retainers, hardened pushrods", 1965, 320, 7200),
    (B_VT, "Triple race springs (solid roller), titanium retainers", 1985, 700, 8800),
    (B_VT, "Conical springs + lightweight retainers (street)", 1996, 260, 6800),
    (B_VT, "Ovate-wire race springs (hydraulic roller)", 1998, 480, 7400)]:
    part("valvetrain", G, b, n, y, pr, 0, {"float": fl})
# pistons: more compression steps, and low-compression pistons for a blower, turbo or nitrous
for (b, n, y, pr, m, d) in [
    (F, "Cast high-compression pistons, .030 over", 1955, 300, 0, {"bore+": 0.76, "cr+": 0.6}),
    (F, "Cast low-compression pistons, .030 over", 1955, 260, 0, {"bore+": 0.76, "cr+": -0.8}),
    (F, "Cast replacement pistons, .060 over", 1955, 280, 0.2, {"bore+": 1.52, "cr+": 0.2}),
    (B_PIST, "Forged pop-up pistons, .040 over", 1957, 640, 0.3, {"bore+": 1.02, "cr+": 1.5}),
    (B_PIST, "Forged blower pistons (7.5:1), .030 over", 1958, 600, -0.4, {"bore+": 0.76, "cr": 7.5}),
    (B_PIST, "Forged flat-top pistons, .020 over", 1959, 500, -0.4, {"bore+": 0.51, "cr+": 0.5}),
    (B_PIST, "Forged blower pistons (6.5:1, race), .030 over", 1960, 650, -0.4, {"bore+": 0.76, "cr": 6.5}),
    (B_PIST, "Forged flat-top pistons, .040 over", 1965, 540, -0.4, {"bore+": 1.02, "cr+": 0.9}),
    (B_PIST, "Forged high-dome race pistons, .060 over", 1966, 760, 0.5, {"bore+": 1.52, "cr+": 2.5}),
    (B_PIST, "Forged low-compression pistons (unleaded fuel, -1.0), .030 over", 1972, 480, -0.3, {"bore+": 0.76, "cr+": -1.0}),
    (B_PIST, "Forged dished pistons for boost (8.5:1), .030 over", 1975, 580, -0.3, {"bore+": 0.76, "cr": 8.5}),
    (B_PIST, "Forged 10.0:1 flat-top pistons, .030 over", 1980, 560, -0.4, {"bore+": 0.76, "cr": 10.0}),
    (B_PIST, "Forged dished turbo pistons (8.0:1), .040 over", 1984, 620, -0.3, {"bore+": 1.02, "cr": 8.0}),
    (B_PIST, "Forged dished pistons for boost (9.0:1), .030 over", 1985, 640, -0.3, {"bore+": 0.76, "cr": 9.0}),
    (B_PIST, "Hypereutectic dished pistons (-0.5), .030 over", 1985, 360, -0.2, {"bore+": 0.76, "cr+": -0.5}),
    (B_PIST, "Hypereutectic dished pistons for boost (8.5:1), .030 over", 1988, 380, -0.2, {"bore+": 0.76, "cr": 8.5}),
    (B_PIST, "Lightweight gas-ported forged pistons, .040 over", 1992, 920, -1.4, {"bore+": 1.02, "cr+": 1.2}),
    (B_PIST, "Forged boost pistons (9.5:1), .030 over", 1995, 680, -0.6, {"bore+": 0.76, "cr": 9.5})]:
    part("pistons", G, b, n, y, pr, m, d)
# crank and rods that keep the stock stroke (relative, so they fit every family)
for (b, n, y, pr, m, d) in [
    (F, "Balance and blueprint (stock parts)", 1955, 300, 0, {"rodrpm+": 300}),
    (B_ROT, "High-strength rod bolts + shot-peened rods", 1955, 260, 0, {"rodrpm+": 400}),
    (B_ROT, "Knife-edged, lightened crank", 1957, 900, -1, {"rmass+": -2, "rodrpm+": 200}),
    (B_ROT, "Forged steel rods (stock length)", 1958, 900, 0, {"rodrpm+": 700}),
    (B_ROT, "Aluminum drag rods (stock length)", 1965, 1100, -1.5, {"rmass+": -1.5, "rodrpm+": 900}),
    (B_ROT, "Billet steel crank (stock stroke)", 1985, 3800, -1, {"rmass+": -1, "rodrpm+": 800}),
    (B_ROT, "H-beam forged rods (stock length)", 1990, 800, 0, {"rodrpm+": 900}),
    (B_ROT, "Titanium rods (stock length)", 1990, 3200, -2, {"rmass+": -2.5, "rodrpm+": 1200})]:
    part("rotating", G, b, n, y, pr, m, d)
# carburettors and throttle bodies
for (b, n, y, pr, d) in [
    (F, "Factory 4-barrel, 400 cfm", 1955, 150, {"carb": 1, "cfm": 400, "filter": 0.06}),
    (B_CARB, "Dual 2-barrel, 2x 250 cfm", 1955, 260, {"carb": 1, "cfm": 500, "filter": 0.04}),
    (B_CARB, "Dual 4-barrel, 2x 400 cfm", 1956, 480, {"carb": 1, "cfm": 800, "filter": 0.03}),
    (F, "Factory high-performance 4-barrel, 500 cfm", 1957, 200, {"carb": 1, "cfm": 500, "filter": 0.05}),
    (B_CARB, "Triple 2-barrel, 750 cfm (street)", 1957, 420, {"carb": 1, "cfm": 750, "filter": 0.04}),
    (B_CARB, "Mechanical constant-flow race injection", 1958, 1400, {"carb": 0, "cfm": 1300, "filter": 0.0}),
    (B_CARB, "Dual 4-barrel, 2x 660 cfm (center-squirter)", 1964, 720, {"carb": 1, "cfm": 1320, "filter": 0.02}),
    (F, "Factory spread-bore 4-barrel, 750 cfm", 1966, 220, {"carb": 1, "cfm": 750, "filter": 0.06}),
    (B_CARB, "4-barrel mechanical secondary, 600 cfm", 1968, 320, {"carb": 1, "cfm": 600, "filter": 0.03}),
    (B_CARB, "4-barrel double pumper, 750 cfm", 1970, 360, {"carb": 1, "cfm": 750, "filter": 0.03}),
    (B_CARB, "Dual 4-barrel, 2x 850 cfm (tunnel ram)", 1970, 860, {"carb": 1, "cfm": 1700, "filter": 0.01}),
    (B_CARB, "4-barrel vacuum secondary, 850 cfm", 1975, 380, {"carb": 1, "cfm": 850, "filter": 0.03}),
    (B_CARB, "Race 4-barrel big flange, 1150 cfm", 1985, 820, {"carb": 1, "cfm": 1150, "filter": 0.01}),
    (B_EFI, "Throttle-body EFI, 4-barrel flange, 750 cfm", 1990, 1300, {"carb": 0, "cfm": 750, "filter": 0.03}),
    (B_EFI, "Throttle-body EFI, 4-barrel flange, 1000 cfm", 1995, 1700, {"carb": 0, "cfm": 1000, "filter": 0.03})]:
    part("carb", G, b, n, y, pr, 0, d)
# exhaust systems
for (b, n, y, pr, m, pipe_in, duals, muf) in [
    (F, "Factory dual 2 in, reverse-flow mufflers", 1955, 220, 2, 2.0, 1, 0.55),
    (B_EXH, "Single 2-1/2 in glasspack", 1955, 160, -1, 2.5, 0, 0.35),
    (B_EXH, "Dual 2-1/4 in steel-pack straight-throughs", 1955, 260, -2, 2.25, 1, 0.3),
    (B_EXH, "Dual 2-1/2 in with cutouts (open for racing)", 1956, 360, 0, 2.5, 1, 0.1),
    (B_EXH, "Dual 2-1/4 in chambered mufflers", 1964, 330, 0, 2.25, 1, 0.38),
    (F, "Factory dual 2-1/2 in, low-restriction mufflers", 1965, 320, 2, 2.5, 1, 0.42),
    (F, "Factory single 2-1/2 in with catalytic converter", 1975, 300, 3, 2.5, 0, 0.7),
    (B_EXH, "Open dual 3-1/2 in race pipes", 1975, 300, -12, 3.5, 1, 0.0),
    (B_EXH, "Dual 2-1/2 in turbo-style mufflers, H-pipe", 1978, 480, 0, 2.5, 1, 0.33),
    (F, "Factory dual 2-1/2 in, high-flow catalysts", 1985, 600, 4, 2.5, 1, 0.45),
    (B_EXH, "Dual 3 in chambered, X-pipe", 1995, 720, -3, 3.0, 1, 0.24),
    (B_EXH, "Dual 3 in stainless, high-flow catalysts", 1997, 1100, 0, 3.0, 1, 0.32)]:
    part("exhaust", G, b, n, y, pr, m, {"pipe": vmm(pipe_in), "duals": duals, "muffler": muf})
# ignition (aftermarket systems bring a performance advance curve, as in v1)
for (b, n, y, pr, ign) in [
    (B_IGN, "High-output coil + points", 1955, 70, 0),
    (F, "Factory dual-point distributor", 1957, 150, 0),
    (B_IGN, "Dual-point race distributor", 1958, 220, 0),
    (B_IGN, "Transistorized points ignition", 1963, 260, 1),
    (B_IGN, "Magnetic-pickup race distributor", 1968, 340, 1),
    (F, "Factory high-energy distributor (coil in cap)", 1975, 200, 1),
    (B_IGN, "Crank-trigger CD race ignition", 1980, 780, 2),
    (B_IGN, "Electronic distributor + high-output coil (street)", 1985, 280, 1),
    (B_IGN, "Programmable CD ignition with rev limiter", 1992, 620, 2)]:
    part("ignition", G, b, n, y, pr, 0, {"ign": ign, "tune": 1} if b != F else {"ign": ign})
# nitrous oxide: wet kits, n2o = crank kW added (hp x 0.7457), n2orpm = arming rpm. Generic: the plate sits under any
# 4-barrel or throttle body; foggers are plumbed into the runners.
def hpkw(hp): return round(hp * 0.7457, 1)
for (b, n, y, pr, m, hp, arm) in [
    (B_N2O, "Competition nitrous plate kit (drag only), 100 hp", 1974, 650, 9, 100, 3500),
    (B_N2O, "Wet nitrous plate kit, 50 hp", 1978, 450, 9, 50, 2500),
    (B_N2O, "Wet nitrous plate kit, 75 hp", 1978, 460, 9, 75, 2500),
    (B_N2O, "Wet nitrous plate kit, 100 hp", 1978, 480, 9, 100, 3000),
    (B_N2O, "Wet nitrous plate kit, 150 hp", 1978, 520, 9, 150, 3000),
    (B_N2O, "Wet nitrous plate kit, 200 hp", 1980, 580, 10, 200, 3500),
    (B_N2O, "Wet nitrous plate kit, 250 hp", 1982, 640, 10, 250, 3500),
    (B_N2O, "Single-nozzle wet kit, 50 hp", 1985, 400, 8, 50, 2500),
    (B_N2O, "Single-nozzle wet kit, 75 hp", 1985, 410, 8, 75, 2500),
    (B_N2O, "Single-nozzle wet kit, 100 hp", 1985, 430, 8, 100, 3000),
    (B_N2O, "Direct-port fogger kit, 150 hp", 1986, 1100, 12, 150, 3000),
    (B_N2O, "Direct-port fogger kit, 200 hp", 1986, 1200, 12, 200, 3500),
    (B_N2O, "Direct-port fogger kit, 250 hp", 1986, 1300, 12, 250, 3500),
    (B_N2O, "Single-nozzle wet kit, 150 hp", 1988, 480, 8, 150, 3000)]:
    part("nitrous", G, b, n, y, pr, m, {"n2o": hpkw(hp), "n2orpm": arm})

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
#   blower, nitrous, so piston cr+= adds to the head's ratio. Piston parts are relative (bore+=, cr+=) so they fit every family.
#   A cam always sets its lifter type. Valve springs set float (valve float rpm) directly.
#
# Part ids: SSFFNN. SS = slot (11 heads, 12 cam, 13 valvetrain, 14 pistons, 15 rotating, 16 intake, 17 carb,
#   18 headers, 19 exhaust, 20 ignition, 21 blower, 22 nitrous), FF = family number below (00 = generic, fits every
#   family; FF + 50 = spare bucket once a bucket's 99 are used), NN = sequence.
#   Ids are saved in player profiles: never renumber or reuse one, only add.
#
# Forced induction (blower slot): blower 0 none, 1 Roots, 2 twin-screw, 3 centrifugal, 4 turbo; boost = psi the kit
#   is set up for; boostrpm = rpm where a centrifugal reaches it / a turbo's full-boost rpm (0 for Roots / screw);
#   beff = adiabatic efficiency; ic = intercooler effectiveness 0..0.8.
# Nitrous slot: n2o = crank kW the wet kit adds (shot hp x 0.7457), n2orpm = arming rpm.
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
# Id stability: every part already in the output file must come out with the same id and the same row (slot,
# families, brand, name, year, price, mass, mods). Player builds are saved by PartId. Refuse to write otherwise.
import os
def p_rows(text):
    return {r.split("\t")[1]: r for r in text.split("\n") if r.startswith("P\t")}
if os.path.exists(sys.argv[1]):
    old_rows, new_rows = p_rows(open(sys.argv[1], encoding="utf-8").read()), p_rows("\n".join(lines))
    changed = [i for i in old_rows if new_rows.get(i) != old_rows[i]]
    for i in changed[:20]:
        print("id check: part", i, "was:", old_rows[i].split("\t")[2:], file=sys.stderr)
        print("            now:", new_rows[i].split("\t")[2:] if i in new_rows else "(missing)", file=sys.stderr)
    if changed:
        raise SystemExit(f"id check FAILED: {len(changed)} existing part ids changed or vanished; not writing")
    print(f"id check: all {len(old_rows)} existing part ids unchanged, {len(new_rows) - len(old_rows)} added")
open(sys.argv[1], "w", encoding="utf-8").write("\n".join(lines) + "\n")
print(len(P), "parts", len(E), "engines", len(FAM), "families; largest id bucket", max(seq.values()), "of 99")
