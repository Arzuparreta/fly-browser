# Spell Checking

Fly checks spelling in the text fields of web pages in up to five languages at
once. A word counts as correct when any of your languages has it, so you can
write English and Spanish in the same field without switching languages.

## Choose your languages

Open **Preferences → General → Spelling**:

- **Check spelling** turns checking on or off.
- **Add…** lists every language that has a dictionary (47) and adds the one you
  pick at the end of the list.
- **Up** moves a language one place up; **Remove** takes it out.

The list applies to every workspace. Put the language you write most at the
top: Fly checks it first and lists its suggestions first.

Fly downloads each dictionary from Google (`redirector.gvt1.com`) the first time
it is needed and keeps it in the `Dictionaries` folder of your Fly data
directory. The text you type is checked on your computer. While a dictionary
downloads, its language shows **Downloading…**; when the download fails,
**Retry** starts it again.

## Suggestions

Right-click an underlined word for up to five suggestions. With several
languages, every language first offers its close corrections: one letter
swapped, missing, extra or wrong. A language then searches its whole
dictionary for similar words only when it has close corrections of its own,
or when no language has any. A Spanish typo therefore gets Spanish
suggestions first, not English words that merely look alike, and the menu
opens sooner. With one language, the suggestions are Chromium's.

Right-click a correctly spelled word in a text field for **Spell check**:
**Use basic spell check** is the same switch as **Check spelling** in
Preferences, and **Use enhanced spell check** sends what you type to Google
(off unless you turn it on). Spelling languages switched on or off in
`chrome://settings/languages` also become Fly's choice for every workspace.

## Why five

Each language you add costs something, and it is measured below:

- **Suggestions.** Every language that does not know the word offers its
  corrections. Without Fly's order of close corrections first, each foreign
  dictionary is searched in full, which takes most of the time.
- **Checking while you type.** A word is looked up in the next language only
  when the earlier ones reject it, so the first language matters: with the
  language of the text last instead of first, checking took about seven times
  longer.
- **Memory.** Each language takes 0.2 to 5.5 MB in every page process that
  checks text, besides its dictionary file, which those processes share.
- **Typos that pass.** A typo that is a real word in one of your languages is
  not underlined. In Spanish text with 2% typos, five languages underlined 12%
  fewer words than English and Spanish alone.
- **The menu shows five suggestions**, so at five languages each still gets
  one; a sixth could be left without any.

Measured with Fly's release build on a Ryzen 7 5800X: checking 6,000 words of
English or Spanish with 2% swapped letters, and right-clicking 60 English and 60
Spanish typos with one mistake and 120 with two. Languages were added in this
order: English (UK), Spanish, French, German, Italian. Suggestion times are the
mean of the English and Spanish typos, median of three runs; the slowest tenth
took about twice as long.

| Languages | Checking, µs per word | Suggestions, one mistake: Chromium → Fly | Two mistakes: Chromium → Fly | Memory per page process |
| --- | --- | --- | --- | --- |
| 1 | 1.9 | 22 ms (unchanged) | 17 ms (unchanged) | 1.0 MB |
| 2 | 2.4 | 46 → 26 ms | 42 → 37 ms | 2.8 MB |
| 3 | 4.3 | 131 → 98 ms | 132 → 124 ms | 5.1 MB |
| 4 | 4.8 | 296 → 109 ms | 303 → 258 ms | 5.8 MB |
| 5 | 5.2 | 317 → 118 ms | 336 → 282 ms | 6.7 MB |

A sixth language (Portuguese) took 166 ms per suggestion and brought the memory
to 12.2 MB. With two languages, the first suggestion for a Spanish typo was the
right word in 4 of 60 cases before Fly's order and in 58 after; the right word
was among the five suggestions as often as before.
