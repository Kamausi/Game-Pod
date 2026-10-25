// UI layer: hash router, views and persistence. Game rules live in logic.js.
(function () {
  const { LESSONS, CATEGORIES, CHALLENGES, SHORTS } = window.Content;
  const L = window.Logic;
  const Books = window.BookCatalog || { categories: {}, books: [] };
  const STORAGE_KEY = "gamepod:v1";
  const view = document.getElementById("view");

  // ---------- state ----------
  function load() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) return { ...L.defaultState(), ...JSON.parse(raw) };
    } catch (e) { /* storage unavailable or corrupt: start fresh */ }
    return L.defaultState();
  }

  let state = load();

  function save() {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(state)); } catch (e) { /* ignore */ }
    renderHeader();
  }

  const today = () => L.dayKey(Date.now());
  const lessonById = (id) => LESSONS.find((l) => l.id === id);
  const challengeById = (id) => CHALLENGES.find((c) => c.id === id);

  function esc(s) {
    return String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  }

  function badgeContext() {
    const perfectQuizzes = Object.entries(state.completed).filter(([id, c]) => {
      const l = lessonById(id);
      return l && c.quizScore === l.quiz.length;
    }).length;
    const finishedChallenges = CHALLENGES.filter((c) => L.challengeProgress(c, state.challenges[c.id], today()).finished).length;
    return { perfectQuizzes, finishedChallenges, ...L.readingStats(state) };
  }

  const bookById = (id) => Books.books.find((b) => b.id === id);

  // Title of whatever a highlight came from: a lesson or a book ("book:<id>").
  function sourceTitle(sourceId) {
    if (sourceId.startsWith("book:")) {
      const b = bookById(sourceId.slice(5));
      return b ? b.title : "";
    }
    const l = lessonById(sourceId);
    return l ? l.title : "";
  }

  // Award any newly earned badges and announce them.
  function checkBadges() {
    const res = L.awardBadges(state, badgeContext());
    state = res.state;
    res.earned.forEach((b) => toast(`${b.icon} Badge unlocked: ${b.name}`));
  }

  // Toasts are queued so several in a row (XP, level up, badge) are all seen.
  const toasts = [];
  function toast(msg) {
    toasts.push(msg);
    if (toasts.length === 1) showNextToast();
  }

  function showNextToast() {
    const t = document.getElementById("toast");
    t.textContent = toasts[0];
    t.classList.add("show");
    setTimeout(() => {
      t.classList.remove("show");
      setTimeout(() => {
        toasts.shift();
        if (toasts.length) showNextToast();
      }, 250);
    }, 2000);
  }

  function renderHeader() {
    document.getElementById("streak-pill").textContent = `🔥 ${L.currentStreak(state, today())}`;
    document.getElementById("xp-pill").textContent = `⭐ Lv ${L.levelFor(state.xp).level}`;
  }

  // ---------- small render helpers ----------
  function tag(category) {
    const c = CATEGORIES[category];
    return `<span class="tag" style="background:${c.color}">${esc(c.label)}</span>`;
  }

  function lessonCard(l) {
    const done = state.completed[l.id];
    return `<a class="card" href="#/lesson/${l.id}">
      <div class="row spread">${tag(l.category)}<span class="muted small">${l.minutes} min ${done ? '<span class="done-mark">✓</span>' : ""}</span></div>
      <h3 style="margin-top:8px">${esc(l.title)}</h3>
      <div class="muted small">${esc(l.hook)}</div>
    </a>`;
  }

  function progressBar(fraction) {
    return `<div class="progress"><div style="width:${Math.round(Math.min(1, fraction) * 100)}%"></div></div>`;
  }

  // ---------- views ----------
  function homeView() {
    const t = today();
    const streak = L.currentStreak(state, t);
    const doneToday = L.activityOnDay(state, t);
    const pick = L.dailyPick(LESSONS, t);
    const due = L.dueCards(state, t).length;
    const week = [];
    for (let i = 6; i >= 0; i--) {
      const d = L.addDays(t, -i);
      const label = new Date(d + "T12:00:00").toLocaleDateString(undefined, { weekday: "narrow" });
      week.push(`<span class="${state.activeDays.includes(d) ? "on" : ""}">${label}</span>`);
    }
    const active = CHALLENGES.filter((c) => state.challenges[c.id] && !L.challengeProgress(c, state.challenges[c.id], t).finished);
    const nextUp = LESSONS.filter((l) => !state.completed[l.id]).slice(0, 4);
    const reading = recentBooks();

    return `
      <div class="card hero">
        <div class="row spread">
          <div>
            <div class="muted small">Daily streak</div>
            <div style="font-size:34px;font-weight:800">🔥 ${streak} day${streak === 1 ? "" : "s"}</div>
          </div>
          <div style="text-align:right">
            <div class="muted small">Today's goal</div>
            <div style="font-size:22px;font-weight:700">${Math.min(doneToday, state.dailyGoal)}/${state.dailyGoal}</div>
          </div>
        </div>
        <div class="week">${week.join("")}</div>
      </div>

      ${reading.length ? `<h2>📖 Continue reading</h2>${reading.slice(0, 2).map(continueCard).join("")}` : ""}

      <h2>✨ Pick of the day</h2>
      ${lessonCard(pick)}

      ${due ? `<h2>🧠 Ready to review</h2>
        <a class="card" href="#/review"><div class="row spread"><div><h3>${due} flashcard${due === 1 ? "" : "s"} due</h3><div class="muted small">A quick review locks ideas into memory.</div></div><span class="btn primary">Review</span></div></a>` : ""}

      <h2>🏁 Challenges</h2>
      ${active.length ? active.map(challengeCard).join("") : `<a class="card" href="#/challenges"><h3>Start a challenge</h3><div class="muted small">Guided paths that unlock one lesson a day.</div></a>`}

      ${!reading.length ? `<h2>🏛️ Read a classic</h2><a class="card" href="#/books"><h3>${Books.books.length} full-length books</h3>
        <div class="muted small">Public-domain classics, from Meditations to The Great Gatsby. Every chapter earns XP.</div></a>` : ""}

      ${nextUp.length ? `<h2>🎓 Up next</h2><div class="grid">${nextUp.map(lessonCard).join("")}</div>` : ""}
    `;
  }

  function libraryView(params) {
    const filter = params.get("c") || "all";
    const list = filter === "all" ? LESSONS : LESSONS.filter((l) => l.category === filter);
    const btn = (key, label) => `<a class="btn small ${filter === key ? "active" : ""}" href="#/library?c=${key}">${esc(label)}</a>`;
    const done = Object.keys(state.completed).length;
    return `
      <h1>Lessons</h1>
      <p class="muted">${done} of ${LESSONS.length} lessons finished · <a href="#/challenges">Challenges</a></p>
      <div class="filters">${btn("all", "All")}${Object.entries(CATEGORIES).map(([k, c]) => btn(k, c.label)).join("")}</div>
      <div class="grid" style="margin-top:12px">${list.map(lessonCard).join("")}</div>
    `;
  }

  function lessonView(id) {
    const l = lessonById(id);
    if (!l) return notFound();
    const saved = new Set(state.highlights.filter((h) => h.lessonId === id).map((h) => h.text));
    return `
      <a href="#/library" class="muted small">← Lessons</a>
      <div class="row spread" style="margin-top:8px">${tag(l.category)}<span class="muted small">${l.minutes} min read</span></div>
      <h1>${esc(l.title)}</h1>
      <div class="row" style="margin:12px 0">
        <button class="btn small" data-action="listen">🔊 Listen</button>
        <span class="muted small">Tip: select any text to save it as a flashcard.</span>
      </div>
      <article class="reader" data-lesson="${l.id}">
        <p class="hook">${esc(l.hook)}</p>
        ${l.sections.map((s, i) => `
          <section>
            <div class="row spread"><h3>${i + 1}. ${esc(s.heading)}</h3>
              <button class="btn small" data-action="save-section" data-index="${i}" ${saved.has(s.body) ? "disabled" : ""}>${saved.has(s.body) ? "✓ Saved" : "🖍️ Save"}</button>
            </div>
            <p>${esc(s.body)}</p>
          </section>`).join("")}
      </article>
      <div class="card" style="text-align:center">
        <h3>Ready to lock it in?</h3>
        <p class="muted small">Answer ${l.quiz.length} quick question${l.quiz.length === 1 ? "" : "s"} to finish and earn XP.</p>
        <a class="btn primary" href="#/quiz/${l.id}">Take the quiz</a>
      </div>
    `;
  }

  // Quiz runs one question at a time; progress is kept in memory only.
  let quiz = null;

  function quizView(id) {
    const l = lessonById(id);
    if (!l) return notFound();
    if (!quiz || quiz.id !== id) quiz = { id, index: 0, score: 0, answered: null };
    if (quiz.index >= l.quiz.length) return quizResults(l);
    const q = l.quiz[quiz.index];
    return `
      <a href="#/lesson/${l.id}" class="muted small">← ${esc(l.title)}</a>
      <div style="margin:12px 0">${progressBar(quiz.index / l.quiz.length)}</div>
      <div class="card">
        <div class="muted small">Question ${quiz.index + 1} of ${l.quiz.length}</div>
        <h2 style="margin-top:6px">${esc(q.q)}</h2>
        ${q.options.map((o, i) => {
          let cls = "";
          if (quiz.answered !== null) {
            if (i === q.answer) cls = "correct";
            else if (i === quiz.answered) cls = "wrong";
          }
          return `<button class="btn quiz-option ${cls}" data-action="answer" data-index="${i}" ${quiz.answered !== null ? "disabled" : ""}>${esc(o)}</button>`;
        }).join("")}
        ${quiz.answered !== null ? `<div class="row spread" style="margin-top:12px">
          <strong>${quiz.answered === q.answer ? "✅ Correct!" : "❌ Not quite."}</strong>
          <button class="btn primary" data-action="next-question">${quiz.index + 1 < l.quiz.length ? "Next" : "Finish"}</button>
        </div>` : ""}
      </div>
    `;
  }

  function quizResults(l) {
    if (!quiz.result) {
      const t = today();
      const before = L.levelFor(state.xp).level;
      const res = L.completeLesson(state, l.id, quiz.score, l.quiz.length, t);
      state = res.state;
      let gained = res.gained;
      const notes = [];
      // Advance any started challenge that has this lesson unlocked.
      for (const c of CHALLENGES) {
        const entry = state.challenges[c.id];
        if (!entry || entry.done.includes(l.id)) continue;
        const p = L.challengeProgress(c, entry, t);
        const idx = c.lessons.indexOf(l.id);
        if (idx === -1 || idx >= p.unlocked) continue;
        const updated = { ...entry, done: [...entry.done, l.id] };
        state = { ...state, challenges: { ...state.challenges, [c.id]: updated } };
        gained += L.XP.challengeDay;
        state.xp += L.XP.challengeDay;
        notes.push(`${c.icon} ${c.title}: day ${updated.done.length} complete`);
        if (L.challengeProgress(c, updated, t).finished) {
          state.xp += L.XP.challengeDone;
          gained += L.XP.challengeDone;
          notes.push(`🎉 You finished the ${c.title} challenge!`);
        }
      }
      checkBadges();
      save();
      const after = L.levelFor(state.xp).level;
      quiz.result = { gained, notes, levelUp: after > before ? after : null };
    }
    const { gained, notes, levelUp } = quiz.result;
    const lv = L.levelFor(state.xp);
    return `
      <div class="card" style="text-align:center">
        <div style="font-size:48px">${quiz.score === l.quiz.length ? "🎯" : "👏"}</div>
        <h1>Lesson complete!</h1>
        <p class="muted">You got ${quiz.score} of ${l.quiz.length} right.</p>
        <p style="font-size:24px;font-weight:800">+${gained} XP</p>
        ${levelUp ? `<p><strong>⭐ Level up! You're now level ${levelUp}.</strong></p>` : ""}
        ${notes.map((n) => `<p>${esc(n)}</p>`).join("")}
        <div style="margin:12px 0">${progressBar(lv.into / lv.needed)}<div class="muted small" style="margin-top:4px">Level ${lv.level} · ${lv.into}/${lv.needed} XP</div></div>
        <div class="row" style="justify-content:center">
          <a class="btn" href="#/lesson/${l.id}">Reread</a>
          <a class="btn primary" href="#/">Done</a>
        </div>
      </div>
    `;
  }

  let shortIndex = 0;
  let shortAnswered = null;

  function shortsView() {
    const s = SHORTS[shortIndex % SHORTS.length];
    return `
      <h1>Shorts</h1>
      <p class="muted">Bite-sized facts. Swap a minute of scrolling for one of these.</p>
      <div class="card short">
        <div class="fact">💡 ${esc(s.fact)}</div>
        <div>
          <h3 style="margin-top:16px">${esc(s.q)}</h3>
          ${s.options.map((o, i) => {
            let cls = "";
            if (shortAnswered !== null) cls = i === s.answer ? "correct" : i === shortAnswered ? "wrong" : "";
            return `<button class="btn quiz-option ${cls}" data-action="short-answer" data-index="${i}" ${shortAnswered !== null ? "disabled" : ""}>${esc(o)}</button>`;
          }).join("")}
        </div>
      </div>
      <div class="row spread" style="margin-top:12px">
        <span class="muted small">${(shortIndex % SHORTS.length) + 1} / ${SHORTS.length}</span>
        <button class="btn primary" data-action="next-short">Next ⚡</button>
      </div>
    `;
  }

  let reviewRevealed = false;

  function reviewView() {
    const due = L.dueCards(state, today());
    if (!state.highlights.length) {
      return `<h1>Review</h1><div class="card"><h3>No flashcards yet</h3>
        <p class="muted">Save highlights while reading and they'll turn into flashcards here.</p>
        <a class="btn primary" href="#/library">Find a lesson</a></div>`;
    }
    if (!due.length) {
      const next = state.highlights.map((h) => state.cards[h.id].due).sort()[0];
      return `<h1>Review</h1><div class="card" style="text-align:center"><div style="font-size:42px">✅</div>
        <h3>All caught up!</h3><p class="muted">Next card is due ${esc(next)}.</p>
        <p class="muted small">${state.highlights.length} card${state.highlights.length === 1 ? "" : "s"} in your deck · ${state.reviews} review${state.reviews === 1 ? "" : "s"} done</p></div>`;
    }
    const h = due[0];
    return `
      <h1>Review</h1>
      <p class="muted">${due.length} card${due.length === 1 ? "" : "s"} due</p>
      <div class="card flashcard">
        <div class="muted small">${esc(sourceTitle(h.lessonId))}</div>
        <h2 style="margin:6px 0">${esc(h.prompt || "What was the key idea you saved?")}</h2>
        ${reviewRevealed ? `<div class="answer">${esc(h.text)}</div>` : ""}
      </div>
      <div class="row" style="margin-top:12px;justify-content:center">
        ${reviewRevealed
          ? ["Forgot", "Hard", "Good", "Easy"].map((g, i) => `<button class="btn ${i === 2 ? "primary" : ""}" data-action="grade" data-grade="${i}" data-id="${h.id}">${g}</button>`).join("")
          : `<button class="btn primary" data-action="reveal">Show answer</button>`}
      </div>
    `;
  }

  function challengeCard(c) {
    const p = L.challengeProgress(c, state.challenges[c.id], today());
    return `<a class="card" href="#/challenge/${c.id}">
      <div class="row spread"><h3>${c.icon} ${esc(c.title)}</h3><span class="muted small">${c.lessons.length} days</span></div>
      <div class="muted small" style="margin-bottom:8px">${esc(c.desc)}</div>
      ${p.started ? `${progressBar(p.done / c.lessons.length)}<div class="muted small" style="margin-top:4px">${p.finished ? "🎉 Completed" : `${p.done}/${c.lessons.length} done`}</div>` : ""}
    </a>`;
  }

  function challengesView() {
    return `<h1>Challenges</h1><p class="muted">Each challenge unlocks one lesson per day. Finish them all for bonus XP.</p>
      ${CHALLENGES.map(challengeCard).join("")}`;
  }

  function challengeView(id) {
    const c = challengeById(id);
    if (!c) return notFound();
    const entry = state.challenges[c.id];
    const p = L.challengeProgress(c, entry, today());
    return `
      <a href="#/challenges" class="muted small">← Challenges</a>
      <h1>${c.icon} ${esc(c.title)}</h1>
      <p class="muted">${esc(c.desc)}</p>
      ${p.started ? progressBar(p.done / c.lessons.length) : `<button class="btn primary" data-action="start-challenge" data-id="${c.id}">Start challenge</button>`}
      <div style="margin-top:16px">
        ${c.lessons.map((lid, i) => {
          const l = lessonById(lid);
          const done = entry && entry.done.includes(lid);
          const unlocked = p.started && i < p.unlocked;
          const status = done ? '<span class="done-mark">✓ Done</span>' : unlocked ? '<span class="btn small primary">Start</span>' : "🔒";
          const inner = `<div class="row spread"><div><div class="muted small">Day ${i + 1}</div><h3>${esc(l.title)}</h3></div>${status}</div>`;
          return unlocked || done ? `<a class="card" href="#/lesson/${lid}">${inner}</a>` : `<div class="card">${inner}</div>`;
        }).join("")}
      </div>
    `;
  }

  function profileView() {
    const lv = L.levelFor(state.xp);
    const ctx = badgeContext();
    const theme = document.documentElement.dataset.theme || "auto";
    return `
      <h1>Your progress</h1>
      <div class="card">
        <div class="row spread"><h3>⭐ Level ${lv.level}</h3><span class="muted small">${state.xp} XP total</span></div>
        ${progressBar(lv.into / lv.needed)}
        <div class="muted small" style="margin-top:4px">${lv.needed - lv.into} XP to level ${lv.level + 1}</div>
      </div>
      <div class="grid" style="margin-top:12px">
        <div class="card"><div class="muted small">Current streak</div><h3>🔥 ${L.currentStreak(state, today())}</h3></div>
        <div class="card"><div class="muted small">Best streak</div><h3>🏆 ${state.bestStreak}</h3></div>
        <div class="card"><div class="muted small">Lessons</div><h3>📚 ${Object.keys(state.completed).length}/${LESSONS.length}</h3></div>
        <div class="card"><div class="muted small">Perfect quizzes</div><h3>🎯 ${ctx.perfectQuizzes}</h3></div>
        <div class="card"><div class="muted small">Chapters read</div><h3>📖 ${ctx.chaptersRead}</h3></div>
        <div class="card"><div class="muted small">Books finished</div><h3>🏛️ ${ctx.booksFinished}</h3></div>
      </div>

      <h2>Badges (${state.badges.length}/${L.BADGES.length})</h2>
      <div class="grid">
        ${L.BADGES.map((b) => `<div class="card badge ${state.badges.includes(b.id) ? "" : "locked"}">
          <div class="icon">${b.icon}</div><h3>${esc(b.name)}</h3><div class="muted small">${esc(b.desc)}</div></div>`).join("")}
      </div>

      <h2>Highlights (${state.highlights.length})</h2>
      ${state.highlights.length ? state.highlights.map((h) => {
        return `<div class="card"><div class="row spread"><span class="muted small">${esc(sourceTitle(h.lessonId))}</span>
          <button class="btn small danger" data-action="delete-highlight" data-id="${h.id}">Delete</button></div>
          <p style="margin:6px 0 0">${esc(h.text)}</p></div>`;
      }).join("") : `<p class="muted">Nothing saved yet.</p>`}

      <h2>Settings</h2>
      <div class="card">
        <div class="row spread"><span>Daily goal</span>
          <select data-action="goal">${[1, 2, 3].map((n) => `<option value="${n}" ${state.dailyGoal === n ? "selected" : ""}>${n} lesson${n > 1 ? "s" : ""}/day</option>`).join("")}</select></div>
        <div class="row spread" style="margin-top:12px"><span>Theme</span>
          <select data-action="theme">${["auto", "light", "dark"].map((t) => `<option value="${t}" ${theme === t ? "selected" : ""}>${t[0].toUpperCase() + t.slice(1)}</option>`).join("")}</select></div>
        <div style="margin-top:16px"><button class="btn small danger" data-action="reset">Reset all progress</button></div>
      </div>
    `;
  }

  // ---------- books ----------
  // Each book is a gzipped JSON file in books/, fetched only when opened.
  const bookCache = new Map();
  let bookQuery = "";
  let bookFilter = "all";
  let readerSession = null; // { id, index, total, chapter } while a chapter is open

  function loadBook(id) {
    if (!bookCache.has(id)) {
      // A host that can't serve .gz can set window.BookFiles = "books/{id}.json".
      const url = (window.BookFiles || "books/{id}.json.gz").replace("{id}", id);
      const p = fetch(url)
        .then((r) => {
          if (!r.ok) throw new Error(`HTTP ${r.status}`);
          return r.arrayBuffer();
        })
        .then(async (buf) => {
          const bytes = new Uint8Array(buf);
          // Some servers decompress .gz for us; only inflate if it's still gzip.
          if (bytes[0] === 0x1f && bytes[1] === 0x8b) {
            const stream = new Blob([buf]).stream().pipeThrough(new DecompressionStream("gzip"));
            return JSON.parse(await new Response(stream).text());
          }
          return JSON.parse(new TextDecoder().decode(bytes));
        });
      p.catch(() => bookCache.delete(id));
      bookCache.set(id, p);
    }
    return bookCache.get(id);
  }

  // Fill #slot once the book loads, unless the user has navigated away.
  function fillWhenLoaded(id, fn) {
    const hash = location.hash;
    loadBook(id)
      .then((book) => { if (location.hash === hash) fn(book); })
      .catch(() => {
        const slot = view.querySelector("#slot");
        if (location.hash !== hash || !slot) return;
        slot.innerHTML = `<div class="card"><h3>Couldn't load this book</h3><p class="muted small">Books are loaded from the
          <code>books/</code> folder, so the app has to be served over HTTP (for example with <code>npm start</code>)
          rather than opened as a file.</p></div>`;
      });
  }

  function remember(id, chapter, pos) {
    state = L.setBookPosition(state, id, chapter, pos);
    state.books[id].updated = Date.now();
    save();
  }

  function formatYear(y) {
    if (y == null) return "";
    if (y < 0) return `c. ${-y} BC`;
    return y < 1000 ? `c. ${y} AD` : String(y);
  }

  function formatLength(minutes) {
    return minutes < 90 ? `${Math.max(1, minutes)} min` : `${Math.round(minutes / 60)} h`;
  }

  const chapterMinutes = (ch) => Math.max(1, Math.round(ch.w / 238));
  // Many novels title chapters with a bare numeral ("XIV"); show "Chapter XIV".
  const chapterLabel = (t) => (/^[IVXLC]+$/.test(t) ? `Chapter ${t}` : t);

  function bookTag(b) {
    const c = Books.categories[b.category];
    return c ? `<span class="tag" style="background:${c.color}">${esc(c.label)}</span>` : "";
  }

  function cover(b, size) {
    const c = Books.categories[b.category] || { color: "#55606a" };
    // Vary the shade per book so a shelf of one category isn't a single colour.
    let h = 0;
    for (const ch of b.id) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
    const shift = (h % 50) - 25;
    return `<div class="cover ${size || ""}" style="--cover:${c.color};--shift:${shift}deg" aria-hidden="true">
      <div class="cover-title">${esc(b.title)}</div><div class="cover-author">${esc(b.author)}</div></div>`;
  }

  function bookProgress(b) {
    return b.chapters ? L.bookEntry(state, b.id).done.length / b.chapters : 0;
  }

  // Books that are started but not finished, most recently read first.
  function recentBooks() {
    return Object.entries(state.books || {})
      .filter(([id, e]) => bookById(id) && !e.finished && (e.done.length || e.chapter || e.pos))
      .sort((a, b) => (b[1].updated || 0) - (a[1].updated || 0))
      .map(([id]) => bookById(id));
  }

  function continueCard(b) {
    const e = L.bookEntry(state, b.id);
    return `<a class="card" href="#/read/${b.id}/${e.chapter}" style="display:flex;gap:12px;align-items:center">${cover(b, "mini")}
      <div style="flex:1;min-width:0"><h3>${esc(b.title)}</h3>
        <div class="muted small">${esc(b.author)} · chapter ${e.chapter + 1} of ${b.chapters}</div>
        <div style="margin-top:8px">${progressBar(bookProgress(b))}</div></div></a>`;
  }

  function bookCard(b) {
    const p = bookProgress(b);
    return `<a class="book-card" href="#/book/${b.id}">${cover(b)}
      <div class="book-title">${esc(b.title)}</div>
      <div class="muted small">${esc(b.author)}</div>
      ${p > 0 ? `<div style="margin-top:4px">${progressBar(p)}</div>` : `<div class="muted small">${formatLength(b.minutes)}</div>`}</a>`;
  }

  function bookGrid() {
    const q = bookQuery.trim().toLowerCase();
    const list = Books.books.filter((b) =>
      (bookFilter === "all" || b.category === bookFilter) &&
      (!q || `${b.title} ${b.author}`.toLowerCase().includes(q)));
    return list.length ? list.map(bookCard).join("") : `<p class="muted">No books match.</p>`;
  }

  function booksView(params) {
    bookFilter = params.get("c") || "all";
    const btn = (key, label) => `<a class="btn small ${bookFilter === key ? "active" : ""}" href="#/books?c=${key}">${esc(label)}</a>`;
    const reading = recentBooks();
    const finished = L.readingStats(state).booksFinished;
    return `
      <h1>Books</h1>
      <p class="muted">${Books.books.length} full-length public-domain classics${finished ? ` · ${finished} finished` : ""}.</p>
      ${reading.length ? `<h2>Continue reading</h2>${reading.slice(0, 3).map(continueCard).join("")}<h2>All books</h2>` : ""}
      <input class="search" type="search" placeholder="Search by title or author" aria-label="Search books"
        data-action="book-search" value="${esc(bookQuery)}">
      <div class="filters">${btn("all", "All")}${Object.entries(Books.categories).map(([k, c]) => btn(k, c.label)).join("")}</div>
      <div class="book-grid" id="book-grid">${bookGrid()}</div>
    `;
  }

  function bookView(id) {
    const b = bookById(id);
    if (!b) return notFound();
    const e = L.bookEntry(state, id);
    const started = e.done.length || e.chapter || e.pos;
    fillWhenLoaded(id, (book) => {
      view.querySelector("#slot").innerHTML = `<h2>Contents</h2><div class="card toc">${book.chapters.map((c, i) => `
        <a href="#/read/${id}/${i}" class="toc-row ${started && i === e.chapter ? "current" : ""}">
          <span>${esc(chapterLabel(c.t))}</span>
          <span class="muted small">${e.done.includes(i) ? '<span class="done-mark">✓</span>' : `${chapterMinutes(c)} min`}</span>
        </a>`).join("")}</div>`;
    });
    return `
      <a href="#/books" class="muted small">← Books</a>
      <div class="book-hero">
        ${cover(b, "large")}
        <div>
          ${bookTag(b)}
          <h1>${esc(b.title)}</h1>
          <div>${esc(b.author)}${b.translator ? `<span class="muted"> · translated by ${esc(b.translator)}</span>` : ""}</div>
          <div class="muted small">${formatYear(b.year)} · ${b.chapters} chapters · about ${formatLength(b.minutes)}</div>
          <div style="margin:12px 0">${progressBar(bookProgress(b))}
            <div class="muted small" style="margin-top:4px">${e.done.length}/${b.chapters} chapters read${e.finished ? " · 🎉 Finished" : ""}</div></div>
          <a class="btn primary" href="#/read/${id}/${e.chapter}">${started ? "Continue reading" : "Start reading"}</a>
        </div>
      </div>
      ${b.blurb ? `<p>${esc(b.blurb)}</p>` : ""}
      <div id="slot"><p class="muted">Loading contents…</p></div>
      <p class="muted small">Text from the <a href="${esc(b.source)}" target="_blank" rel="noopener">Standard Ebooks edition</a>,
        in the public domain in the United States.</p>
    `;
  }

  function renderBlock(b) {
    switch (b[0]) {
      case "h": return `<h3>${esc(b[1])}</h3>`;
      case "v": return `<p class="verse">${esc(b[1])}</p>`;
      case "q": return `<blockquote>${esc(b[1])}</blockquote>`;
      case "d": return `<p class="line"><span class="speaker">${esc(b[1])}</span> ${esc(b[2])}</p>`;
      case "b": return `<hr class="break">`;
      default: return `<p>${esc(b[1])}</p>`;
    }
  }

  function readerView(id, index) {
    const b = bookById(id);
    if (!b) return notFound();
    fillWhenLoaded(id, (book) => {
      const slot = view.querySelector("#slot");
      const ch = book.chapters[index];
      if (!ch) { slot.innerHTML = notFound(); return; }
      const entry = L.bookEntry(state, id);
      const resume = entry.chapter === index ? entry.pos : 0;
      remember(id, index, resume);
      readerSession = { id, index, total: book.chapters.length, chapter: ch };
      const last = index + 1 >= book.chapters.length;
      slot.innerHTML = `
        <div class="muted small" style="margin-top:12px">Chapter ${index + 1} of ${book.chapters.length} · ${chapterMinutes(ch)} min</div>
        <h1>${esc(chapterLabel(ch.t))}</h1>
        <div class="row" style="margin:12px 0">
          <button class="btn small" data-action="listen-book">🔊 Listen</button>
          <button class="btn small" data-action="font" data-step="-1" aria-label="Smaller text">A−</button>
          <button class="btn small" data-action="font" data-step="1" aria-label="Larger text">A+</button>
        </div>
        <article class="reader book-text" data-lesson="book:${id}">${ch.b.map(renderBlock).join("")}</article>
        <div class="card chapter-end">
          ${entry.done.includes(index)
            ? `<p class="done-mark">✓ Chapter read</p>`
            : `<button class="btn primary" data-action="finish-chapter">✓ Finish chapter · +${L.XP.chapter} XP${last ? ` (+${L.XP.bookDone} for the book)` : ""}</button>`}
          <div class="row spread" style="margin-top:12px">
            ${index > 0 ? `<a class="btn small" href="#/read/${id}/${index - 1}">← Previous</a>` : "<span></span>"}
            ${last ? "" : `<a class="btn small" href="#/read/${id}/${index + 1}">Next →</a>`}
          </div>
        </div>`;
      if (resume > 0) {
        requestAnimationFrame(() => window.scrollTo(0, resume * (document.documentElement.scrollHeight - innerHeight)));
      }
    });
    return `
      <div class="row spread"><a href="#/book/${id}" class="muted small">← ${esc(b.title)}</a><span class="muted small">${esc(b.author)}</span></div>
      <div id="slot"><p class="muted">Loading…</p></div>
    `;
  }

  function finishCurrentChapter() {
    const r = readerSession;
    if (!r) return;
    const before = L.levelFor(state.xp).level;
    const res = L.finishChapter(state, r.id, r.index, r.total, today());
    state = res.state;
    if (res.gained) toast(res.finishedBook ? `🎉 You finished the book! +${res.gained} XP` : `📖 Chapter done! +${res.gained} XP`);
    const after = L.levelFor(state.xp).level;
    if (after > before) toast(`⭐ Level up! You're now level ${after}`);
    checkBadges();
    save();
    if (r.index + 1 < r.total) {
      remember(r.id, r.index + 1, 0);
      location.hash = `#/read/${r.id}/${r.index + 1}`;
    } else {
      location.hash = `#/book/${r.id}`;
    }
  }

  // Remember the reading position as the user scrolls.
  let scrollTimer;
  window.addEventListener("scroll", () => {
    if (!readerSession) return;
    clearTimeout(scrollTimer);
    scrollTimer = setTimeout(() => {
      if (!readerSession) return;
      const max = document.documentElement.scrollHeight - innerHeight;
      remember(readerSession.id, readerSession.index, max > 0 ? Math.min(1, scrollY / max) : 0);
    }, 400);
  }, { passive: true });

  // Chapters can be long, so speak one block at a time.
  function toggleBookAudio(btn) {
    if (!("speechSynthesis" in window)) return toast("Audio isn't supported in this browser");
    if (speechSynthesis.speaking || speechSynthesis.pending) {
      stopAudio();
      btn.textContent = "🔊 Listen";
      return;
    }
    const r = readerSession;
    if (!r) return;
    const texts = [r.chapter.t, ...r.chapter.b.filter((b) => b.length > 1).map((b) => (b[0] === "d" ? `${b[1]}. ${b[2]}` : b[1]))];
    texts.forEach((t, i) => {
      const u = new SpeechSynthesisUtterance(t.replace(/\n/g, " "));
      u.rate = 1.05;
      if (i === texts.length - 1) u.onend = () => { btn.textContent = "🔊 Listen"; };
      speechSynthesis.speak(u);
    });
    btn.textContent = "⏹ Stop";
  }

  function notFound() {
    return `<div class="card"><h3>Not found</h3><a href="#/">Go home</a></div>`;
  }

  // ---------- router ----------
  const routes = [
    [/^\/$/, () => homeView(), "home"],
    [/^\/library$/, (m, p) => libraryView(p), "library"],
    [/^\/lesson\/([\w-]+)$/, (m) => lessonView(m[1]), "library"],
    [/^\/quiz\/([\w-]+)$/, (m) => quizView(m[1]), "library"],
    [/^\/shorts$/, () => shortsView(), "shorts"],
    [/^\/review$/, () => reviewView(), "review"],
    [/^\/challenges$/, () => challengesView(), "home"],
    [/^\/challenge\/([\w-]+)$/, (m) => challengeView(m[1]), "home"],
    [/^\/profile$/, () => profileView(), "profile"],
    [/^\/books$/, (m, p) => booksView(p), "books"],
    [/^\/book\/([\w-]+)$/, (m) => bookView(m[1]), "books"],
    [/^\/read\/([\w-]+)\/(\d+)$/, (m) => readerView(m[1], Number(m[2])), "books"],
  ];

  let lastPath = null;

  function render() {
    const hash = location.hash.slice(1) || "/";
    const [path, query] = hash.split("?");
    const params = new URLSearchParams(query || "");
    if (path !== lastPath) {
      stopAudio();
      readerSession = null;
      if (!path.startsWith("/quiz/")) quiz = null;
      reviewRevealed = false;
      window.scrollTo(0, 0);
    }
    lastPath = path;
    let html = notFound();
    let tab = null;
    for (const [re, fn, t] of routes) {
      const m = path.match(re);
      if (m) { html = fn(m, params); tab = t; break; }
    }
    view.innerHTML = html;
    document.querySelectorAll(".tabbar a").forEach((a) => a.classList.toggle("active", a.dataset.tab === tab));
    hideSelectionButton();
    renderHeader();
  }

  // ---------- audio (browser text-to-speech) ----------
  function stopAudio() {
    if ("speechSynthesis" in window) speechSynthesis.cancel();
  }

  function toggleAudio(btn) {
    if (!("speechSynthesis" in window)) return toast("Audio isn't supported in this browser");
    if (speechSynthesis.speaking) {
      stopAudio();
      btn.textContent = "🔊 Listen";
      return;
    }
    const l = lessonById(view.querySelector(".reader").dataset.lesson);
    const text = [l.title, l.hook, ...l.sections.map((s) => `${s.heading}. ${s.body}`)].join(". ");
    const u = new SpeechSynthesisUtterance(text);
    u.rate = 1.05;
    u.onend = () => { btn.textContent = "🔊 Listen"; };
    speechSynthesis.speak(u);
    btn.textContent = "⏹ Stop";
  }

  // ---------- highlights ----------
  function addHighlight(lessonId, text, prompt) {
    text = text.trim();
    if (!text || state.highlights.some((h) => h.lessonId === lessonId && h.text === text)) return false;
    const id = Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
    state.highlights = [...state.highlights, { id, lessonId, text, prompt }];
    state.cards = { ...state.cards, [id]: L.newCard(today()) };
    state.xp += L.XP.highlight;
    checkBadges();
    save();
    toast(`🖍️ Saved to flashcards (+${L.XP.highlight} XP)`);
    return true;
  }

  const selBtn = document.getElementById("save-selection");
  function hideSelectionButton() { selBtn.hidden = true; }

  document.addEventListener("selectionchange", () => {
    const sel = window.getSelection();
    const reader = view.querySelector(".reader");
    if (!reader || !sel || sel.isCollapsed || sel.toString().trim().length < 8) return hideSelectionButton();
    if (!reader.contains(sel.anchorNode) || !reader.contains(sel.focusNode)) return hideSelectionButton();
    selBtn.hidden = false;
  });

  // Keep the selection alive when the button is pressed.
  selBtn.addEventListener("mousedown", (e) => e.preventDefault());
  selBtn.addEventListener("click", () => {
    const reader = view.querySelector(".reader");
    const text = window.getSelection().toString();
    if (reader) addHighlight(reader.dataset.lesson, text, null);
    window.getSelection().removeAllRanges();
    hideSelectionButton();
  });

  // ---------- events ----------
  view.addEventListener("click", (e) => {
    const el = e.target.closest("[data-action]");
    if (!el) return;
    const action = el.dataset.action;
    const t = today();

    if (action === "listen") return toggleAudio(el);
    if (action === "listen-book") return toggleBookAudio(el);
    if (action === "finish-chapter") return finishCurrentChapter();
    if (action === "font") {
      state.fontSize = Math.min(26, Math.max(14, (state.fontSize || 18) + Number(el.dataset.step)));
      save();
      document.documentElement.style.setProperty("--reader-size", state.fontSize + "px");
      return;
    }

    if (action === "save-section") {
      const l = lessonById(view.querySelector(".reader").dataset.lesson);
      const s = l.sections[Number(el.dataset.index)];
      if (addHighlight(l.id, s.body, s.heading)) { el.textContent = "✓ Saved"; el.disabled = true; }
      return;
    }

    if (action === "answer" && quiz && quiz.answered === null) {
      const l = lessonById(quiz.id);
      quiz.answered = Number(el.dataset.index);
      if (quiz.answered === l.quiz[quiz.index].answer) quiz.score++;
      return render();
    }

    if (action === "next-question" && quiz) {
      quiz.index++;
      quiz.answered = null;
      return render();
    }

    if (action === "short-answer" && shortAnswered === null) {
      shortAnswered = Number(el.dataset.index);
      if (shortAnswered === SHORTS[shortIndex % SHORTS.length].answer) {
        state.xp += 2;
        save();
        toast("⚡ +2 XP");
      }
      return render();
    }

    if (action === "next-short") {
      shortIndex++;
      shortAnswered = null;
      return render();
    }

    if (action === "reveal") {
      reviewRevealed = true;
      return render();
    }

    if (action === "grade") {
      const id = el.dataset.id;
      state.cards = { ...state.cards, [id]: L.reviewCard(state.cards[id], Number(el.dataset.grade), t) };
      state.reviews += 1;
      state.xp += L.XP.review;
      state = L.touchStreak(state, t);
      checkBadges();
      save();
      reviewRevealed = false;
      return render();
    }

    if (action === "start-challenge") {
      state.challenges = { ...state.challenges, [el.dataset.id]: { startDay: t, done: [] } };
      save();
      toast("🏁 Challenge started — day 1 is unlocked!");
      return render();
    }

    if (action === "delete-highlight") {
      const id = el.dataset.id;
      state.highlights = state.highlights.filter((h) => h.id !== id);
      const cards = { ...state.cards };
      delete cards[id];
      state.cards = cards;
      save();
      return render();
    }

    // Two taps to reset, so progress is never erased by accident.
    if (action === "reset") {
      if (el.dataset.armed !== "yes") {
        el.dataset.armed = "yes";
        el.textContent = "Tap again to erase all progress";
        setTimeout(() => {
          el.dataset.armed = "";
          el.textContent = "Reset all progress";
        }, 4000);
        return;
      }
      state = L.defaultState();
      save();
      toast("Progress reset");
      return render();
    }
  });

  view.addEventListener("input", (e) => {
    if (e.target.dataset.action === "book-search") {
      bookQuery = e.target.value;
      const grid = view.querySelector("#book-grid");
      if (grid) grid.innerHTML = bookGrid();
    }
  });

  view.addEventListener("change", (e) => {
    const action = e.target.dataset.action;
    if (action === "goal") {
      state.dailyGoal = Number(e.target.value);
      save();
    } else if (action === "theme") {
      applyTheme(e.target.value);
    }
  });

  function applyTheme(theme) {
    if (theme === "auto") delete document.documentElement.dataset.theme;
    else document.documentElement.dataset.theme = theme;
    try { localStorage.setItem("gamepod:theme", theme); } catch (e) { /* ignore */ }
  }

  try { applyTheme(localStorage.getItem("gamepod:theme") || "auto"); } catch (e) { /* ignore */ }

  document.documentElement.style.setProperty("--reader-size", (state.fontSize || 18) + "px");
  window.addEventListener("hashchange", render);
  render();
})();
