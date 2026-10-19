"""Add (or update) the Upgrades_Brakes camera preset used by the upgrade shop's
brake page (fm4/src/brake_preview.cpp). Edits <game>/Media/UICarTargetCameraPresets.xml
in place and keeps a one-time backup next to it (.orig).

Usage: python add_upgrade_brakes_camera.py <extracted game root>
Camera: straight side view like Upgrades_Main (same direction), but closer and
slightly lower, so both wheels and their brakes fill the car view. (v1, a
front-wheel close-up, came out offset in game.)
Coordinates are car-relative like the other presets (+X right side, +Z front).
Tweak POS / TARGET / FOV below and re-run to adjust.
"""
import math, os, shutil, sys

POS = (4.60, -0.45, -0.05)     # camera position (Upgrades_Main: 5.615, -0.417, -0.051)
TARGET = (0.00, -0.08, 0.08)   # look point; gives Upgrades_Main's view direction
FOV = 23.5

def preset_line():
    d = [t - p for t, p in zip(TARGET, POS)]
    n = math.sqrt(sum(c * c for c in d))
    f = [c / n for c in d]
    return ('  <Preset name="Upgrades_Brakes" ordinal="-1" posx="%.6f" posy="%.6f" posz="%.6f" '
            'facex="%.6f" facey="%.6f" facez="%.6f" fov="%.6f" enableXAngleRestriction="0" '
            'minXAngle="0.000000" maxXAngle="0.000000" enableYAngleRestriction="1" minYAngle="0.000000" '
            'maxYAngle="0.000000" allowZoom="0"/>' % (*POS, *f, FOV))

def main(root):
    path = os.path.join(root, 'Media', 'UICarTargetCameraPresets.xml')
    if not os.path.exists(path + '.orig'):
        shutil.copy2(path, path + '.orig')
    text = open(path, newline='').read()
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = [l for l in text.split(nl) if 'name="Upgrades_Brakes"' not in l]
    at = next(i for i, l in enumerate(lines) if 'name="Upgrades_WheelStyle"' in l) + 1
    lines.insert(at, preset_line())
    open(path, 'w', newline='').write(nl.join(lines))
    print('Upgrades_Brakes preset written to', path)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else r'C:\Z-Temp\Forza-4-compiling-stuff\extracted')
