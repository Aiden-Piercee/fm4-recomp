"""Add (or update) the BuyCarSel camera preset used by the Buy Cars list on the stage
(fm4/src/buy_stage.cpp). Edits <game>/Media/UICarTargetCameraPresets.xml in place and keeps a
one-time backup next to it (.orig).

Usage: python add_buycar_camera.py <extracted game root>
Camera: front-right three-quarter view of the stage car, framed for the wide picture box
(about 2.7:1). Coordinates are car-relative like the other presets (+X right side, +Z front,
-Z rear; Y up, car origin about half-way up the body). Tweak POS / TARGET / FOV below and re-run:
  - car too small / too big -> move POS closer / further (or change FOV; it is the vertical angle)
  - view from the other side -> negate POS x (front-left)
  - car too high / low in the box -> raise / lower TARGET y
"""
import math, os, shutil, sys

POS = (3.50, 0.35, 3.90)       # camera position: right of the car, in front of it, about 0.9 m above ground
TARGET = (0.00, -0.15, 0.25)   # look point: near the middle of the car, slightly low
FOV = 28.0

def preset_line():
    d = [t - p for t, p in zip(TARGET, POS)]
    n = math.sqrt(sum(c * c for c in d))
    f = [c / n for c in d]
    return ('  <Preset name="BuyCarSel" ordinal="-1" posx="%.6f" posy="%.6f" posz="%.6f" '
            'facex="%.6f" facey="%.6f" facez="%.6f" fov="%.6f" enableXAngleRestriction="0" '
            'minXAngle="0.000000" maxXAngle="0.000000" enableYAngleRestriction="1" minYAngle="0.000000" '
            'maxYAngle="0.000000" allowZoom="0"/>' % (*POS, *f, FOV))

def main(root):
    path = os.path.join(root, 'Media', 'UICarTargetCameraPresets.xml')
    if not os.path.exists(path + '.orig'):
        shutil.copy2(path, path + '.orig')
    text = open(path, newline='').read()
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = [l for l in text.split(nl) if 'name="BuyCarSel"' not in l]
    at = next(i for i, l in enumerate(lines) if 'name="CarColorSel"' in l) + 1
    lines.insert(at, preset_line())
    open(path, 'w', newline='').write(nl.join(lines))
    print('BuyCarSel preset written to', path)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else r'C:\Z-Temp\Forza-4-compiling-stuff\extracted')
