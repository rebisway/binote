// imgui_prose - www.rebisway.com - contact@rebisway.com
// Copyright (c) 2026 Thomas Gourgnier
// SPDX-License-Identifier: MIT
//
// Licensed under the MIT license: see the LICENSE file in the folder of this component.

// imgui_prose : multiline text editor for prose, for Dear ImGui (MIT license)
// Word wrap, line numbers, paragraph numbers, current paragraph highlight, decorations (backgrounds, squiggles),
// selection, undo/redo, UTF-8. A paragraph is a non-empty line. Dependencies: Dear ImGui only.
//
// Lines are 1-based (as shown to the user), columns are 0-based byte offsets in the line (UTF-8).

// Index of this file:
// [SECTION] Forward declarations and configuration
// [SECTION] Structures
// [SECTION] Functions

#pragma once
#include "imgui.h"

//-----------------------------------------------------------------------------
// [SECTION] Forward declarations and configuration
//-----------------------------------------------------------------------------

#ifndef IMGUI_PROSE_API
#define IMGUI_PROSE_API
#endif

struct ImGuiProseEditor;                // The state of one editor (text, caret, undo, layout cache)
struct ImGuiProsePos;
struct ImGuiProseSpan;
struct ImGuiProseColors;
struct ImGuiProseConfig;
struct ImGuiProseShaper;
struct ImGuiProseScrollAnchor;

enum ImGuiProseSpanKind_
{
    ImGuiProseSpanKind_Background,      // Filled rectangle behind the text. Start = End = -1: the whole line
    ImGuiProseSpanKind_Squiggle,        // Wavy underline (spelling)
    ImGuiProseSpanKind_Underline,
    ImGuiProseSpanKind_TextColor,       // Changes the color of the text
};

//-----------------------------------------------------------------------------
// [SECTION] Structures
//-----------------------------------------------------------------------------

struct ImGuiProsePos
{
    int     Line;                       // 1-based
    int     Column;                     // 0-based byte offset in the line

    ImGuiProsePos()                     { Line = 1; Column = 0; }
    ImGuiProsePos(int line, int column) { Line = line; Column = column; }
};

// A decoration given by the host (diff marks, spelling errors...). The editor does not keep spans in step with the edits:
// the host sets them again when the text changed (see ImGuiProse::GetVersion).
struct ImGuiProseSpan
{
    int     Line;                       // 1-based
    int     Start, End;                 // Byte columns, End exclusive
    ImU32   Color;
    int     Kind;                       // ImGuiProseSpanKind_

    ImGuiProseSpan()                    { Line = 1; Start = End = 0; Color = 0; Kind = ImGuiProseSpanKind_Background; }
    ImGuiProseSpan(int line, int start, int end, ImU32 color, int kind = ImGuiProseSpanKind_Background) { Line = line; Start = start; End = end; Color = color; Kind = kind; }
};

struct ImGuiProseScrollAnchor
{
    int     Paragraph;                  // 1-based, 0 = the view shows no paragraph
    float   Fraction;                   // 0..1: part of the paragraph (all its rows) already scrolled out at the top; -1..0 for the first paragraph while the padding above it is still visible

    ImGuiProseScrollAnchor()            { Paragraph = 0; Fraction = 0.0f; }
    ImGuiProseScrollAnchor(int paragraph, float fraction) { Paragraph = paragraph; Fraction = fraction; }
};

