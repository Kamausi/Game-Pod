# Game-Pod

A gamified microlearning web app inspired by apps like Headway: short self-growth lessons you can read or listen to in about 5 minutes, plus a library of 91 full-length public-domain classics, all wrapped in streaks, XP, badges, flashcards and multi-day challenges.

It's a static site with no build step and no runtime dependencies. Progress is saved in the browser's `localStorage`.

## Features

- **Micro-lessons**: 12 original lessons across Productivity, Habits, Mindfulness, Money and Communication, each with a short quiz.
- **Listen mode**: lessons are read aloud with the browser's built-in text-to-speech.
- **Streaks and daily goal**: a streak counter, a 7-day activity strip and a configurable daily goal (lessons and book chapters both count).
- **XP and levels**: XP for lessons, quiz answers, book chapters, highlights, reviews and challenges.
- **Badges**: 15 achievements, such as streak milestones, perfect quizzes, finished challenges and finished books.
- **Highlights and flashcards**: select any text in a lesson, or tap 🖍️ Save on a section, to make a flashcard. Cards are scheduled with a simplified SM-2 spaced-repetition algorithm.
- **Challenges**: guided 3–5 day paths that unlock one lesson per day.
- **Shorts**: one-tap fact cards with a quick check, meant to replace a few minutes of scrolling.
- **Full-length books**: 91 public-domain classics (about 12 million words), from *Meditations*, *The Art of War* and *Walden* to *War and Peace*, *The Great Gatsby*, Shakespeare and Dante. Browse by category or search, then read chapter by chapter. The app remembers your place, can read chapters aloud, lets you adjust the text size, and turns highlights into flashcards. Each finished chapter earns XP and counts toward your streak and daily goal, and finishing a whole book earns a bonus and badges.
- **Pick of the day**: a featured lesson that changes daily.
- **Light and dark themes**: follows the system setting, or set it manually in Me → Settings.

## Run it

```sh
npm start          # serves the folder at http://localhost:8000
```

Any static file server works. Books are fetched from `books/`, so the app needs to be served over HTTP; opening `index.html` directly from disk works for everything except books.

## Test

```sh
npm test
```

The tests cover the game logic (streaks, levels, spaced repetition, challenges, books, badges), check that the lesson content is well formed, and check that every book file matches the catalog.

## Project layout

```
index.html              App shell and tab bar
css/styles.css          Styles, including light and dark themes
js/logic.js             Pure game rules (no DOM), shared with the tests
js/content.js           Lessons, challenges and shorts
js/books-catalog.js     Book catalog (generated)
js/app.js               Hash router, views and persistence
books/                  One gzipped JSON file per book (generated)
scripts/books.json      Which books to include, with category and year
scripts/build_books.py  Downloads and converts the books
tests/                  node:test unit tests
```

## Books

The books come from [Standard Ebooks](https://standardebooks.org), which publishes carefully proofread editions of works in the public domain in the United States and dedicates its own work to the public domain (CC0). Some translations may still be under copyright in other countries.

`scripts/build_books.py` (Python 3, standard library only) downloads each book's source from the Standard Ebooks GitHub repositories, splits it into chapters using the book's table of contents, groups very short sections (aphorisms, numbered verses) into one chapter, splits very long ones into parts, and writes `books/<id>.json.gz` and `js/books-catalog.js`.

To add a book, add its Standard Ebooks repository name to `scripts/books.json` with a category and year, then run:

```sh
python3 scripts/build_books.py                 # rebuild everything
python3 scripts/build_books.py <repo-name> ... # rebuild only some books
```

Downloads are cached in `scripts/.cache/`, so rebuilds are fast.

## Adding content

Add a lesson object to `LESSONS` in `js/content.js`, with an `id`, `title`, `category`, `minutes`, `hook`, `sections` and `quiz`. To build a challenge, list lesson ids in `CHALLENGES`. `npm test` checks that ids, categories and quiz answers are valid.
