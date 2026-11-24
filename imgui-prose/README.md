# imgui-prose

Multiline text editor for prose, for Dear ImGui. MIT. Depends only on Dear ImGui.

## Status

Working: word wrap (optional, horizontal scroll otherwise), line numbers, paragraph numbers (a paragraph is a non-empty line),
current paragraph highlight (background and/or text color), UTF-8, caret, selection (keys, mouse drag, double click = word,
triple click = paragraph), clipboard, undo / redo with grouping, read-only mode, markers of the no-break spaces (U+00A0, U+202F) and, with `ShowInvisible`, of every invisible typographic character (thin, wide and zero-width spaces, soft hyphen, direction marks), one colour per kind,
decorations given by the host (`ImGuiProseSpan`: backgrounds, wavy underlines, underlines, text colors),
`TypedCallback` after typed characters (typography), scroll synchronisation between two editors by paragraph.
A 600 KB text (6000 lines) loads and scrolls without trouble.

Not done yet: input method editors (CJK), bidirectional text, rectangular selection, find / replace (the host does it with
`Replace` and `SetSpans`), drag and drop of text, spans following the edits (the host sets them again when `GetVersion` changes).

## Use

```cpp
ImGuiProseEditor* editor = ImGuiProse::CreateEditor();
ImGuiProse::SetText(editor, text);
ImGuiProseConfig config;                    // colors, wrap, numbers, highlight, callbacks
bool changed = ImGuiProse::Render(editor, "##text", config, ImVec2(0, 0));   // every frame
```

Scroll synchronisation of a source editor and a destination editor:

```cpp
if (ScrolledByUser(a) && !ScrolledByUser(b))
    SetScrollAnchor(b, GetScrollAnchor(a));  // same paragraph at the top of both, even if the texts differ in length
```

Fonts: load a range covering your text (Latin-1, U+2000..U+206F, and the symbols you use). Put the font to use in
`ImGuiProseConfig::Font` (a proportional font for prose, a monospaced one for plain text).

## Demo and tests

`cmake -S . -B build -G Ninja && cmake --build build`, then `imgui_prose_demo file.md`.
`--script file` drives the demo like a user (click, drag, type, key, wheel, shot, dump, log, set); see the header of `demo/main.cpp`.
