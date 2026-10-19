"""donor.py - rank FM4 cars as the donor (physics baseline, textures, cockpit, brakes, audio) for an FM2 car.

Score: model-name similarity (letters only, MediaName segment 2) x50, same make (ManufacturerCode) +30,
same FM4 class +10, minus 0.5 per year of difference and 0.02 per point of performance index difference.
Tiers: model (same make, similarity >= 0.85), make (same make), class (same class), none.
"""
import difflib, re, sqlite3


def _letters(s):
    return re.sub(r'[^a-z]', '', s.lower())


def model_of(media):
    """LOT_Elise135R_03 -> 'elise'."""
    parts = media.split('_')
    return _letters(parts[1]) if len(parts) > 1 else ''


def make_code(db, make_id):
    r = db.execute('select ManufacturerCode from List_CarMake where ID=?', (make_id,)).fetchone()
    return r[0] if r else None


def fm4_class(db, pi):
    r = db.execute('select Id from CarClasses where MaxPerformanceIndex>=? order by Id limit 1', (pi,)).fetchone()
    return r[0] if r else None


def rank(src, fm4_db, top=8):
    """src: dict(MediaName, MakeCode, Year, PI, Class (FM4 class id), Asp).
    Returns the best `top` FM4 candidates, best first."""
    D = sqlite3.connect('file:%s?mode=ro' % fm4_db, uri=True)
    try:
        s_model = model_of(src['MediaName'])
        out = []
        for cid, media, mk, year, cls, pi, asp in D.execute(
                'select Id, MediaName, MakeID, Year, ClassID, PerformanceIndex, AspirationTypeId from Data_Car'):
            if not media:
                continue
            code = make_code(D, mk)
            same_make = bool(code) and code.upper() == (src['MakeCode'] or '').upper()
            mdl = model_of(media)
            ratio = difflib.SequenceMatcher(None, s_model, mdl).ratio() if s_model and mdl else 0.0
            same_class = cls == src['Class']
            score = 50 * ratio + (30 if same_make else 0) + (10 if same_class else 0)
            score -= 0.5 * abs((year or 0) - (src['Year'] or 0)) + 0.02 * abs((pi or 0) - (src['PI'] or 0))
            if ratio >= 0.85 and same_make:
                tier = 'model'
            elif same_make:
                tier = 'make'
            elif same_class:
                tier = 'class'
            else:
                tier = 'none'
            reasons = ['model similarity %.2f' % ratio, 'same make' if same_make else 'different make',
                       'same class' if same_class else 'different class',
                       'year %s (FM2 %s)' % (year, src['Year'])]
            if asp is not None and src.get('Asp') is not None and asp != src['Asp']:
                reasons.append('aspiration id differs (FM4 %s, FM2 %s; enum not verified)' % (asp, src['Asp']))
            out.append(dict(score=round(score, 2), tier=tier, Id=cid, MediaName=media, Year=year, PI=pi,
                            Asp=asp, reasons=reasons))
    finally:
        D.close()
    out.sort(key=lambda r: -r['score'])
    return out[:top]
