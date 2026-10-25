// Pure game logic: dates, streaks, XP/levels, badges and spaced repetition.
// No DOM access here so it can be unit-tested in Node.
(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory();
  else root.Logic = factory();
})(typeof self !== "undefined" ? self : this, function () {
  const DAY_MS = 24 * 60 * 60 * 1000;

  // Local calendar day as YYYY-MM-DD.
  function dayKey(date) {
    const d = new Date(date);
    const m = String(d.getMonth() + 1).padStart(2, "0");
    const day = String(d.getDate()).padStart(2, "0");
    return `${d.getFullYear()}-${m}-${day}`;
  }

  function daysBetween(fromKey, toKey) {
    const [y1, m1, d1] = fromKey.split("-").map(Number);
    const [y2, m2, d2] = toKey.split("-").map(Number);
    return Math.round((Date.UTC(y2, m2 - 1, d2) - Date.UTC(y1, m1 - 1, d1)) / DAY_MS);
  }

  function addDays(key, n) {
    const [y, m, d] = key.split("-").map(Number);
    const t = new Date(Date.UTC(y, m - 1, d) + n * DAY_MS);
    return t.toISOString().slice(0, 10);
  }

  function defaultState() {
    return {
      xp: 0,
      streak: 0,
      bestStreak: 0,
      lastActiveDay: null,
      activeDays: [],
      completed: {}, // lessonId -> { day, quizScore }
      highlights: [], // { id, lessonId, text }
      cards: {}, // highlightId -> { interval, ease, reps, due }
      challenges: {}, // challengeId -> { startDay, done: [lessonId] }
      badges: [],
      reviews: 0,
      dailyGoal: 1,
    };
  }

  // Record activity for `today`, updating the streak. Returns the new state.
  function touchStreak(state, today) {
    const s = { ...state, activeDays: [...state.activeDays] };
    if (s.lastActiveDay === today) return s;
    const gap = s.lastActiveDay ? daysBetween(s.lastActiveDay, today) : null;
    s.streak = gap === 1 ? s.streak + 1 : 1;
    s.bestStreak = Math.max(s.bestStreak, s.streak);
    s.lastActiveDay = today;
    if (!s.activeDays.includes(today)) s.activeDays.push(today);
    return s;
  }

  // Streak shown to the user: it lapses if yesterday was missed.
  function currentStreak(state, today) {
    if (!state.lastActiveDay) return 0;
    return daysBetween(state.lastActiveDay, today) <= 1 ? state.streak : 0;
  }

  // Level n requires 100 * n * (n + 1) / 2 total XP (100, 300, 600, ...).
  function levelFor(xp) {
    let level = 1;
    while (xp >= (100 * level * (level + 1)) / 2) level++;
    const floor = (100 * (level - 1) * level) / 2;
    const ceil = (100 * level * (level + 1)) / 2;
    return { level, into: xp - floor, needed: ceil - floor };
  }

  const XP = { lesson: 50, quizPerfect: 30, quizPerQuestion: 10, highlight: 5, review: 5, challengeDay: 20, challengeDone: 100 };

  function completeLesson(state, lessonId, quizScore, quizTotal, today) {
    let s = touchStreak(state, today);
    const first = !s.completed[lessonId];
    let gained = 0;
    if (first) gained += XP.lesson;
    gained += quizScore * XP.quizPerQuestion;
    if (quizTotal > 0 && quizScore === quizTotal) gained += XP.quizPerfect;
    const prev = s.completed[lessonId];
    s = {
      ...s,
      xp: s.xp + gained,
      completed: {
        ...s.completed,
        [lessonId]: { day: prev ? prev.day : today, quizScore: Math.max(quizScore, prev ? prev.quizScore : 0) },
      },
    };
    return { state: s, gained };
  }

  function lessonsOnDay(state, day) {
    return Object.values(state.completed).filter((c) => c.day === day).length;
  }

  // --- Spaced repetition (simplified SM-2) ---
  // grade: 0 = forgot, 1 = hard, 2 = good, 3 = easy
  function newCard(today) {
    return { interval: 0, ease: 2.5, reps: 0, due: today };
  }

  function reviewCard(card, grade, today) {
    let { interval, ease, reps } = card;
    if (grade === 0) {
      reps = 0;
      interval = 1;
      ease = Math.max(1.3, ease - 0.2);
    } else {
      reps += 1;
      if (reps === 1) interval = grade === 3 ? 3 : 1;
      else if (reps === 2) interval = grade === 1 ? 3 : 6;
      else interval = Math.round(interval * (grade === 1 ? 1.2 : ease) * (grade === 3 ? 1.3 : 1));
      ease = Math.max(1.3, ease + (grade === 1 ? -0.15 : grade === 3 ? 0.15 : 0));
    }
    return { interval, ease: Math.round(ease * 100) / 100, reps, due: addDays(today, interval) };
  }

  function dueCards(state, today) {
    return state.highlights.filter((h) => {
      const c = state.cards[h.id];
      return c && daysBetween(c.due, today) >= 0;
    });
  }

  // --- Challenges ---
  // A challenge unlocks one lesson per day since it started.
  function challengeProgress(challenge, entry, today) {
    if (!entry) return { started: false, unlocked: 0, done: 0, finished: false };
    const unlocked = Math.min(challenge.lessons.length, daysBetween(entry.startDay, today) + 1);
    const done = entry.done.length;
    return { started: true, unlocked, done, finished: done >= challenge.lessons.length };
  }

  // --- Badges ---
  const BADGES = [
    { id: "first-step", name: "First Step", icon: "🌱", desc: "Finish your first lesson", test: (s) => Object.keys(s.completed).length >= 1 },
    { id: "bookworm", name: "Bookworm", icon: "📚", desc: "Finish 5 lessons", test: (s) => Object.keys(s.completed).length >= 5 },
    { id: "scholar", name: "Scholar", icon: "🎓", desc: "Finish 10 lessons", test: (s) => Object.keys(s.completed).length >= 10 },
    { id: "streak-3", name: "On a Roll", icon: "🔥", desc: "Reach a 3-day streak", test: (s) => s.bestStreak >= 3 },
    { id: "streak-7", name: "Week Warrior", icon: "⚡", desc: "Reach a 7-day streak", test: (s) => s.bestStreak >= 7 },
    { id: "streak-30", name: "Unstoppable", icon: "🏆", desc: "Reach a 30-day streak", test: (s) => s.bestStreak >= 30 },
    { id: "highlighter", name: "Highlighter", icon: "🖍️", desc: "Save 5 highlights", test: (s) => s.highlights.length >= 5 },
    { id: "recall", name: "Total Recall", icon: "🧠", desc: "Complete 20 flashcard reviews", test: (s) => s.reviews >= 20 },
    { id: "perfect", name: "Sharp Mind", icon: "🎯", desc: "Ace 3 quizzes", test: (s, ctx) => ctx.perfectQuizzes >= 3 },
    { id: "challenger", name: "Challenger", icon: "🏅", desc: "Finish a challenge", test: (s, ctx) => ctx.finishedChallenges >= 1 },
    { id: "level-5", name: "Rising Star", icon: "⭐", desc: "Reach level 5", test: (s) => levelFor(s.xp).level >= 5 },
  ];

  // Returns { state, earned: [badge] } with any newly earned badges added.
  function awardBadges(state, ctx) {
    const earned = BADGES.filter((b) => !state.badges.includes(b.id) && b.test(state, ctx));
    if (!earned.length) return { state, earned };
    return { state: { ...state, badges: [...state.badges, ...earned.map((b) => b.id)] }, earned };
  }

  // Deterministic "free pick of the day" from the lesson list.
  function dailyPick(lessons, today) {
    let h = 0;
    for (const ch of today) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
    return lessons[h % lessons.length];
  }

  return {
    dayKey, daysBetween, addDays, defaultState, touchStreak, currentStreak, levelFor, XP,
    completeLesson, lessonsOnDay, newCard, reviewCard, dueCards, challengeProgress,
    BADGES, awardBadges, dailyPick,
  };
});
