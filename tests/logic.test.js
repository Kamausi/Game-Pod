const test = require("node:test");
const assert = require("node:assert/strict");
const L = require("../js/logic.js");
const { LESSONS, CHALLENGES, SHORTS, CATEGORIES } = require("../js/content.js");

test("streak grows on consecutive days and resets after a gap", () => {
  let s = L.defaultState();
  s = L.touchStreak(s, "2026-10-01");
  s = L.touchStreak(s, "2026-10-01");
  assert.equal(s.streak, 1);
  s = L.touchStreak(s, "2026-10-02");
  s = L.touchStreak(s, "2026-10-03");
  assert.equal(s.streak, 3);
  s = L.touchStreak(s, "2026-10-06");
  assert.equal(s.streak, 1);
  assert.equal(s.bestStreak, 3);
});

test("streak survives month boundaries", () => {
  let s = L.touchStreak(L.defaultState(), "2026-10-31");
  s = L.touchStreak(s, "2026-11-01");
  assert.equal(s.streak, 2);
});

test("currentStreak lapses once a day is missed", () => {
  const s = L.touchStreak(L.defaultState(), "2026-10-01");
  assert.equal(L.currentStreak(s, "2026-10-02"), 1);
  assert.equal(L.currentStreak(s, "2026-10-03"), 0);
});

test("levels follow triangular XP thresholds", () => {
  assert.deepEqual(L.levelFor(0), { level: 1, into: 0, needed: 100 });
  assert.equal(L.levelFor(99).level, 1);
  assert.equal(L.levelFor(100).level, 2);
  assert.equal(L.levelFor(299).level, 2);
  assert.equal(L.levelFor(300).level, 3);
});

test("completing a lesson awards XP once plus quiz bonuses", () => {
  let { state, gained } = L.completeLesson(L.defaultState(), "tiny-habits", 2, 2, "2026-10-01");
  assert.equal(gained, L.XP.lesson + 2 * L.XP.quizPerQuestion + L.XP.quizPerfect);
  assert.equal(state.streak, 1);
  ({ state, gained } = L.completeLesson(state, "tiny-habits", 1, 2, "2026-10-02"));
  assert.equal(gained, L.XP.quizPerQuestion);
  assert.equal(state.completed["tiny-habits"].quizScore, 2);
  assert.equal(state.completed["tiny-habits"].day, "2026-10-01");
});

test("spaced repetition intervals grow and reset on forget", () => {
  let c = L.newCard("2026-10-01");
  c = L.reviewCard(c, 2, "2026-10-01");
  assert.equal(c.interval, 1);
  assert.equal(c.due, "2026-10-02");
  c = L.reviewCard(c, 2, "2026-10-02");
  assert.equal(c.interval, 6);
  c = L.reviewCard(c, 2, "2026-10-08");
  assert.equal(c.interval, 15);
  c = L.reviewCard(c, 0, "2026-10-23");
  assert.equal(c.interval, 1);
  assert.equal(c.reps, 0);
  assert.ok(c.ease >= 1.3);
});

test("dueCards returns only cards due today or earlier", () => {
  const s = {
    ...L.defaultState(),
    highlights: [{ id: "a" }, { id: "b" }],
    cards: { a: { due: "2026-10-01" }, b: { due: "2026-10-05" } },
  };
  assert.deepEqual(L.dueCards(s, "2026-10-02").map((h) => h.id), ["a"]);
});

test("challenges unlock one lesson per day", () => {
  const c = { lessons: ["a", "b", "c"] };
  assert.equal(L.challengeProgress(c, undefined, "2026-10-01").started, false);
  const entry = { startDay: "2026-10-01", done: ["a"] };
  assert.equal(L.challengeProgress(c, entry, "2026-10-01").unlocked, 1);
  assert.equal(L.challengeProgress(c, entry, "2026-10-10").unlocked, 3);
  assert.equal(L.challengeProgress(c, { ...entry, done: ["a", "b", "c"] }, "2026-10-03").finished, true);
});

test("badges are awarded once", () => {
  const { state } = L.completeLesson(L.defaultState(), "tiny-habits", 0, 2, "2026-10-01");
  const first = L.awardBadges(state, { perfectQuizzes: 0, finishedChallenges: 0 });
  assert.deepEqual(first.earned.map((b) => b.id), ["first-step"]);
  const again = L.awardBadges(first.state, { perfectQuizzes: 0, finishedChallenges: 0 });
  assert.equal(again.earned.length, 0);
});

test("dailyPick is stable per day", () => {
  assert.equal(L.dailyPick(LESSONS, "2026-10-01"), L.dailyPick(LESSONS, "2026-10-01"));
});

test("content is well formed", () => {
  const ids = new Set();
  for (const l of LESSONS) {
    assert.ok(!ids.has(l.id), `duplicate lesson id ${l.id}`);
    ids.add(l.id);
    assert.ok(CATEGORIES[l.category], `unknown category for ${l.id}`);
    assert.ok(l.sections.length > 0 && l.quiz.length > 0, `${l.id} needs sections and quiz`);
    for (const q of l.quiz) assert.ok(q.answer >= 0 && q.answer < q.options.length, `bad answer in ${l.id}`);
  }
  for (const c of CHALLENGES) for (const id of c.lessons) assert.ok(ids.has(id), `${c.id} references missing ${id}`);
  for (const s of SHORTS) assert.ok(s.answer < s.options.length);
});

test("finishing chapters awards XP once, counts toward the daily goal and finishes the book", () => {
  let s = L.defaultState();
  let r = L.finishChapter(s, "book", 0, 2, "2026-10-01");
  assert.equal(r.gained, L.XP.chapter);
  assert.equal(r.finishedBook, false);
  assert.equal(L.activityOnDay(r.state, "2026-10-01"), 1);
  assert.equal(r.state.streak, 1);
  const again = L.finishChapter(r.state, "book", 0, 2, "2026-10-01");
  assert.equal(again.gained, 0);
  r = L.finishChapter(r.state, "book", 1, 2, "2026-10-02");
  assert.equal(r.gained, L.XP.chapter + L.XP.bookDone);
  assert.equal(r.finishedBook, true);
  assert.deepEqual(L.readingStats(r.state), { chaptersRead: 2, booksFinished: 1 });
  const badges = L.awardBadges(r.state, { perfectQuizzes: 0, finishedChallenges: 0, ...L.readingStats(r.state) });
  assert.ok(badges.earned.some((b) => b.id === "bibliophile"));
});

test("reading position is remembered without touching progress", () => {
  const s = L.setBookPosition(L.defaultState(), "book", 3, 0.5);
  assert.deepEqual(L.bookEntry(s, "book"), { chapter: 3, pos: 0.5, done: [], finished: false });
  assert.equal(s.xp, 0);
});

test("state saved before books existed still works", () => {
  const old = { ...L.defaultState() };
  delete old.books;
  delete old.chapterLog;
  assert.equal(L.activityOnDay(old, "2026-10-01"), 0);
  assert.equal(L.readingStats(old).chaptersRead, 0);
  assert.equal(L.finishChapter(old, "b", 0, 3, "2026-10-01").gained, L.XP.chapter);
});
