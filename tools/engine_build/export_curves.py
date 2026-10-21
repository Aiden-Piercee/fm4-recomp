#!/usr/bin/env python3
"""Writes the stock FM4 torque curve of every engine in the builder catalogue as CSV (EngineID,rpm,torque_nm), for
tools/engine_build/dyno_calibrate.cpp. Reads the user's own extracted gamedb.slt; the output is game data, so keep it
out of the repository.

usage: export_curves.py <gamedb.slt> <fm4_engine_parts.tsv> <out.csv>
"""
import csv
import sqlite3
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    db, tsv, out = sys.argv[1:]
    engines = sorted({int(line.split('\t')[1]) for line in open(tsv, encoding='utf-8') if line.startswith('E\t')})
    c = sqlite3.connect('file:%s?mode=ro' % db, uri=True)
    with open(out, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['EngineID', 'rpm', 'torque_nm'])
        for eid in engines:
            cam = c.execute('SELECT TorqueCurveMaxRPM, TorqueCurveFullThrottleID FROM List_UpgradeEngineCamshaft '
                            'WHERE EngineID=? AND IsStock=1', (eid,)).fetchone()
            if not cam:
                print('engine %d: no stock camshaft row' % eid)
                continue
            tc = c.execute('SELECT * FROM List_TorqueCurve WHERE TorqueCurveID=?', (cam[1],)).fetchone()
            if not tc:
                print('engine %d: no torque curve %d' % (eid, cam[1]))
                continue
            scale, n = tc[1], tc[2]
            for i, v in enumerate(tc[3:3 + n]):
                w.writerow([eid, round(i * cam[0] / (n - 1)), round(v * scale, 2)])
    print('wrote', out, 'for', len(engines), 'engines')


if __name__ == '__main__':
    main()
