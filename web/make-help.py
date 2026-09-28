#!/usr/bin/env python3
"""Writes the in-page game guide (dist/help.html) for the web build.

The game content comes from the desktop key guides in
~/Desktop/Games/Roguelikes/Docs (build-docs.py + guides.py), so both guides
stay in sync; only the saving and "playing in the browser" parts are
written here, because they differ on the web."""
import html, importlib.util, os, sys

DOCS = os.path.expanduser('~/Desktop/Games/Roguelikes/Docs')
PAGE = 'zapm.html'

sys.path.insert(0, DOCS)
spec = importlib.util.spec_from_file_location('build_docs', os.path.join(DOCS, 'build-docs.py'))
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)
from guides import GUIDES   # noqa: E402

game = next(g for g in docs.GAMES if g['file'] == PAGE)
guide = dict(GUIDES[PAGE])
info = dict(game['info'])
kbd = docs.kbd
esc = html.escape

SAVING = '''<ul>
<li><strong>Saving is automatic.</strong> The game is stored in this browser (IndexedDB) every two minutes while it waits for your next command, and whenever you switch to another tab or window. Reloading the page continues from there.</li>
<li><kbd>S</kbd> (answer <kbd>y</kbd>) saves and ends the session, as in the original; reload the page (or press <em>Play again</em>) to continue.</li>
<li>When your character dies or you quit with <kbd>Ctrl+Q</kbd>, the save is deleted: death is final.</li>
<li>Each browser keeps <strong>one game</strong>. <em>New game</em> deletes it and starts over.</li>
<li><em>Export save</em> downloads the save file; <em>Import save</em> loads one (browser saves only; the Mac version's saves are not compatible).</li>
<li>Window layout and zoom are stored in the same browser storage.</li>
<li>Private/incognito windows and "clear site data" delete the stored game. Export first if it matters.</li>
</ul>'''

WEB = '''<ul>
<li><strong>Windows:</strong> the map top left, Messages (with history) under it, Status and Inventory on the right. Help, lists and menus pop up over the map. Text only, in the game's own colours.</li>
<li><strong>Resize windows</strong> by dragging the gaps between them; a window's contents shrink to fit when it is too small. <em>Reset windows</em> puts everything back.</li>
<li><strong>Zoom:</strong> <em>A−</em> / <em>A+</em> on the Map title bar (shown on hover) change the size of the map; when it is bigger than its window it scrolls to follow you. Hover over a text window's title to show its <em>A−</em> / <em>A+</em> buttons.</li>
<li><strong>No sound:</strong> ZAPM never had sound effects.</li>
<li><strong>Keys:</strong> the arrow keys, the digits or the numeric keypad move you (vi keys can be switched on with <kbd>O</kbd>).</li>
<li>Browsers keep a few shortcuts for themselves (<kbd>Ctrl+W</kbd>, <kbd>Ctrl+T</kbd>, <kbd>Ctrl+N</kbd>, and <kbd>Cmd</kbd> shortcuts on a Mac), so those never reach the game. Use <kbd>N</kbd> to name items instead of <kbd>Ctrl+N</kbd>.</li>
<li>If the game ever crashes, a message appears at the top; reload the page to continue from the last autosave.</li>
</ul>'''

KEY_HINTS = [
    ('?', 'In-game help'),
    ('X', 'Auto-explore: walk to the nearest unexplored spot'),
    ('Enter', 'Menu of all commands'),
    ('i', 'Inventory with a cursor: letter = main action, Enter = all actions'),
    ('<', 'Go up (walks to the nearest known staircase)'),
    ('>', 'Go down (walks to the nearest known staircase)'),
    ('s', 'Search: dead ends often hide secret doors'),
]


def dl(items):
    return '<dl>' + ''.join(f'<dt>{kbd(k)}</dt><dd>{esc(d)}</dd>' for k, d in items) + '</dl>'


def section(anchor, title, body):
    return f'<h2 id="h-{anchor}">{esc(title)}</h2>{body}'


parts = []
toc = [('about', 'About the game'), ('keys', 'Keyboard controls'), ('saving', 'Saving your game'),
       ('tips', 'Tips'), ('guide', "New player's guide"), ('web', 'Playing in the browser')]
parts.append('<p>' + esc(game['tagline']) + '</p>' + info['About the game'] + '<ul class="toc">' +
             ''.join(f'<li><a href="#h-{a}">{esc(t)}</a></li>' for a, t in toc) + '</ul>')

parts.append(section('about', 'About the game',
                     guide.pop('How ZAPM differs from Angband')))

ess = ''.join(f'<div class="box"><h3>{esc(cat)}</h3>{dl(items)}</div>' for cat, items in game['essentials'])
all_keys = game['all']() if callable(game['all']) else game['all']
full = ''.join(f'<div>{kbd(k)}<span>{esc(d)}</span></div>' for k, d in all_keys)
parts.append(section('keys', 'Keyboard controls',
                     '<div class="box key"><h3>The keys to remember</h3>' + dl(KEY_HINTS) + '</div>'
                     '<h3>Essential keys</h3><div class="grid">' + ess + '</div>'
                     '<details><summary>Complete key list (' + str(len(all_keys)) + ' commands)</summary>'
                     '<div class="all">' + full + '</div></details>'))

parts.append(section('saving', 'Saving your game', SAVING))
parts.append(section('tips', 'Tips', info['Tips']))
parts.append(section('guide', "New player's guide",
                     ''.join(f'<h3>{esc(t)}</h3>{b}' for t, b in guide.items())))
parts.append(section('web', 'Playing in the browser', WEB))

# RVIP: About this version (rogue2wasm.md: Source and changes)
parts.append('<h2 id="h-version">About this version</h2><ul>'
             '<li>Based on <strong>ZAPM 0.8.3</strong> (Cyrus Dolph), winny- fork.</li>'
             '<li>Original source: <a href="https://github.com/winny-/ZAPM-winny/tree/0fd7af89931b044ad3e27ac608fbd5467b97cffb" target="_blank" rel="noopener">winny-/ZAPM-winny, commit 0fd7af8</a></li>'
             '<li>Our changes (port, auto-explore, command menu, inventory menus, web build): '
             '<a href="https://github.com/memmaker/zapm" target="_blank" rel="noopener">memmaker/zapm</a></li></ul>')
print('\n'.join(parts))
