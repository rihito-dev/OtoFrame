"""Embed a generated report in the offline demo. Python standard library only."""
import json
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
(root / "demo/index.html").write_text(template.replace("__DEMO_JSON__", data))
print("Saved demo/index.html (self-contained; no server needed)")
