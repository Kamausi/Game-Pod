#!/usr/bin/env python3
"""Build the full-length book library from Standard Ebooks sources.

Standard Ebooks (https://standardebooks.org) publishes carefully proofread,
public-domain (in the US) books, with their own work dedicated to the public
domain under CC0. Each book lives in a GitHub repo as XHTML chapters.

For every entry in scripts/books.json this script:
  1. downloads content.opf, toc.xhtml and each chapter (cached in scripts/.cache),
  2. converts chapters into compact text blocks,
  3. writes books/<slug>.json.gz and the catalog js/books-catalog.js.

Usage: python3 scripts/build_books.py [slug ...]
"""
import gzip
import html
import json
import math
import os
import re
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from html.parser import HTMLParser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "scripts", ".cache")
OUT = os.path.join(ROOT, "books")
RAW = "https://raw.githubusercontent.com/standardebooks/{slug}/master/src/epub/{path}"

# Spine items that are publishing furniture rather than the book itself.
SKIP_FILES = {"titlepage", "imprint", "halftitlepage", "colophon", "uncopyright", "endnotes", "toc", "loi", "dedication-sei"}
WORDS_PER_MINUTE = 238
MIN_CHAPTER_WORDS = 15
MERGE_AVG_WORDS = 600  # runs of sibling sections shorter than this become one chapter
SPLIT_WORDS = 15000  # chapters longer than this are split into parts...
PART_WORDS = 6000  # ...of roughly this size


def fetch(slug, path):
    local = os.path.join(CACHE, slug, path)
    if os.path.exists(local):
        with open(local, encoding="utf-8") as f:
            return f.read()
    url = RAW.format(slug=slug, path=path)
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                text = r.read().decode("utf-8")
            break
        except Exception:
            if attempt == 3:
                raise
    os.makedirs(os.path.dirname(local), exist_ok=True)
    with open(local, "w", encoding="utf-8") as f:
        f.write(text)
    return text


def clean(text):
    return re.sub(r"[ \t\r\n]+", " ", text).strip()


# ---------- content.opf ----------

def first_paragraph(escaped, limit=600):
    """The OPF description is escaped HTML; keep its first paragraph as plain text."""
    if not escaped:
        return None
    para = html.unescape(escaped).split("</p>")[0]
    text = clean(re.sub(r"<[^>]+>", "", para))
    if len(text) > limit:
        text = text[:limit].rsplit(". ", 1)[0] + "."
    return text


def parse_opf(opf):
    def tag(name, attr=""):
        m = re.search(rf"<{name}[^>]*{attr}[^>]*>(.*?)</{name}>", opf, re.S)
        return clean(re.sub(r"<[^>]+>", "", m.group(1))) if m else None

    creators = [clean(c) for c in re.findall(r'<dc:creator id="author[^"]*">(.*?)</dc:creator>', opf, re.S)]
    translators = []
    for cid, name in re.findall(r'<dc:contributor id="([^"]+)">(.*?)</dc:contributor>', opf, re.S):
        if re.search(rf'refines="#{re.escape(cid)}"[^>]*>trl<', opf):
            translators.append(clean(name))
    manifest = dict(re.findall(r'<item[^>]*?href="([^"]+)"[^>]*?id="([^"]+)"', opf))
    manifest = {v: k for k, v in manifest.items()}
    spine = re.findall(r'<itemref idref="([^"]+)"', opf)
    return {
        "title": tag("dc:title", 'id="title"'),
        "authors": creators,
        "translators": translators,
        "description": first_paragraph(tag("dc:description")),
        "url": (tag("dc:identifier") or "").removeprefix("url:"),
        "files": [manifest[i] for i in spine if i in manifest],
    }


# ---------- toc.xhtml ----------

class TocParser(HTMLParser):
    """Collects (href, label path) for every link in the toc nav."""

    def __init__(self):
        super().__init__()
        self.in_toc = False
        self.stack = []  # labels of open <li>s
        self.entries = []
        self.href = None
        self.text = ""

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "nav" and "toc" in (a.get("epub:type") or ""):
            self.in_toc = True
        if not self.in_toc:
            return
        if tag == "li":
            self.stack.append(None)
        elif tag == "a":
            self.href, self.text = a.get("href", ""), ""

    def handle_endtag(self, tag):
        if not self.in_toc:
            return
        if tag == "nav":
            self.in_toc = False
        elif tag == "li" and self.stack:
            self.stack.pop()
        elif tag == "a" and self.href is not None:
            label = clean(self.text)
            if self.stack:
                self.stack[-1] = (self.href.split("#")[0], label)
            parents = [s for s in self.stack[:-1] if s]
            self.entries.append((self.href, parents, label))
            self.href = None

    def handle_data(self, data):
        if self.href is not None:
            self.text += data


