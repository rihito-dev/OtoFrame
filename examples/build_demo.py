"""Embed a generated report in the offline demo. Python standard library only."""
import json
import base64
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
if len(sys.argv) != 2:
    raise SystemExit("Usage: python3 examples/build_demo.py report.json")
report = json.loads(Path(sys.argv[1]).read_text())
if report.get("schema") != 1 or report.get("synthetic") is not True:
    raise SystemExit("The bundled public demo must use a synthetic schema-1 report")
# Escape HTML delimiters so JSON cannot close the data script element.
data = json.dumps(report, separators=(",", ":"), allow_nan=False).replace("<", "\\u003c").replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")
template = (root / "demo/viewer.template.html").read_text()
for token, filename in [("__GEIST_FONT__", "Geist.woff2"), ("__GEIST_MONO_FONT__", "GeistMono.woff2")]:
    font = (root / "demo/fonts" / filename).read_bytes()
    if font[:4] != b"wOF2":
        raise SystemExit("Invalid bundled WOFF2 font: " + filename)
    template = template.replace(token, "data:font/woff2;base64," + base64.b64encode(font).decode("ascii"))
font_license = (root / "demo/fonts/OFL.txt").read_text()
if "-->" in font_license:
    raise SystemExit("Font license cannot contain an HTML comment terminator")
template = template.replace("</head>", "<!-- Bundled Geist fonts: SIL Open Font License 1.1\n" + font_license + "\n-->\n</head>")
(root / "demo/index.html").write_text(template.replace("__DEMO_JSON__", data))
print("Saved demo/index.html (self-contained; no server needed)")
