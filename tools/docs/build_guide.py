"""Render current API references into the offline guide without dependencies."""
import argparse
from html import escape
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PAGES = {"19-lua-api.md": "lua.html", "20-components.md": "components.html", "27-game-ui.md": "game-ui.html"}


def inline(text):
    text = escape(text)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)

    def link(match):
        label, target = match.groups()
        if not target.startswith(("https://", "http://", "#")):
            target = PAGES.get(target, "../" + target)
        return f'<a href="{target}">{label}</a>'

    text = re.sub(r"!\[([^\]]+)\]\(([^)]+)\)",
                  lambda m: f'<img src="{m[2] if m[2].startswith(("https://", "http://")) else "../" + m[2]}" loading="lazy" alt="{m[1]}">', text)
    return re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, text)


def render(source):
    rows = source.splitlines()
    content, navigation = [], []
    index = 0
    while index < len(rows):
        line = rows[index]
        if line.startswith("```"):
            code = []
            index += 1
            while index < len(rows) and not rows[index].startswith("```"):
                code.append(rows[index]); index += 1
            content.append("<pre><code>" + escape("\n".join(code)) + "</code></pre>")
        elif line.startswith("#"):
            level = len(line) - len(line.lstrip("#"))
            title = line[level:].strip()
            anchor = f"section-{len(navigation)}"
            navigation.append(f'<a href="#{anchor}">{escape(title)}</a>')
            content.append(f'<h{level} id="{anchor}">{inline(title)}</h{level}>')
        elif line.startswith("|"):
            table = []
            while index < len(rows) and rows[index].startswith("|"):
                cells = rows[index].strip().strip("|").split("|")
                if not all(re.fullmatch(r"\s*:?-+:?\s*", cell) for cell in cells):
                    tag = "th" if not table else "td"
                    table.append("<tr>" + "".join(f"<{tag}>{inline(cell.strip())}</{tag}>" for cell in cells) + "</tr>")
                index += 1
            content.append('<div class="table-scroll"><table>' + "".join(table) + "</table></div>")
            continue
        elif re.match(r"^(?:- |\d+\. )", line):
            ordered = line[0].isdigit()
            tag = "ol" if ordered else "ul"
            items = []
            pattern = r"^\d+\. " if ordered else r"^- "
            while index < len(rows) and re.match(pattern, rows[index]):
                items.append("<li>" + inline(re.sub(pattern, "", rows[index])) + "</li>")
                index += 1
            content.append(f"<{tag}>" + "".join(items) + f"</{tag}>")
            continue
        elif line.strip():
            content.append("<p>" + inline(line) + "</p>")
        index += 1
    return "\n".join(content), "\n".join(navigation)


sample, navigation = render("# Example\n<script>\n\n```lua\na < b\n```\n\n| Key | Value |\n|---|---|\n| x | 1 |")
assert '<script>' not in sample and '&lt;script&gt;' in sample
assert '<pre><code>a &lt; b</code></pre>' in sample and '<td>x</td>' in sample
assert 'href="#section-0"' in navigation
assert inline('[Lua](19-lua-api.md)') == '<a href="lua.html">Lua</a>'
assert 'src="../guide/media/game-ui.png"' in inline('![UI](guide/media/game-ui.png)')
assert 'src="https://example.com/ui.png"' in inline('![UI](https://example.com/ui.png)')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--document", choices=PAGES.keys(), help="Render only the selected reference")
selected = parser.parse_args().document
for document, output in PAGES.items():
    if selected and document != selected:
        continue
    source = (ROOT / "docs" / document).read_text(encoding="utf-8")
    content, navigation = render(source)
    title = source.splitlines()[0].lstrip("# ")
    page = f'''<!doctype html>
<html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{escape(title)} · MyEngine</title><link rel="stylesheet" href="style.css"></head>
<body><a class="skip" href="#main">본문으로 이동</a><header><a class="brand" href="index.html">MyEngine</a><span>{escape(title)}</span><a href="index.html">제작 가이드</a></header>
<div class="layout"><aside aria-label="API 목차"><nav>{navigation}</nav></aside><main id="main">{content}
<footer>정본: <a href="../{document}">{document}</a> · 구현 범위와 실패 조건을 확인하세요.</footer></main></div></body></html>
'''
    (ROOT / "docs/guide" / output).write_text(page, encoding="utf-8", newline="\n")
    print(f"Rendered {document} -> docs/guide/{output}")
