import re
import sys
from pathlib import Path

if sys.version_info >= (3, 7):
    sys.stdout.reconfigure(encoding='utf-8')

if len(sys.argv) < 2:
    raise SystemExit("Uso: python search_wiki.py <ruta_html>")

html_path = Path(sys.argv[1])

with open(html_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Let's search for "no output after" or "Why is there no output"
matches = list(re.finditer(r"Why is there no output|RGB screen issue", content, re.IGNORECASE))
print(f"Total matches found: {len(matches)}")
for idx, m in enumerate(matches):
    start = max(0, m.start() - 100)
    end = min(len(content), m.end() + 1000)
    snippet = content[start:end]
    clean_snippet = re.sub(r'<[^>]+>', ' ', snippet)
    clean_snippet = re.sub(r'\s+', ' ', clean_snippet).strip()
    print(f"\n--- Match {idx} (Pos {m.start()}) ---")
    print(clean_snippet[:500])
