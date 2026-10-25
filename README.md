# Game-Pod

A gamified microlearning web app inspired by apps like Headway: short self-growth lessons you can read or listen to in about 5 minutes, wrapped in streaks, XP, badges, flashcards and multi-day challenges.

It's a static site with no build step and no dependencies. Progress is saved in the browser's `localStorage`.

## Features

- **Micro-lessons**: 12 original lessons across Productivity, Habits, Mindfulness, Money and Communication, each with a short quiz.
- **Listen mode**: lessons are read aloud with the browser's built-in text-to-speech.
- **Streaks and daily goal**: a streak counter, a 7-day activity strip and a configurable daily lesson goal.
- **XP and levels**: XP for lessons, quiz answers, highlights, reviews and challenges.
- **Badges**: 11 achievements, such as streak milestones, perfect quizzes and finished challenges.
- **Highlights and flashcards**: select any text in a lesson, or tap 🖍️ Save on a section, to make a flashcard. Cards are scheduled with a simplified SM-2 spaced-repetition algorithm.
- **Challenges**: guided 3–5 day paths that unlock one lesson per day.
- **Shorts**: one-tap fact cards with a quick check, meant to replace a few minutes of scrolling.
- **Pick of the day**: a featured lesson that changes daily.
- **Light and dark themes**: follows the system setting, or set it manually in Me → Settings.

## Run it

```sh
npm start          # serves the folder at http://localhost:8000
```

Any static file server works, and so does opening `index.html` directly.

## Test

```sh
npm test
```

The tests cover the game logic (streaks, levels, spaced repetition, challenges, badges) and check that the lesson content is well formed.

## Project layout

```
index.html         App shell and tab bar
css/styles.css     Styles, including light and dark themes
js/logic.js        Pure game rules (no DOM), shared with the tests
js/content.js      Lessons, challenges and shorts
js/app.js          Hash router, views and persistence
tests/             node:test unit tests
```

## Adding content

Add a lesson object to `LESSONS` in `js/content.js`, with an `id`, `title`, `category`, `minutes`, `hook`, `sections` and `quiz`. To build a challenge, list lesson ids in `CHALLENGES`. `npm test` checks that ids, categories and quiz answers are valid.
