const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const zlib = require("node:zlib");

const root = path.join(__dirname, "..");
const sandbox = { window: {} };
vm.runInNewContext(fs.readFileSync(path.join(root, "js/books-catalog.js"), "utf8"), sandbox);
const { categories, books } = sandbox.window.BookCatalog;
const KINDS = new Set(["p", "v", "q", "h", "d", "b"]);

test("catalog entries are complete and unique", () => {
  assert.ok(books.length > 50);
  const ids = new Set();
  for (const b of books) {
    assert.ok(!ids.has(b.id), `duplicate ${b.id}`);
    ids.add(b.id);
    assert.ok(b.title && b.author, `${b.id} needs title and author`);
    assert.ok(categories[b.category], `${b.id} has unknown category ${b.category}`);
    assert.match(b.source, /^https:\/\/standardebooks\.org\/ebooks\//);
    assert.ok(b.chapters > 0 && b.words > 1000, `${b.id} looks empty`);
  }
});

test("every book file matches its catalog entry", () => {
  for (const b of books) {
    const file = path.join(root, "books", `${b.id}.json.gz`);
    const data = JSON.parse(zlib.gunzipSync(fs.readFileSync(file)).toString("utf8"));
    assert.equal(data.id, b.id);
    assert.equal(data.chapters.length, b.chapters, `${b.id} chapter count`);
    let words = 0;
    for (const ch of data.chapters) {
      assert.ok(ch.t && ch.b.length, `${b.id} has an empty chapter`);
      for (const block of ch.b) assert.ok(KINDS.has(block[0]), `${b.id} has unknown block ${block[0]}`);
      words += ch.w;
    }
    assert.equal(words, b.words, `${b.id} word count`);
  }
});
