// imgui_md : Markdown rendering for Dear ImGui (MIT license)
// Dependencies: Dear ImGui, md4c.

// Index of this file:
// [SECTION] Forward declarations and configuration
// [SECTION] Structures
// [SECTION] Functions

#pragma once
#include "imgui.h"

//-----------------------------------------------------------------------------
// [SECTION] Forward declarations and configuration
//-----------------------------------------------------------------------------

#ifndef IMGUI_MD_API
#define IMGUI_MD_API
#endif

struct ImGuiMdDocument;                 // Parsed document. Parse once per text change, render every frame.
struct ImGuiMdFonts;
struct ImGuiMdColors;
struct ImGuiMdConfig;
struct ImGuiMdState;

//-----------------------------------------------------------------------------
// [SECTION] Structures
//-----------------------------------------------------------------------------

struct ImGuiMdFonts
{
    ImFont*     Regular;                // NULL = current font
    ImFont*     Bold;                   // NULL = Regular
    ImFont*     Italic;                 // NULL = Regular
    ImFont*     BoldItalic;             // NULL = Bold
    ImFont*     Code;                   // NULL = Regular
    ImFont*     Heading[6];             // h1..h6. NULL = Bold

    ImGuiMdFonts()                      { memset(this, 0, sizeof(*this)); }
};

struct ImGuiMdColors
{
    ImU32       Text;                   // 0 = ImGuiCol_Text
    ImU32       Link;
    ImU32       CodeBg;
    ImU32       QuoteBar;
    ImU32       Rule;
    ImU32       HighlightBg;            // Background of the block holding ImGuiMdState::HighlightLine

    ImGuiMdColors()
    {
        Text = 0;
        Link = IM_COL32(80, 160, 255, 255);
        CodeBg = IM_COL32(128, 128, 128, 50);
        QuoteBar = IM_COL32(128, 128, 128, 160);
        Rule = IM_COL32(128, 128, 128, 120);
        HighlightBg = IM_COL32(255, 220, 80, 70);
    }
};

struct ImGuiMdConfig
{
    ImGuiMdFonts    Fonts;
    ImGuiMdColors   Colors;
    float           IndentSpacing;      // Horizontal indentation of lists and quotes
    float           ParagraphSpacing;   // In multiples of the font size
    float           LineSpacing;        // Height of a row of text, in multiples of the font size
    bool            SoftBreakAsNewLine; // A single line break inside a paragraph starts a new line instead of a space
    bool            ShowTooltips;       // Tooltip with the title (or the URL) of the hovered link, the title (or alt text) of the hovered image
    void            (*LinkCallback)(const char* url, void* user_data);                          // Called when a link is clicked
    ImTextureID     (*ImageCallback)(const char* url, ImVec2* out_size, void* user_data);       // Returns 0 to draw the alt text instead
    // Optional renderer of fenced code blocks with a language (```mermaid ...). Called to measure (draw = false) then to draw (draw = true)
    // at 'pos' (screen space), with the available width. Returns the height used, or a negative value to get the default code block.
    // It runs for every visible block at every frame: cache the layout on your side.
    float           (*CodeBlockCallback)(const char* language, const char* text, int text_length, float width, bool draw, ImVec2 pos, void* user_data);
    void*           UserData;

    ImGuiMdConfig()
    {
        IndentSpacing = 22.0f;
        ParagraphSpacing = 0.6f;
        LineSpacing = 1.35f;
        SoftBreakAsNewLine = false;
        ShowTooltips = true;
        LinkCallback = NULL;
        ImageCallback = NULL;
        CodeBlockCallback = NULL;
        UserData = NULL;
    }
};

// Interaction with an editor displayed beside the preview. Lines are 1-based source lines.
struct ImGuiMdState
{
    int         HighlightLine;          // [In]  Highlight the block containing this source line (0 = none)
    int         ScrollToLine;           // [In]  Scroll to make this source line visible. Reset to 0 once done
    int         ClickedLine;            // [Out] Source line of the block clicked this frame (0 = none)

    ImGuiMdState()                      { memset(this, 0, sizeof(*this)); }
};

//-----------------------------------------------------------------------------
// [SECTION] Functions
//-----------------------------------------------------------------------------

namespace ImGuiMd
{
    IMGUI_MD_API ImGuiMdDocument*   CreateDocument();
    IMGUI_MD_API void               DestroyDocument(ImGuiMdDocument* doc);
    IMGUI_MD_API void               SetText(ImGuiMdDocument* doc, const char* text, const char* text_end = NULL);

    // Glyph ranges to load in the fonts: Latin-1 and Latin Extended-A, general punctuation (apostrophe, dashes, ellipsis, narrow no-break space),
    // arrows, box drawing, block elements, geometric shapes, symbols and dingbats (ASCII diagrams, check marks).
    // ImGui has no automatic font fallback: merge a font which holds the symbols (GetSymbolGlyphRanges) into the fonts which lack them.
    IMGUI_MD_API const ImWchar*     GetGlyphRanges();
    IMGUI_MD_API const ImWchar*     GetSymbolGlyphRanges();

    // Draw inside the current window or child. The wrap width is the available content width.
    IMGUI_MD_API void               Render(ImGuiMdDocument* doc, const ImGuiMdConfig& config, ImGuiMdState* state = NULL);
}