def chapter_titles(toc, book_title):
    """Returns {href: title}, where href is "file" or "file#fragment"."""
    p = TocParser()
    p.feed(toc)
    titles = {}
    for href, parents, label in p.entries:
        if href in titles:
            continue
        # Prefix with ancestors (e.g. "Book One · Chapter I"), minus the book's own title.
        prefix = [lbl for h, lbl in parents if h != href and lbl != book_title]
        titles[href] = " · ".join(prefix + [label])
        titles.setdefault(href.split("#")[0], titles[href])
    return titles


# ---------- chapter XHTML ----------

VERSE_TYPES = ("z3998:poem", "z3998:verse", "z3998:song", "z3998:hymn", "z3998:lyrics")


class ChapterParser(HTMLParser):
    """Turns a chapter into blocks:
    ["p", text]  paragraph       ["v", text]  verse (newlines kept)
    ["q", text]  quotation       ["h", text]  subheading
    ["d", speaker, text]  drama line          ["b"]  section break
    """

    def __init__(self, title, splits):
        super().__init__(convert_charrefs=True)
        self.splits = splits  # element id -> title of a chapter that starts there
        self.segments = [(title, [])]
        self.blocks = self.segments[0][1]
        self.stack = []  # (tag, epub_type)
        self.buf = None
        self.kind = None
        self.skip = 0  # depth inside noterefs, headers of the chapter itself, etc.
        self.seen_text = False
        self.row = None  # drama table row: list of [type, text]

    def _types(self):
        return " ".join(t for _, t in self.stack)

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        etype = a.get("epub:type") or ""
        self.stack.append((tag, etype))
        if self.skip:
            if tag not in ("br", "img", "hr"):
                self.skip += 1
            else:
                self.stack.pop()
            return
        if a.get("id") in self.splits:
            self._flush()
            self.segments.append((self.splits[a["id"]], []))
            self.blocks = self.segments[-1][1]
            self.seen_text = False
        if tag in ("br", "img", "hr"):
            self.stack.pop()
            if tag == "br" and self.row:
                self.row[-1][1] += "\n"
            elif tag == "br" and self.buf is not None:
                self.buf += "\n"
            elif tag == "hr":
                self._flush()
                self.blocks.append(["b"])
            return
        if "noteref" in etype or tag in ("figure", "script", "style"):
            self.skip = 1
            return
        # The chapter's own title block (shown by the app already).
        if not self.seen_text and (tag in ("header", "hgroup") or re.fullmatch(r"h[1-6]", tag)):
            self.skip = 1
            return
        if tag == "tr":
            self.row = []
        elif tag == "td" and self.row is not None:
            self.row.append(["persona" if "z3998:persona" in etype else "", ""])
        elif tag in ("p", "li") or re.fullmatch(r"h[1-6]", tag):
            self._flush()
            types = self._types()
            if re.fullmatch(r"h[1-6]", tag):
                self.kind = "h"
            elif any(t in types for t in VERSE_TYPES):
                self.kind = "v"
            elif any(t == "blockquote" for t, _ in self.stack[:-1]):
                self.kind = "q"
            else:
                self.kind = "p"
            self.buf = "• " if tag == "li" else ""

    def handle_endtag(self, tag):
        # Void elements (<br/>, <td/> ...) also arrive here; ignore unmatched ends.
        if not any(t == tag for t, _ in self.stack):
            return
        # Pop to the matching tag (tolerates sloppy nesting).
        while self.stack:
            t, _ = self.stack.pop()
            if self.skip:
                self.skip -= 1
            if t == tag:
                break
        if self.skip:
            return
        if tag in ("p", "li") or re.fullmatch(r"h[1-6]", tag):
            self._flush()
        elif tag == "tr" and self.row is not None:
            speaker = next((clean(x) for t, x in self.row if t == "persona"), "")
            text = "\n".join(lines(x) for t, x in self.row if t != "persona" and lines(x))
            if text:
                self.blocks.append(["d", speaker, text] if speaker else ["p", text])
                self.seen_text = True
            self.row = None

    def handle_data(self, data):
        if self.skip:
            return
        if self.row is not None and self.row:
            self.row[-1][1] += data
        elif self.buf is not None:
            self.buf += data

    def _flush(self):
        if self.buf is None:
            return
        text = lines(self.buf)
        if text and text != "•":
            self.blocks.append([self.kind, text])
            self.seen_text = True
        self.buf = None
        self.kind = None


def lines(text):
    """Collapse whitespace but keep explicit line breaks."""
    return "\n".join(l for l in (clean(x) for x in text.split("\n")) if l)


def count_words(blocks):
    return sum(len(b[-1].split()) for b in blocks if len(b) > 1)


# ---------- chapter post-processing ----------

def block_words(b):
    return len(b[-1].split()) if len(b) > 1 else 0


