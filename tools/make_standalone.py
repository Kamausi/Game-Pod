#!/usr/bin/env python3
"""Bundle Mud Buddies into ONE self-contained HTML file (WASM + JS + CSS + CAD parts inlined).

    python3 tools/make_standalone.py build-wasm/mudbuddies_single.js dist/mudbuddies.html

Open the result straight from disk (double-click) or host it anywhere. Solo play works offline;
multiplayer / global leaderboards still need `node server/server.js` (enter its ws:// URL in
Multiplayer)."""
import os
import re
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
game_js_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'build-wasm', 'mudbuddies_single.js')
out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, 'dist', 'mudbuddies.html')

def read(p):
    with open(p, encoding='utf-8') as f:
        return f.read()

def script_safe(js):
    # An inline <script> ends at the first "</script"; break any occurrence inside the code.
    return re.sub(r'</(script)', r'<\\/\1', js, flags=re.IGNORECASE)

html = read(os.path.join(root, 'web', 'index.html'))
css = read(os.path.join(root, 'web', 'style.css'))
app = read(os.path.join(root, 'web', 'app.js'))
game = read(game_js_path)

html = html.replace('<link rel="stylesheet" href="style.css">', '<style>\n' + css + '\n</style>')
html = html.replace('<script src="app.js"></script>', '<script>\n' + script_safe(app) + '\n</script>')
html = html.replace('<script async src="build/mudbuddies.js"></script>', '<script>\n' + script_safe(game) + '\n</script>')
assert 'src="app.js"' not in html and 'build/mudbuddies.js' not in html, 'placeholder not replaced'

os.makedirs(os.path.dirname(out_path), exist_ok=True)
with open(out_path, 'w', encoding='utf-8') as f:
    f.write(html)
print(f'wrote {out_path} ({os.path.getsize(out_path) / 1e6:.2f} MB)')