struct ImGuiProseColors
{
    ImU32   Text;                       // 0 = ImGuiCol_Text
    ImU32   Background;                 // 0 = ImGuiCol_FrameBg
    ImU32   Gutter;                     // Background of the numbers
    ImU32   LineNumber;
    ImU32   ParagraphNumber;
    ImU32   Selection;
    ImU32   Caret;                      // 0 = text color
    ImU32   CurrentBackground;          // Background of the current paragraph
    ImU32   CurrentText;                // Text of the current paragraph, 0 = unchanged
    ImU32   Nbsp;                       // Marker of U+00A0
    ImU32   NarrowNbsp;                 // Marker of U+202F
    ImU32   ThinSpace;                  // Marker of the thin spaces (U+2009, U+200A, U+2006)
    ImU32   WideSpace;                  // Marker of the wide spaces (en, em, figure, punctuation, ideographic...)
    ImU32   ZeroWidth;                  // Marker of the zero-width characters (U+200B, U+200C, U+200D, U+2060, U+FEFF)
    ImU32   SoftHyphen;                 // Marker of U+00AD
    ImU32   Control;                    // Marker of the direction marks, the line and paragraph separators and the tabs
    ImU32   Spacer;                     // Spacer lines (SetParagraphSpacers)
    ImU32   SpacerLines;                // Hatching of the spacers

    ImGuiProseColors()
    {
        Text = Background = 0;
        Gutter = IM_COL32(128, 128, 128, 30);
        LineNumber = IM_COL32(128, 128, 128, 255);
        ParagraphNumber = IM_COL32(200, 150, 60, 255);
        Selection = IM_COL32(70, 130, 220, 110);
        Caret = 0;
        CurrentBackground = IM_COL32(255, 220, 80, 60);
        CurrentText = 0;
        Nbsp = IM_COL32(230, 140, 30, 120);
        NarrowNbsp = IM_COL32(60, 140, 240, 120);
        ThinSpace = IM_COL32(0, 190, 200, 130);
        WideSpace = IM_COL32(80, 190, 90, 130);
        ZeroWidth = IM_COL32(220, 60, 200, 190);
        SoftHyphen = IM_COL32(150, 100, 220, 160);
        Control = IM_COL32(230, 50, 50, 170);
        Spacer = IM_COL32(128, 128, 128, 28);
        SpacerLines = IM_COL32(128, 128, 128, 55);
    }
};

// How the text of a row is measured, broken, drawn and walked through. The built-in implementation (ImGuiProse::GetDefaultShaper) handles
// left-to-right text, one glyph per code point. A host may provide its own (right-to-left scripts, shaping, other line breaking rules).
// A row is the byte range [row_start, row_end) of a line: all offsets are byte offsets in the line (UTF-8). Every function receives the shaper
// itself, so UserData is reachable. All functions are required.
struct ImGuiProseShaper
{
    void*   UserData;
    // Appends the start offsets of the rows of a line, but the first one (which starts at 0): out_starts already holds [0]. Called when the line wraps.
    void    (*BreakRows)(const ImGuiProseShaper* shaper, const char* text, int size, ImFont* font, float font_size, float wrap_width, ImVector<int>* out_starts);
    // Distance from the left edge of the row to the caret position before the byte column col (row_start <= col <= row_end)
    float   (*ColumnToX)(const ImGuiProseShaper* shaper, const char* text, int row_start, int row_end, int col, ImFont* font, float font_size);
    // The byte column of the row whose caret position is the nearest to x (x is relative to the left edge of the row)
    int     (*XToColumn)(const ImGuiProseShaper* shaper, const char* text, int row_start, int row_end, float x, ImFont* font, float font_size);
    // Horizontal extents (pairs of x, relative to the left edge of the row) which the bytes [from, to) occupy, clipped to the row. May be several (mixed directions)
    void    (*GetRanges)(const ImGuiProseShaper* shaper, const char* text, int row_start, int row_end, int from, int to, ImFont* font, float font_size, ImVector<ImVec2>* out_ranges);
    // Draws the bytes [from, to) of a row in one colour, at their place in the row. pos is the top left of the row text (x of the left edge, y of the glyph top)
    void    (*DrawRun)(const ImGuiProseShaper* shaper, ImDrawList* draw_list, ImFont* font, float font_size, ImVec2 pos, const char* text, int row_start, int row_end, int from, int to, ImU32 color);
    // Caret position next to col in the visual direction (-1 left, +1 right). The result is a column of the line, col itself at the end of the line
    int     (*MoveColumn)(const ImGuiProseShaper* shaper, const char* text, int size, int col, int direction);

