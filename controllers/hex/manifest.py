#!/usr/bin/env python3
# Writes controllers/hex/controller.json from controllers/grid/controller.json: the Grid's settings,
# in the same order (hex.c is grid.c, which reads them by place), with the Hex keyboard's words, then
# its own settings. Run it again after the Grid's settings change:  python3 controllers/hex/manifest.py
# (engine/tests/test_rail.cpp checks the two agree.)
import json, os, collections
root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
g = json.load(open(root + '/controllers/grid/controller.json'), object_pairs_hook=collections.OrderedDict)
m = collections.OrderedDict()
m['format'] = 1
m['id'] = 'patrickreynolds/hex'
m['version'] = '1.1.0'
m['name'] = 'Hex'
m['description'] = ("Hexagonal keys in an isomorphic layout, as on a Lumatone: every chord and scale has the same shape "
                    "in every key. Wicki-Hayden, Harmonic table or Bosanquet, in 12, 19, 22, 31, 41 or 53 equal steps or "
                    "just intonation, or a Scala file's tuning. Each key plays as the Grid's squares do: dynamics and tone inside it, slide out to "
                    "bend, wiggle for vibrato; press harder for pressure")
for k in ('author', 'licence'):
    m[k] = g[k]
m['needs'] = collections.OrderedDict([('fingers', 10), ('touchSize', False)])
for k in ('orientation', 'parts', 'partsFrom', 'arp'):
    m[k] = g[k]

words = {
    'cols': dict(name='Keys top to bottom', min=3, max=16, default=5),
    'layout': dict(hidden=True),
    'scrollLock': dict(hidden=True),
    'split': dict(name='Second keyboard on the right'),
    'splitSize': dict(name="Right keyboard's share (%)"),
    'topWay': dict(name='Right keyboard: rows'),
    'topNotes': dict(hidden=True),
    'topScale': dict(name='Right keyboard: scale'),
    'topKey': dict(name='Right keyboard: key'),
    'topOctave': dict(name="Right keyboard: octave (from left's)"),
    'squareSize': dict(name='Key size (%)'),
    'chromatic': dict(hidden=True, default=1),
    'insideDyn': dict(name='Up/down in a key: dynamics'),
    'dynLow': dict(name='Loudness at the bottom of a key (%)'),
    'dynHigh': dict(name='Loudness at the top of a key (%)'),
    'insideTone': dict(name='Left/right in a key: tone'),
    'insideOn': dict(name='Inside a key (dynamics, tone)'),
    'outOn': dict(name='Sliding out past a key'),
    'labels': dict(default=True),
    'colours': dict(options=['Plain', 'Every degree', 'Chord tones', 'Root, 4th, 5th', 'By note name',
                             'White keys', 'Sharps and flats', 'Rainbow', 'Distance from the key'], default=6),
    'sideLock': dict(hidden=True),
    'noteEdit': dict(hidden=True),
    'interval': dict(hidden=True),
}
settings = []
for s in g['settings']:
    s = collections.OrderedDict(s)
    w = words.get(s['id'], {})
    for k, v in w.items():
        s[k] = v
    if s.get('group') == 'Second grid':
        s['group'] = 'Second keyboard'
    settings.append(s)

def S(**kw):
    o = collections.OrderedDict()
    for k in ('id', 'name', 'type', 'options', 'min', 'max', 'step', 'default', 'hidden', 'group', 'showIf'):
        if k in kw:
            o[k] = kw[k]
    return o

settings += [
    S(id='hexLayout', name='Note layout', type='choice', options=['Wicki-Hayden', 'Harmonic table', 'Bosanquet', 'Your own steps'],
      default=0, group='Layout'),
    S(id='tuning', name='Tuning', type='choice',
      options=['12 equal', '19 equal', '22 equal', '31 equal', '41 equal', '53 equal', 'Just (5-limit)', 'Pythagorean',
               'Meantone (1/4 comma)', 'Scala file'], default=0, group='Notes'),
    S(id='stepRight', name='Steps to the next key in a row', type='number', min=-30, max=30, step=1, default=2, group='Layout',
      showIf={'hexLayout': 3}),
    S(id='stepUpRight', name='Steps up to the right', type='number', min=-30, max=30, step=1, default=7, group='Layout',
      showIf={'hexLayout': 3}),
    S(id='press', name='Press harder', type='choice', options=['Off', 'Pressure', 'Pressure and velocity'], default=1,
      group='Touch'),
    # A Scala (.scl) file's tuning: the MESH reads the file and sets these (mpe/scala.h); hex.c's SCALA_MAX.
    S(id='scala', name='Scala file', type='scala', max=128, default=0, group='Notes', showIf={'tuning': 9}),
]
settings += [S(id='scala%d' % k, type='number', min=-1000000, max=1000000, step=0, default=0, hidden=True)
             for k in range(1, 129)]
m['settings'] = settings
out = json.dumps(m, ensure_ascii=False, indent=2)
open(root + '/controllers/hex/controller.json', 'w').write(out + '\n')
print(len(settings), 'settings')
