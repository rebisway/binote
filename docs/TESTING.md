# Testing Binote

Three kinds of tests: the unit tests of the grammar engine, the interface tests driven by the **test bridge**, and the screenshots of the manual (taken with the same bridge).

## The grammar engine

```
cd grammar-engine
python -m unittest discover -s tests
```

The tests need only Python 3.9 or later. They have, for each rule, cases which must be reported and cases which must not, and they exercise the server mode (in process and as a real `--serve` process). The engine can also be run alone: `python -m grammar_engine --lang fr < text.txt`.

## The interface: the test bridge

The bridge (`binote-app/src/ui/bridge.{h,cpp}`) drives the running application like a user: mouse, keys, text, screenshots. It plays a **script** one command per line, one action per frame:

```
binote.exe --script test.txt [--width W] [--height H] [file or folder]
```

A line starting with `#` is a comment. Blank lines are ignored.

### Commands of the bridge

| Command | Effect |
|---|---|
| `wait N` | wait N frames |
| `move X Y` | move the mouse to X, Y (pixels of the window) |
| `click X Y [count]` | left click (`count` times) |
| `rclick X Y` | right click |
| `drag X1 Y1 X2 Y2` | drag with the left button |
| `type TEXT` | type the rest of the line as text (UTF-8) |
| `key NAME [ctrl] [shift] [alt]` | press a key with modifiers (`a`, `F5`, `enter`, `backspace`, `delete`, `tab`, `escape`, `left`, `right`, `up`, `down`, `home`, `end`, `pageup`, `pagedown`, `space`, `minus`, `equal`, `kpadd`, `kpsub`...) |
| `wheel DY` | mouse wheel |
| `shot FILE` | write the framebuffer as a PNG |
| `log TEXT` | print a line on the standard output |
| `quit` | quit the application |

### Commands of the application (`App::command`)

Any other command goes to the application. Arguments are split on spaces; a path or text with spaces is written between double quotes.

| Command | Effect |
|---|---|
| `open FILE [DST]`, `openfolder FOLDER [DST]` | open a file or a folder project |
| `sysopen PATH`, `sysmode N` | open as the system does ("Open with"); the mode of opening files given by the system |
| `save`, `savestate` | save the destination; save the state of the session |
| `doc N` | go to the chapter N |
| `goto N [p]` | go to the line N (or the paragraph N with `p`) |
| `find TEXT` | open find and go to the next match (quote a text with spaces) |
| `findopts REGEX CASE WORD FIND [REPLACE]` | set the options (0 or 1 each) and the texts; prints `matches=N error=...` |
| `replace one\|all\|project` | replace |
| `lang CODE`, `langs` | switch the interface language; list the languages |
| `plang CODE` | set the language of the project text (**writes `binote.json`**: never on the repository) |
| `theme dark\|light`, `reading on\|off`, `tree on\|off`, `preview src\|dst on\|off` | display |
| `set wrap\|diff\|sync\|nbsp\|invisible\|lines\|paras\|autotypo\|mono\|markdown\|log on\|off`, `set fontsize N` | modes |
| `font ui\|mono\|editor NAME` | choose a font |
| `typo [all]` | fix the typography of the paragraph, or of the chapter |
| `untypo [all]` | the opposite: plain quotes, apostrophes, dots and spaces, in the paragraph or the chapter |
| `toc` | generate the table of contents |
| `spell on\|off` | spell checker |
| `grammar "TEXT"` | print the grammar issues of a paragraph: rule, range, text, suggestion |
| `grammarfile FILE [LIMIT]` | check every paragraph of a file: a sample with context, then the number of issues by rule |
| `problems`, `charmap`, `about`, `export`, `prefs`, `names`, `shownotes`, `stats`, `history [N]`, `vcs`, `scripteditor` | open the window |
| `script NAME`, `scriptok` | start a script by its name; confirm its dialog |
| `style`, `stylehl on\|off` | run the style analysis and print its measures; colour the passages |
| `note "TEXT"` | add a note on the current paragraph |
| `vcscommit "MESSAGE"` | commit in the working folder of the project |
| `anchors` | print the scroll anchors of the two panes |
| `status` | print the font sizes, the document, the dirty flag, the paragraph counts, the caret, the differences and the headings |
| `dump FILE` | write the text of the destination to a file |

An unknown command is reported. Output goes to the standard output.

### Rules for tests

- **Never write into `exemples/`.** The sample project `exemples/la_seizieme_figure` (18 chapters, about 600 KB, with accents, « », no-break spaces) is read-only: copy it elsewhere to test saves.
- **Never run `plang` on the repository**: it rewrites `binote.json`. Work on a copy of `docs/sample`.
- The application keeps its preferences in `%AppData%\Binote\settings.ini`; tests must not change it on purpose.

### Example

```
# test.txt: open a copy of the sample, check one paragraph, take a screenshot
openfolder "C:\temp\sample"
wait 30
grammar "Les chat dorment."
set wrap on
shot "C:\temp\shot.png"
quit
```

```
binote-app\build\binote.exe --script test.txt --width 1280 --height 800
```

## Screenshots of the manual

The images in `docs/images` are taken with `shot` on the sample project `docs/sample` (an excerpt of *Alice's Adventures in Wonderland*, public domain), with the interface in English. Open the window with its command (`problems`, `stats`, `history`...), `wait` a few frames, `shot`.

## Testing the components

`imgui-md` and `imgui-prose` have a demo each (`demo/main.cpp`, built by their own CMake file with the option `IMGUI_MD_BUILD_DEMO` / `IMGUI_PROSE_BUILD_DEMO`): they are tested through their demos, not through Binote.

## Testing in the application the grammar engine

With Python installed, the commands `grammar TEXT` and `grammarfile FILE` of the bridge call the engine through the bridge module; the output shows the rule identifiers. For a text file of your own: `grammarfile "C:\path\text.md" 60`.