    ImGuiProseShaper()
    {
        UserData = NULL;
        BreakRows = NULL; ColumnToX = NULL; XToColumn = NULL; GetRanges = NULL; DrawRun = NULL; MoveColumn = NULL;
    }
};

struct ImGuiProseConfig
{
    ImGuiProseColors Colors;
    ImFont*     Font;                   // NULL = current font
    const ImGuiProseShaper* Shaper;         // NULL = the built-in one (left-to-right, see ImGuiProse::GetDefaultShaper). Must outlive the editor
    bool        WordWrap;
    bool        ShowLineNumbers;
    bool        ShowParagraphNumbers;
    bool        ScrollPastEnd;          // Room below the last line, so any line can be scrolled to the top of the view
    bool        ShowNbsp;               // Mark the no-break spaces (U+00A0, U+202F)
    bool        ShowInvisible;          // Mark every invisible typographic character but the normal space, one colour by kind (see ImGuiProseColors)
    bool        ReadOnly;
    bool        HighlightCaretParagraph;// Without HighlightLine, the paragraph of the caret is the current one
    int         HighlightLine;          // 1-based line of the current paragraph (0 = none / caret). Set it every frame
    bool        HighlightBackground;    // Use Colors.CurrentBackground
    bool        HighlightText;          // Use Colors.CurrentText
    float       LineSpacing;            // Row height in multiples of the font size
    float       Padding;                // Around the text, in pixels
    int         TabSpaces;              // A typed or pasted tab becomes this number of spaces
    // Called after characters typed by the user were inserted (typography, auto-replacement). May call ImGuiProse::Replace.
    void        (*TypedCallback)(ImGuiProseEditor* editor, const char* typed_utf8, void* user_data);
    void*       UserData;

    ImGuiProseConfig()
    {
        Font = NULL;
        Shaper = NULL;
        WordWrap = true;
        ShowLineNumbers = true;
        ShowParagraphNumbers = true;
        ScrollPastEnd = false;
        ShowNbsp = false;
        ShowInvisible = false;
        ReadOnly = false;
        HighlightCaretParagraph = true;
        HighlightLine = 0;
        HighlightBackground = true;
        HighlightText = false;
        LineSpacing = 1.35f;
        Padding = 8.0f;
        TabSpaces = 4;
        TypedCallback = NULL;
        UserData = NULL;
    }
};

//-----------------------------------------------------------------------------
// [SECTION] Functions
//-----------------------------------------------------------------------------

namespace ImGuiProse
{
    IMGUI_PROSE_API ImGuiProseEditor*   CreateEditor();
    IMGUI_PROSE_API const ImGuiProseShaper* GetDefaultShaper();                                          // The built-in one: a host can call its functions for the runs it does not handle itself
    IMGUI_PROSE_API void                DestroyEditor(ImGuiProseEditor* editor);

    // Draw the editor in a child window. Returns true if the text was changed this frame.
    IMGUI_PROSE_API bool                Render(ImGuiProseEditor* editor, const char* str_id, const ImGuiProseConfig& config, ImVec2 size = ImVec2(0.0f, 0.0f));

    // Text. SetText clears the undo history. Line breaks are '\n' (CR is dropped).
    IMGUI_PROSE_API void                SetText(ImGuiProseEditor* editor, const char* text, const char* text_end = NULL);
    IMGUI_PROSE_API void                GetText(ImGuiProseEditor* editor, ImVector<char>* out);              // Zero terminated
    IMGUI_PROSE_API int                 GetLineCount(ImGuiProseEditor* editor);
    IMGUI_PROSE_API const char*         GetLine(ImGuiProseEditor* editor, int line, int* out_length);        // 1-based. Valid until the next edit
    IMGUI_PROSE_API int                 GetVersion(ImGuiProseEditor* editor);                                // Changes with every edit
    IMGUI_PROSE_API int                 GetParagraphOfLine(ImGuiProseEditor* editor, int line);              // 1-based paragraph number, 0 for a blank line
    IMGUI_PROSE_API int                 GetParagraphCount(ImGuiProseEditor* editor);
    IMGUI_PROSE_API int                 GetLineOfParagraph(ImGuiProseEditor* editor, int paragraph);         // 0 if none

