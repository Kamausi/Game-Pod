// UI layer: hash router, views and persistence. Game rules live in logic.js.
(function () {
  const { LESSONS, CATEGORIES, CHALLENGES, SHORTS } = window.Content;
  const L = window.Logic;
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
    return { perfectQuizzes, finishedChallenges };
  }

  // Award any newly earned badges and announce them.
  function checkBadges() {
    const res = L.awardBadges(state, badgeContext());
    state = res.state;
    res.earned.forEach((b, i) => setTimeout(() => toast(`${b.icon} Badge unlocked: ${b.name}`), 1200 * (i + 1)));
  }

  let toastTimer;
  function toast(msg) {
    const t = document.getElementById("toast");
    t.textContent = msg;
    t.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.remove("show"), 2200);
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
    const doneToday = L.lessonsOnDay(state, t);
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

      <h2>✨ Pick of the day</h2>
      ${lessonCard(pick)}

      ${due ? `<h2>🧠 Ready to review</h2>
        <a class="card" href="#/review"><div class="row spread"><div><h3>${due} flashcard${due === 1 ? "" : "s"} due</h3><div class="muted small">A quick review locks ideas into memory.</div></div><span class="btn primary">Review</span></div></a>` : ""}

      <h2>🏁 Challenges</h2>
      ${active.length ? active.map(challengeCard).join("") : `<a class="card" href="#/challenges"><h3>Start a challenge</h3><div class="muted small">Guided paths that unlock one lesson a day.</div></a>`}

      ${nextUp.length ? `<h2>📖 Up next</h2><div class="grid">${nextUp.map(lessonCard).join("")}</div>` : ""}
    `;
  }

  function libraryView(params) {
    const filter = params.get("c") || "all";
    const list = filter === "all" ? LESSONS : LESSONS.filter((l) => l.category === filter);
    const btn = (key, label) => `<a class="btn small ${filter === key ? "active" : ""}" href="#/library?c=${key}">${esc(label)}</a>`;
    const done = Object.keys(state.completed).length;
    return `
      <h1>Library</h1>
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
      <a href="#/library" class="muted small">← Library</a>
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
    const l = lessonById(h.lessonId);
    return `
      <h1>Review</h1>
      <p class="muted">${due.length} card${due.length === 1 ? "" : "s"} due</p>
      <div class="card flashcard">
        <div class="muted small">${l ? esc(l.title) : ""}</div>
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
      </div>

      <h2>Badges (${state.badges.length}/${L.BADGES.length})</h2>
      <div class="grid">
        ${L.BADGES.map((b) => `<div class="card badge ${state.badges.includes(b.id) ? "" : "locked"}">
          <div class="icon">${b.icon}</div><h3>${esc(b.name)}</h3><div class="muted small">${esc(b.desc)}</div></div>`).join("")}
      </div>

      <h2>Highlights (${state.highlights.length})</h2>
      ${state.highlights.length ? state.highlights.map((h) => {
        const l = lessonById(h.lessonId);
        return `<div class="card"><div class="row spread"><span class="muted small">${l ? esc(l.title) : ""}</span>
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
  ];

  let lastPath = null;

  function render() {
    const hash = location.hash.slice(1) || "/";
    const [path, query] = hash.split("?");
    const params = new URLSearchParams(query || "");
    if (path !== lastPath) {
      stopAudio();
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

    if (action === "reset" && confirm("Reset all progress? This can't be undone.")) {
      state = L.defaultState();
      save();
      return render();
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

  window.addEventListener("hashchange", render);
  render();
})();
