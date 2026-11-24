# typo

Automatic typography driven by profiles, one profile per language: quotation marks, spaces before the signs, apostrophe, ellipsis, dashes. C++17, no dependency but the standard library. MIT ([LICENSE](LICENSE)).

It is the engine of the automatic typography of [Binote](../README.md), and it does not depend on it.

## Two ways to use it

- **While typing**: `Typo::OnTyped` receives the character which was just typed and the line up to the caret, and tells what to replace.
- **On a text**: `Typo::FixLine` and `Typo::FixText` apply every rule to a line or to a whole text. Both ways give the same result.

```cpp
#include "typo.h"

std::string error;
TypoProfile* fr = Typo::CreateProfile(profile_text, nullptr, &error);   // NULL and an error message if it cannot be read

TypoOptions options;                                                    // QuoteStyle (Local, Straight, Curly), Markdown
std::string fixed = Typo::FixText(fr, options, text);

// In an editor, after each typed character:
TypoEdit e = Typo::OnTyped(fr, options, typed, line_up_to_the_caret);
if (e.Valid)
    replace(line, e.Start, e.Length, e.Text);                           // then the caret goes after e.Text, plus e.CaretExtra bytes

Typo::DestroyProfile(fr);
```

Strings are UTF-8, offsets are byte offsets. With `options.Markdown` the code spans and the fenced code blocks are left alone. `QuoteStyle_Straight` keeps the straight quotes, `QuoteStyle_Curly` uses the alternative marks of the profile.

## The profiles

A profile is a text file of the INI kind (`profiles/*.typo`). `#` starts a comment. A value is `U+XXXX`, `space`, `tab`, `letter`, `digit`, `none` or the character itself.

```ini
[profile]
name = French
extends = other          # the settings of another profile (optional); [space_before] is replaced, not merged

[quotes]
open = «                 # the marks of the language, "none" to leave the quotes alone
close = »
gap_open = U+202F        # the space inside the marks
gap_close = U+202F
alt_open = “             # the curly quotes, for QuoteStyle_Curly
alt_close = ”
open_after = space tab ( [ – — « “     # a straight quote which follows one of these opens a quotation
open_after_typed = space tab ( [ – —   # the same while typing

[apostrophe]
char = ’                 # between two letters

[ellipsis]
char = …                 # three dots

[dash]
spaced_double_hyphen = – # " -- " between two spaces

[space_before]
; = U+202F closers       # sign = the space put before it (and "closers": a closing sign may follow it)
! = U+202F closers
? = U+202F closers
: = U+00A0

[inverted]
? = ¿                       # the mark put at the start of the sentence when it ends with the sign (Spanish)
! = ¡

[context]
prev = letter digit ) ] » … ’ * _ "    # what a sign may follow for the space to be put before it
prev_typed = letter digit ) ] » … ’
closers = ) " » * _ ]
```

Inverted marks: a sentence which ends with `?` or `!` and has no mark at its start gets one, at the start of the sentence (after `. ! ? …`, or after an opening `(`, `«`, `“`). A group such as `?!` and a word such as `a?b` are left alone; a question in the middle of a sentence ("Si vienes, ¿qué...?") is kept when you type the `¿` yourself.

Included: `fr.typo` (French), `en.typo`, `es.typo` (Spanish, with the inverted marks) and `generic.typo` (for a language without a profile: nothing but the curly quotes when asked). A profile which cannot be read gives an error with its line number.

In Binote, the profile of the language of the project is looked for in `%AppData%\Binote\typography`, then in the `typography` folder beside the program. A profile placed in the first one replaces the one shipped. `fr-FR` gives `fr-fr.typo`, then `fr.typo`, then `generic.typo`.

## Tests

```
typo_test <folder of the profiles> <folder of the cases>
```

The cases are in `tests/cases/<profile>.txt`, one per line (`fix`, `fixmd`, `fix1`, `fix2`, `type`, `type2`: see the header of `tests/typo_test.cpp`). With CMake: `ctest`. Fixing a fixed text must not change it: the test checks it for every case.