    // Edition (undoable). Replaces the text between two positions, the caret goes after the inserted text.
    IMGUI_PROSE_API void                Replace(ImGuiProseEditor* editor, ImGuiProsePos start, ImGuiProsePos end, const char* text, const char* text_end = NULL);
    IMGUI_PROSE_API bool                Undo(ImGuiProseEditor* editor);
    IMGUI_PROSE_API bool                Redo(ImGuiProseEditor* editor);

    // Caret and selection
    IMGUI_PROSE_API ImGuiProsePos       GetCaret(ImGuiProseEditor* editor);
    IMGUI_PROSE_API void                SetCaret(ImGuiProseEditor* editor, ImGuiProsePos pos, bool scroll_to = true);
    IMGUI_PROSE_API void                SetSelection(ImGuiProseEditor* editor, ImGuiProsePos start, ImGuiProsePos end);
    IMGUI_PROSE_API bool                GetSelection(ImGuiProseEditor* editor, ImGuiProsePos* out_start, ImGuiProsePos* out_end);   // False if empty
    IMGUI_PROSE_API void                ScrollToLine(ImGuiProseEditor* editor, int line, float ratio = 0.3f);       // Applied by the next Render

    // Decorations. The spans are copied.
    IMGUI_PROSE_API void                SetSpans(ImGuiProseEditor* editor, const ImGuiProseSpan* spans, int count);
    IMGUI_PROSE_API bool                HasFocus(ImGuiProseEditor* editor);
    // Text position under a screen position (context menu, tooltips). False before the first Render.
    IMGUI_PROSE_API bool                GetPositionFromScreen(ImGuiProseEditor* editor, ImVec2 screen_pos, ImGuiProsePos* out_pos);                                  // As of the last Render

    // Scroll synchronisation between two editors (source / destination). The anchor is the paragraph at the top of the view and the fraction
    // of it already scrolled, so two texts whose paragraphs differ in length stay aligned paragraph by paragraph.
    // Usage, after both Render calls:  if (ScrolledByUser(a) && !ScrolledByUser(b)) SetScrollAnchor(b, GetScrollAnchor(a));
    // (SetScrollAnchor / SetScrollFraction are not reported by ScrolledByUser, so the two editors do not drive each other in a loop.)
    // Facing paragraphs: empty space (not editable, not in the text) above paragraph n (1-based) of the editor, heights[n - 1] pixels.
    // GetParagraphGeometry gives where a paragraph would start without any spacer (top, from the first line), as of the last Render.
    // The host computes the spacers of two editors so that matching paragraphs start at the same height.
    IMGUI_PROSE_API void                SetParagraphSpacers(ImGuiProseEditor* editor, const float* heights, int count);
    IMGUI_PROSE_API bool                GetParagraphGeometry(ImGuiProseEditor* editor, int paragraph, float* top, float* height);

    IMGUI_PROSE_API ImGuiProseScrollAnchor GetScrollAnchor(ImGuiProseEditor* editor);
    IMGUI_PROSE_API void                SetScrollAnchor(ImGuiProseEditor* editor, ImGuiProseScrollAnchor anchor);   // Applied by the next Render
    IMGUI_PROSE_API float               GetScrollFraction(ImGuiProseEditor* editor);                         // 0..1 of the scroll range
    IMGUI_PROSE_API void                SetScrollFraction(ImGuiProseEditor* editor, float fraction);         // Applied by the next Render
    IMGUI_PROSE_API bool                ScrolledByUser(ImGuiProseEditor* editor);                            // The view moved in the last Render (wheel, scroll bar, caret), not by a Set call
}
