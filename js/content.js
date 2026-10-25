// Original micro-lessons. Each lesson is a short set of key ideas plus a quiz.
// To add content, append to LESSONS and reference lesson ids from CHALLENGES.
(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory();
  else root.Content = factory();
})(typeof self !== "undefined" ? self : this, function () {
  const CATEGORIES = {
    productivity: { label: "Productivity", color: "#2f7de1" },
    habits: { label: "Habits", color: "#1f9d6b" },
    mindfulness: { label: "Mindfulness", color: "#8a5cd6" },
    money: { label: "Money", color: "#d18a1a" },
    communication: { label: "Communication", color: "#d6455d" },
  };

  const LESSONS = [
    {
      id: "tiny-habits",
      title: "Make Habits Tiny",
      category: "habits",
      minutes: 5,
      hook: "Big changes stick when the first step is almost too small to fail.",
      sections: [
        { heading: "Start below your motivation", body: "Motivation rises and falls. A habit sized for your best day collapses on your worst. Shrink the action until you could do it while tired: one push-up, one page, one line in a journal." },
        { heading: "Anchor it to something you already do", body: "Attach the new behavior to an existing routine: \"After I pour my coffee, I will write one sentence.\" The old routine becomes the reminder, so you don't rely on memory." },
        { heading: "Celebrate immediately", body: "A quick, genuine moment of satisfaction right after the action tells your brain the behavior is worth repeating. Feeling good wires habits faster than feeling guilty." },
        { heading: "Grow only after it's automatic", body: "Once the tiny version happens without thinking, let it expand naturally. Consistency builds identity; volume can come later." },
      ],
      quiz: [
        { q: "Why size a habit for your worst day?", options: ["Because it's more impressive", "So it survives low-motivation days", "To finish faster"], answer: 1 },
        { q: "What is an anchor?", options: ["An existing routine that triggers the new habit", "A financial reward", "A strict deadline"], answer: 0 },
      ],
    },
    {
      id: "deep-focus",
      title: "Protect Deep Focus",
      category: "productivity",
      minutes: 6,
      hook: "Your best work happens in long, uninterrupted stretches. Guard them.",
      sections: [
        { heading: "Switching has a cost", body: "Each time you jump between tasks, part of your attention lingers on the previous one. Frequent switching makes everything slower and shallower." },
        { heading: "Schedule focus blocks", body: "Put 60–90 minute blocks on your calendar for demanding work and treat them like meetings with yourself. Defend them the same way." },
        { heading: "Batch the shallow work", body: "Email, messages and admin expand to fill any gap. Handle them in a few set windows rather than continuously." },
        { heading: "Design your environment", body: "Remove the easiest distractions before you start: phone in another room, notifications off, one window open." },
      ],
      quiz: [
        { q: "What happens when you switch tasks often?", options: ["Attention fully resets", "Attention lingers on the old task", "Nothing measurable"], answer: 1 },
        { q: "A good way to handle email is to…", options: ["Check it continuously", "Batch it into set windows", "Never read it"], answer: 1 },
      ],
    },
    {
      id: "two-minute-rule",
      title: "The Two-Minute Rule",
      category: "productivity",
      minutes: 4,
      hook: "If it takes less than two minutes, do it now.",
      sections: [
        { heading: "Small tasks pile up", body: "Quick tasks feel harmless to postpone, but dozens of them create a constant background hum of unfinished business." },
        { heading: "Do, don't track", body: "Writing down and revisiting a two-minute task often takes longer than just doing it. Act immediately and move on." },
        { heading: "Use it to start bigger things", body: "For larger goals, commit only to the first two minutes: open the document, lace your shoes. Starting is usually the hardest part." },
      ],
      quiz: [
        { q: "What should you do with a task that takes under two minutes?", options: ["Add it to a list", "Do it right away", "Delegate it"], answer: 1 },
        { q: "How can the rule help with big goals?", options: ["By committing to just the first two minutes", "By skipping them", "By doing them in two minutes total"], answer: 0 },
      ],
    },
    {
      id: "breathing-reset",
      title: "The 90-Second Reset",
      category: "mindfulness",
      minutes: 4,
      hook: "Slow breathing is the fastest remote control for your nervous system.",
      sections: [
        { heading: "Name what you feel", body: "Labeling an emotion — \"this is frustration\" — creates a little distance from it and makes it easier to manage." },
        { heading: "Lengthen the exhale", body: "Breathe in for about four counts and out for six. A longer exhale signals safety to your body and slows your heart rate." },
        { heading: "Return to the next small action", body: "After a few breaths, ask: what's the one useful thing I can do next? Calm is a launch pad, not the destination." },
      ],
      quiz: [
        { q: "Which part of the breath calms you most?", options: ["A longer inhale", "A longer exhale", "Holding your breath"], answer: 1 },
        { q: "Why label an emotion?", options: ["To create distance from it", "To make it stronger", "To ignore it"], answer: 0 },
      ],
    },
    {
      id: "gratitude-practice",
      title: "Train Your Attention with Gratitude",
      category: "mindfulness",
      minutes: 4,
      hook: "What you look for, you find more of.",
      sections: [
        { heading: "Your brain filters reality", body: "You can't notice everything, so attention favors what you've practiced looking for. Regularly noticing good things makes them easier to spot." },
        { heading: "Be specific", body: "\"My friend checked in when I was stressed\" works better than \"I'm grateful for friends.\" Specific details make the feeling real." },
        { heading: "Keep it light", body: "Three items, a few times a week, is plenty. Forcing it daily can turn it into a chore." },
      ],
      quiz: [
        { q: "Which gratitude note is more effective?", options: ["I'm grateful for life", "My coworker covered for me when I was sick", "Everything is fine"], answer: 1 },
      ],
    },
    {
      id: "pay-yourself-first",
      title: "Pay Yourself First",
      category: "money",
      minutes: 5,
      hook: "Save before you spend, not from what's left over.",
      sections: [
        { heading: "Leftovers rarely exist", body: "Spending tends to rise to match income. If saving waits until the end of the month, there's usually nothing left." },
        { heading: "Automate it", body: "Set an automatic transfer to savings on payday. What you never see in your checking account, you don't miss." },
        { heading: "Start with a percentage", body: "Pick a rate you can sustain — even 5% — and raise it by a point whenever your income goes up." },
        { heading: "Build a buffer first", body: "A small emergency fund keeps surprises from turning into debt, which protects every other goal." },
      ],
      quiz: [
        { q: "When should saving happen?", options: ["At the end of the month", "Automatically on payday", "Only after big purchases"], answer: 1 },
        { q: "Why build an emergency fund early?", options: ["So surprises don't become debt", "To earn high returns", "It's required by banks"], answer: 0 },
      ],
    },
    {
      id: "compound-growth",
      title: "The Quiet Power of Compounding",
      category: "money",
      minutes: 5,
      hook: "Small gains on top of gains become large over time.",
      sections: [
        { heading: "Growth on growth", body: "Compounding means returns earn their own returns. Early on the effect is invisible; later it dominates." },
        { heading: "Time beats timing", body: "Starting earlier usually matters more than picking the perfect moment or the perfect investment." },
        { heading: "It works for skills too", body: "Getting 1% better at something regularly compounds the same way. Knowledge builds on knowledge." },
      ],
      quiz: [
        { q: "What's usually the biggest lever in compounding?", options: ["Time", "Luck", "Perfect timing"], answer: 0 },
        { q: "Compounding also applies to…", options: ["Only money", "Skills and knowledge", "Nothing else"], answer: 1 },
      ],
    },
    {
      id: "active-listening",
      title: "Listen to Understand",
      category: "communication",
      minutes: 5,
      hook: "Most people listen to reply. Great communicators listen to understand.",
      sections: [
        { heading: "Pause your rebuttal", body: "While someone talks, notice when you start preparing your response. Set it aside and keep listening." },
        { heading: "Reflect back", body: "Summarize what you heard: \"So the main issue is the deadline, not the scope?\" It shows attention and catches misunderstandings early." },
        { heading: "Ask open questions", body: "Questions starting with \"what\" or \"how\" invite people to explain. Yes/no questions tend to close conversations." },
      ],
      quiz: [
        { q: "What does reflecting back accomplish?", options: ["It wins the argument", "It shows attention and catches misunderstandings", "It ends the conversation"], answer: 1 },
        { q: "Which is an open question?", options: ["Did you finish?", "How did it go?", "Is it done?"], answer: 1 },
      ],
    },
    {
      id: "clear-asks",
      title: "Make Clear Requests",
      category: "communication",
      minutes: 4,
      hook: "Vague asks get vague results.",
      sections: [
        { heading: "Say what, who and when", body: "\"Can you look at this?\" leaves too much open. \"Can you review section 2 by Thursday noon?\" gets action." },
        { heading: "Share the why", body: "A sentence of context helps people make good judgment calls without coming back to you." },
        { heading: "Make it easy to say yes", body: "Attach what they need, suggest a format and keep the request short." },
      ],
      quiz: [
        { q: "Which request is clearest?", options: ["Thoughts?", "Can you look at this sometime?", "Can you review section 2 by Thursday noon?"], answer: 2 },
      ],
    },
    {
      id: "weekly-review",
      title: "The Weekly Review",
      category: "productivity",
      minutes: 5,
      hook: "Thirty minutes a week keeps your whole system trustworthy.",
      sections: [
        { heading: "Empty your head", body: "Write down every open loop — tasks, worries, ideas. A mind that's trying to remember can't think clearly." },
        { heading: "Look back", body: "What went well? What slipped? A quick honest look at last week is the best predictor of next week." },
        { heading: "Pick your top three", body: "Choose the three outcomes that would make next week a success, and block time for them first." },
      ],
      quiz: [
        { q: "How many priorities should you pick for the week?", options: ["Three", "Ten", "As many as possible"], answer: 0 },
      ],
    },
    {
      id: "environment-design",
      title: "Design Your Environment",
      category: "habits",
      minutes: 4,
      hook: "Make good choices the easy ones.",
      sections: [
        { heading: "Friction shapes behavior", body: "We tend to do what's convenient. Adding a few seconds of effort to a bad habit, or removing it from a good one, changes behavior more than willpower." },
        { heading: "Make cues visible", body: "Leave the book on your pillow, the water bottle on your desk, the guitar out of its case." },
        { heading: "Hide the temptations", body: "Log out of distracting apps, keep snacks out of sight, put the remote in a drawer." },
      ],
      quiz: [
        { q: "What usually changes behavior more reliably than willpower?", options: ["Adjusting friction", "Trying harder", "Waiting for motivation"], answer: 0 },
      ],
    },
    {
      id: "single-tasking",
      title: "Be Here Now",
      category: "mindfulness",
      minutes: 4,
      hook: "Doing one thing at a time is a form of meditation.",
      sections: [
        { heading: "Notice drift", body: "Your mind will wander. The skill isn't preventing it — it's noticing and gently returning." },
        { heading: "Use everyday anchors", body: "Washing dishes, walking, drinking tea: give one ordinary activity your full attention each day." },
        { heading: "Less rush, more done", body: "Presence reduces mistakes and rework, so single-tasking often feels slower but finishes sooner." },
      ],
      quiz: [
        { q: "What's the core skill in mindfulness practice?", options: ["Never letting the mind wander", "Noticing drift and returning", "Thinking about nothing"], answer: 1 },
      ],
    },
  ];

  const CHALLENGES = [
    {
      id: "habit-kickstart",
      title: "Habit Kickstart",
      icon: "🌱",
      desc: "Five days to build a habit that actually sticks.",
      lessons: ["tiny-habits", "environment-design", "two-minute-rule", "weekly-review", "compound-growth"],
    },
    {
      id: "calm-mind",
      title: "Calm Mind",
      icon: "🧘",
      desc: "Five days of simple practices for stress and focus.",
      lessons: ["breathing-reset", "single-tasking", "gratitude-practice", "deep-focus", "active-listening"],
    },
    {
      id: "money-basics",
      title: "Money Basics",
      icon: "💰",
      desc: "Three days to set up better money habits.",
      lessons: ["pay-yourself-first", "compound-growth", "tiny-habits"],
    },
  ];

  // Shorts: bite-sized facts with a one-tap check.
  const SHORTS = [
    { fact: "Writing a task down frees working memory, which can make it easier to focus on what's in front of you.", q: "Writing tasks down helps by…", options: ["Freeing working memory", "Making tasks shorter"], answer: 0 },
    { fact: "A longer exhale than inhale activates the body's relaxation response.", q: "To calm down, make which part longer?", options: ["Inhale", "Exhale"], answer: 1 },
    { fact: "Reviewing something just before you'd forget it strengthens memory more than rereading it immediately.", q: "When is review most effective?", options: ["Right after learning", "Just before you'd forget"], answer: 1 },
    { fact: "Most habits are triggered by a cue: a time, place, emotion, person or preceding action.", q: "What triggers a habit?", options: ["A cue", "A goal"], answer: 0 },
    { fact: "Saving even a small percentage automatically tends to beat larger, irregular manual saving.", q: "Which tends to work better?", options: ["Small automatic saving", "Large saving when you remember"], answer: 0 },
    { fact: "Summarizing what someone said before replying reduces misunderstandings.", q: "Before replying, it helps to…", options: ["Summarize what you heard", "Prepare a counterpoint"], answer: 0 },
  ];

  return { CATEGORIES, LESSONS, CHALLENGES, SHORTS };
});
