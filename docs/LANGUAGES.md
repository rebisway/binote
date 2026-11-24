# Interface languages

Binote's interface is available in English, French and Spanish. A language is a text file; adding one needs no change to the code. This page describes the system, the rules for the files and how to add a language. (The language of the *text* you write, which drives spelling, grammar and typography, is another thing: **Tools > Language of the project text**. The grammar engine has its own languages: see [its README](../grammar-engine/README.md).)

## The files

The files are in `binote-app/lang/` and are copied beside the executable at build time (`lang/`):

| File | `@lang` (the name shown in the menu) |
|---|---|
| `en-us.txt` | English |
| `fr-fr.txt` | Français |
| `es-es.txt` | Español |

The name of a file is the code of the language (`language-COUNTRY`, in lower case). Binote looks for the folder `lang` beside the executable, then in a few parent folders (so that an executable run from a build folder finds the sources).

## The format

One entry per line, `KEY=value`:

```
@lang=English
STATUS_COPY_NOT_SAVED=(copy of the source, not saved yet)
STATUS_DIFF=Diff: %d changed, %d added, %d removed
CONFIRM_SAVE_CHANGED="%s" was modified. Save it?
```

- The special key `@lang` is the name of the language **written in that language** (English, Français, Español). It is what the **Mode > Language** menu shows.
- The text uses `printf` placeholders (`%d`, `%s`) in the order the code gives its arguments; keep them, and their order, in a translation.
- `{\n}` in a value is a line break.
- **UTF-8 without a byte order mark.** A BOM would be read as part of the first key (`@lang`) and the language would be missing from the menu.
- **No leading or trailing spaces in a value** (editors remove them): the code adds the spaces it needs.
- A key missing from a file is shown as the key itself, which makes a missing translation easy to spot.

## The keys

**French is never used as a key.** The code only knows English identifiers (`MENU_FILE`, `BTN_SAVE`, `MSG_SAVED`...) through the macro `TR("MENU_FILE")`; French has its own file, `fr-fr.txt`, like every other language. Every visible string of the application is a key in **all** the language files: when you add a string in the code, add its key to each file.

A suffix `##id` after a key is kept as it is: it is a Dear ImGui identifier, so only the part before `##` is translated.

## Which language is used

- At the first start, the language of the system when there is a file for it (`GetUserDefaultLocaleName` on Windows, `$LANG` elsewhere).
- When there is no file for it, another country of the same language (`fr-ca` uses `fr-fr`), else French.
- Afterwards, the choice made in **Mode > Language**, saved in the preferences.

## Adding a language

1. Copy `en-us.txt` to `<code>.txt` (for example `de-de.txt`).
2. Change the first line: `@lang=Deutsch` (the name in the language itself).
3. Translate the values. Keep the keys, the placeholders and their order, and `{\n}`.
4. Save as UTF-8 without BOM.
5. Rebuild (or copy the file next to the executable in `lang/`): the language appears in **Mode > Language**.

The test bridge has the commands `langs` (lists the languages, one `code = name` per line) and `lang <code>` (switches) to check the result from a script (see [TESTING.md](TESTING.md)).

## For developers

- `common/i18n.*`: `i18n::languages()`, `i18n::load(code)`, `i18n::current()`, `i18n::tr(key)` and `TR(key)`, `i18n::fmt(TR("KEY"), args...)` (a `printf` with a translated format).
- `common/lang.*`: the dictionary and the reading of the files.
- Any new visible text: an English key in the code, a value in the three files.
