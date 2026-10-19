"""Add (or update) the Upgrades_Exhaust camera preset used by the upgrade shop's
exhaust page (fm4/src/brake_preview.cpp). Edits <game>/Media/UICarTargetCameraPresets.xml
in place and keeps a one-time backup next to it (.orig, shared with the brakes tool).

Usage: python add_upgrade_exhaust_camera.py <extracted game root>
Camera: rear 3/4 view like Upgrades_RearBumper (pos 3.069, -0.417, -3.601), but
closer and lower, aimed at the lower rear bumper where the tips are.
Coordinates are car-relative like the other presets (+X right side, +Z front;
in these presets a more negative Y is lower, as with Upgrades_Brakes).
First guess, like the brake preset was: tweak POS / TARGET / FOV and re-run.
"""
import math, os, shutil, sys

POS = (2.55, -0.55, -3.05)     # camera position (Upgrades_RearBumper: 3.069, -0.417, -3.601)
TARGET = (0.15, -0.10, 0.20)   # look point: low rear, slightly right of centre
FOV = 24.0

def preset_line():
    d = [t - p for t, p in zip(TARGET, POS)]
    n = math.sqrt(sum(c * c for c in d))
    f = [c / n for c in d]
    return ('  <Preset name="Upgrades_Exhaust" ordinal="-1" posx="%.6f" posy="%.6f" posz="%.6f" '
            'facex="%.6f" facey="%.6f" facez="%.6f" fov="%.6f" enableXAngleRestriction="0" '
            'minXAngle="0.000000" maxXAngle="0.000000" enableYAngleRestriction="1" minYAngle="0.000000" '
            'maxYAngle="0.000000" allowZoom="0"/>' % (*POS, *f, FOV))

def main(root):
    path = os.path.join(root, 'Media', 'UICarTargetCameraPresets.xml')
    if not os.path.exists(path + '.orig'):
        shutil.copy2(path, path + '.orig')
    text = open(path, newline='').read()
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = [l for l in text.split(nl) if 'name="Upgrades_Exhaust"' not in l]
    at = next(i for i, l in enumerate(lines) if 'name="Upgrades_RearBumper"' in l) + 1
    lines.insert(at, preset_line())
    open(path, 'w', newline='').write(nl.join(lines))
    print('Upgrades_Exhaust preset written to', path)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else r'C:\Z-Temp\Forza-4-compiling-stuff\extracted')