def merge_fragments(chapters):
    """Join runs of short sibling sections (aphorisms, numbered paragraphs) into one chapter."""
    out, i = [], 0
    while i < len(chapters):
        prefix = chapters[i]["t"].rpartition(" · ")[0]
        j = i
        while prefix and j < len(chapters) and chapters[j]["t"].rpartition(" · ")[0] == prefix:
            j += 1
        run = chapters[i:j]
        if len(run) >= 3 and sum(c["w"] for c in run) / len(run) < MERGE_AVG_WORDS:
            blocks = []
            for c in run:
                blocks.append(["h", c["t"].rpartition(" · ")[2]])
                blocks.extend(c["b"])
            out.append({"t": prefix, "b": blocks, "w": sum(c["w"] for c in run)})
            i = j
        else:
            out.append(chapters[i])
            i += 1
    return out


def split_long(chapters, book_title):
    """Split very long chapters into parts, preferring section breaks and headings."""
    out = []
    for c in chapters:
        if c["w"] <= SPLIT_WORDS:
            out.append(c)
            continue
        n = math.ceil(c["w"] / PART_WORDS)
        target = c["w"] / n
        parts, cur, words = [], [], 0
        for b in c["b"]:
            at_break = b[0] in ("b", "h")
            if cur and (words >= target or (at_break and words >= 0.6 * target)):
                parts.append((cur, words))
                cur, words = [], 0
            if not (b[0] == "b" and not cur):  # don't start a part with a break
                cur.append(b)
            words += block_words(b)
        if parts and words < 0.3 * target:  # fold a small tail into the previous part
            prev, prev_words = parts.pop()
            cur, words = prev + cur, prev_words + words
        parts.append((cur, words))
        for k, (blocks, w) in enumerate(parts, 1):
            label = f"Part {k} of {len(parts)}"
            title = label if c["t"] == book_title else f'{c["t"]} · {label}'
            out.append({"t": title, "b": blocks, "w": w})
    return out


# ---------- build ----------

def build(entry):
    slug = entry["slug"]
    meta = parse_opf(fetch(slug, "content.opf"))
    titles = chapter_titles(fetch(slug, "toc.xhtml"), meta["title"])
    files = [f for f in meta["files"] if os.path.splitext(os.path.basename(f))[0] not in SKIP_FILES]

    with ThreadPoolExecutor(8) as pool:
        sources = list(pool.map(lambda f: fetch(slug, f), files))

    chapters = []
    for file, src in zip(files, sources):
        name = os.path.splitext(os.path.basename(file))[0]
        splits = {h.split("#")[1]: t for h, t in titles.items() if h.startswith(file + "#")}
        p = ChapterParser(titles.get(file) or name.replace("-", " ").title(), splits)
        p.feed(src)
        p._flush()
        for title, blocks in p.segments:
            words = count_words(blocks)
            if words >= MIN_CHAPTER_WORDS:
                chapters.append({"t": title, "b": blocks, "w": words})

    if not entry.get("keep_sections"):
        chapters = merge_fragments(chapters)
    chapters = split_long(chapters, meta["title"])
    words = sum(c["w"] for c in chapters)
    os.makedirs(OUT, exist_ok=True)
    payload = json.dumps({"id": slug, "chapters": chapters}, ensure_ascii=False, separators=(",", ":"))
    with gzip.open(os.path.join(OUT, f"{slug}.json.gz"), "wb", compresslevel=9) as f:
        f.write(payload.encode("utf-8"))

    return {
        "id": slug,
        "title": entry.get("title") or meta["title"],
        "author": " & ".join(meta["authors"]),
        "translator": ", ".join(meta["translators"]) or None,
        "year": entry.get("year"),
        "category": entry["category"],
        "blurb": meta["description"],
        "chapters": len(chapters),
        "words": words,
        "minutes": round(words / WORDS_PER_MINUTE),
        "source": meta["url"],
    }


def main():
    with open(os.path.join(ROOT, "scripts", "books.json"), encoding="utf-8") as f:
        config = json.load(f)
    only = set(sys.argv[1:])
    catalog_path = os.path.join(ROOT, "js", "books-catalog.js")
    existing = {}
    if only and os.path.exists(catalog_path):
        with open(catalog_path, encoding="utf-8") as f:
            data = json.loads(f.read().split("window.BookCatalog = ", 1)[1].rstrip().rstrip(";"))
            existing = {b["id"]: b for b in data["books"]}

    catalog = []
    for entry in config["books"]:
        if only and entry["slug"] not in only:
            if entry["slug"] in existing:
                catalog.append(existing[entry["slug"]])
            continue
        book = build(entry)
        catalog.append(book)
        print(f'{book["chapters"]:4d} ch {book["words"]:8d} w  {book["title"]} — {book["author"]}', flush=True)

    with open(catalog_path, "w", encoding="utf-8") as f:
        f.write("// Generated by scripts/build_books.py. Do not edit by hand.\n")
        f.write("window.BookCatalog = ")
        f.write("{\"categories\":" + json.dumps(config["categories"], ensure_ascii=False) + ",\n\"books\":[\n")
        f.write(",\n".join(json.dumps(b, ensure_ascii=False, separators=(",", ":")) for b in catalog))
        f.write("\n]};\n")


if __name__ == "__main__":
    main()
