# imgui-md

Markdown renderer for Dear ImGui. MIT. Depends only on Dear ImGui and md4c (vendored in `third/md4c`).

## Status

Working: headings, bold, italic, strikethrough, inline and block code, bullet / numbered / task lists (nested),
quotes, links (callback), rules, tables, images (callback), `&nbsp;` and numeric entities, UTF-8 BOM.
Layout is cached per width and only visible blocks are drawn (an 18-chapter novel, ~50 KB per chapter, scrolls fine).

Editor hooks (source lines, 1-based): `ImGuiMdState::HighlightLine`, `ScrollToLine`, `ClickedLine`.

Also working: table column alignment, baseline alignment of mixed fonts, faked bold / italic when a font is missing,
tooltips (link title or URL, image title or alt text; `ImGuiMdConfig::ShowTooltips`), images loaded through
`ImageCallback` (the demo loads PNG/JPEG next to the Markdown file with stb_image), links underlined across spaces.

Diagrams: `ImGuiMdConfig::CodeBlockCallback` lets the host render fenced code blocks that have a language.
`imgui_md_diagram.h` is a small separate module, written from scratch, that draws a subset of Mermaid
(flowcharts `graph` / `flowchart` TD, LR, BT, RL; sequence diagrams) with the ImGui draw list. Hook it with
`ImGuiMdDiagrams::CodeBlock(cache, language, text, ...)` as the demo does. A block it cannot parse falls back to the normal code block.
Limits: no subgraphs or styling, two opposite edges between the same nodes overlap.

Not done yet: text selection / copy, footnotes, HTML blocks (ignored), per-line alignment inside a table cell.

## Use

```cpp
ImGuiMdDocument* doc = ImGuiMd::CreateDocument();
ImGuiMd::SetText(doc, text);            // parse once per text change
ImGuiMdConfig config;                   // fonts, colors, callbacks
ImGuiMdState state;                     // optional editor interaction
ImGuiMd::Render(doc, config, &state);   // every frame, inside a window or child
```

Fonts: load a range covering your text (Latin-1 + U+2000..U+206F for `’ … – —` and U+202F). The demo shows how.

## Demo

`cmake -S . -B build -G Ninja && cmake --build build`, then
`imgui_md_demo file.md [--line N] [--shot out.png --frames 8]`.
