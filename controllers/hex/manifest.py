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
m['version'] = '2.2.0'
m['name'] = 'Hex'
m['description'] = ("Hexagonal keys in an isomorphic layout, as on a Lumatone: every chord and scale has the same shape "
                    "in every key. Wicki-Hayden, Harmonic table or Bosanquet, in 12, 19, 22, 31, 41 or 53 equal steps or "
                    "just intonation, or a Scala file's tuning. Dynamics and tone inside a key; two fingers in a key, slid out of it, "
                    "bend the note toward the keys either way (snapped to points if you like); press harder for pressure. "
                    "Move the keyboard with two fingers: drag it, size it, turn it")
for k in ('author', 'licence'):
    m[k] = g[k]
m['needs'] = collections.OrderedDict([('fingers', 10), ('touchSize', False)])
for k in ('orientation', 'parts', 'partsFrom'):
    m[k] = g[k]
# Two fingers slid out of a key bend it along its row; the rows are turned on the screen by viewTurn's
# degrees (the keyboard moved: hex.c's view), so a take finds a key's row that way (take_score.cpp).
m['bend'] = 'two fingers out'
m['rowTurn'] = 'viewTurn'
m['arp'] = g['arp']

# Keys top to bottom and Key size are hidden: moving the keyboard (its view, below) sizes it now. They
# still set the keys' first size, so a preset from before plays as it did.
words = {
    'cols': dict(name='Keys top to bottom', min=3, max=16, default=5, hidden=True),
    'layout': dict(hidden=True),
    'scrollLock': dict(hidden=True),
    'split': dict(name='Second keyboard on the right'),
    'splitSize': dict(name="Right keyboard's share (%)"),
    'topWay': dict(name='Right keyboard: rows'),
    'topNotes': dict(hidden=True),
    'topScale': dict(name='Right keyboard: scale'),
    'topKey': dict(name='Right keyboard: key'),
    'topOctave': dict(name="Right keyboard: octave (from left's)"),
    'third': dict(name='Third keyboard, right of the second'),
    'thirdOctave': dict(name="Third keyboard: octave (from left's)"),
    'squareSize': dict(name='Key size (%)', hidden=True),
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
    # Its touch past the inside of a key is its own (two fingers to slide): the Grid's ways out of a
    # square, vibrato and Slide between notes are not used.
    'xTarget': dict(hidden=True), 'yTarget': dict(hidden=True), 'bendRange': dict(hidden=True),
    'vibrato': dict(hidden=True, default=0), 'slide': dict(hidden=True, default=False),
    'slideGlide': dict(hidden=True), 'glideLR': dict(hidden=True), 'glideUD': dict(hidden=True),
    'slideOnly': dict(hidden=True, default=False), 'sideSlide': dict(hidden=True, default=0),
    'outOn': dict(hidden=True), 'sideOut': dict(hidden=True, default=0),
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
# Two fingers to slide: snapped to so many points between one key's note and the next's (hex.c S_SNAP).
settings += [
    S(id='slideSnap', name='Snap the slide to points', type='switch', default=False, group='Touch'),
    S(id='snapPoints', name='Points between keys', type='number', min=1, max=24, step=1, default=4, group='Touch',
      showIf={'slideSnap': True}),
]
# Moving the keyboard (hex.c's view): the mode and putting it back on the rail; where it is, hidden
# (kept in presets and takes); its button beside the keys, and where that button is moved to.
settings += [
    S(id='moveKeys', name='Move the keyboard (two fingers)', type='switch', default=False, group='Layout'),
    S(id='viewReset', name='Put the keyboard back', type='switch', default=False, group='Layout'),
    S(id='viewX', name='Keyboard moved across (keys)', type='number', min=-150, max=150, step=0, default=0, hidden=True, group='Layout'),
    S(id='viewY', name='Keyboard moved up (keys)', type='number', min=-150, max=150, step=0, default=0, hidden=True, group='Layout'),
    S(id='viewScale', name='Keyboard size', type='number', min=0.35, max=3, step=0, default=1, hidden=True, group='Layout'),
    S(id='viewTurn', name='Keyboard turned (degrees)', type='number', min=0, max=360, step=0, default=0, hidden=True, group='Layout'),
    S(id='sideMove', name='Move button', type='choice', options=['Off', 'Top', 'Bottom'], default=1, group='Controls'),
    S(id='place9', name='Moved control 10', type='number', min=0, max=9007199254740991, step=1, default=0, hidden=True, group='Controls'),
]
m['settings'] = settings
out = json.dumps(m, ensure_ascii=False, indent=2)
open(root + '/controllers/hex/controller.json', 'w').write(out + '\n')
print(len(settings), 'settings')
