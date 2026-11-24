// imgui_prose : multiline text editor for prose, for Dear ImGui (MIT license)

// Index of this file:
// [SECTION] Text storage
// [SECTION] Editor state
// [SECTION] Edition and undo
// [SECTION] Built-in shaper
// [SECTION] Layout (word wrap)
// [SECTION] Caret movement
// [SECTION] Input
// [SECTION] Drawing
// [SECTION] Public API

#include "imgui_prose.h"
#include "imgui_internal.h"
#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// [SECTION] Text storage
//-----------------------------------------------------------------------------

struct ImGuiProseLine
{
    char*   Data;                   // Zero terminated
    int     Size;
    int     Cap;
    int     Rows;                   // Visual rows with the current wrap width, -1 = to compute
    float   Y;                      // Top, relative to the first line
    float   Width;                  // Width of the text (no wrap), -1 = to compute
    int     Para;                   // 1-based paragraph number, 0 for a blank line
    bool    Blank;
    float   Pad;                    // Spacer above the line (host request, see SetParagraphSpacers), 0 if none
    float   PadSum;                 // Spacers up to this line, included
};

static void ImGuiProseLine_Init(ImGuiProseLine* l)
{
    memset(l, 0, sizeof(*l));
    l->Rows = -1;
    l->Width = -1.0f;
    l->Cap = 16;
    l->Data = (char*)IM_ALLOC(l->Cap);
    l->Data[0] = 0;
}

static void ImGuiProseLine_Free(ImGuiProseLine* l)
{
    IM_FREE(l->Data);
    l->Data = NULL;
}

static void ImGuiProseLine_Reserve(ImGuiProseLine* l, int size)
{
    if (size + 1 <= l->Cap)
        return;
    int cap = ImMax(l->Cap * 2, size + 1);
    char* data = (char*)IM_ALLOC(cap);
    memcpy(data, l->Data, l->Size + 1);
    IM_FREE(l->Data);
    l->Data = data;
    l->Cap = cap;
}

static void ImGuiProseLine_Insert(ImGuiProseLine* l, int col, const char* text, int len)
{
    ImGuiProseLine_Reserve(l, l->Size + len);
    memmove(l->Data + col + len, l->Data + col, l->Size - col + 1);
    memcpy(l->Data + col, text, len);
    l->Size += len;
    l->Rows = -1;
    l->Width = -1.0f;
}

static void ImGuiProseLine_Erase(ImGuiProseLine* l, int col, int len)
{
    memmove(l->Data + col, l->Data + col + len, l->Size - col - len + 1);
    l->Size -= len;
    l->Rows = -1;
    l->Width = -1.0f;
}

//-----------------------------------------------------------------------------
// [SECTION] Editor state
//-----------------------------------------------------------------------------

struct ImGuiProseUndoRecord
{
    int     Line, Column;                           // 0-based start of the replaced range
    int     RemovedOffset, RemovedLength;           // In UndoText
    int     InsertedOffset, InsertedLength;
    int     CaretBeforeLine, CaretBeforeColumn;
    int     CaretAfterLine, CaretAfterColumn;
    int     Group;                                  // Records of one group are undone together
};

struct ImGuiProseEditor
{
    ImVector<ImGuiProseLine>        Lines;
    int                             CaretLine, CaretCol;        // 0-based line
    int                             AnchorLine, AnchorCol;
    float                           PreferredX;                 // < 0: to compute
    int                             Version;
    bool                            Changed;                    // In this frame

    // Layout cache
    ImFont*                         LayoutFont;
    float                           LayoutFontSize;
    float                           LayoutWrapWidth;
    bool                            LayoutWrap;
    const ImGuiProseShaper*         Shaper;                 // Set by every Render, never NULL after CreateEditor
    const ImGuiProseShaper*         LayoutShaper;
    float                           TotalHeight;
    float                           MaxLineWidth;
    int                             ParaCount;
    ImVector<int>                   ParaLines;                  // 0-based line of each paragraph
    ImVector<int>                   TempRows;

    // Undo
    ImVector<ImGuiProseUndoRecord>  Undo;
    int                             UndoIndex;
    ImVector<char>                  UndoText;
    int                             Group;
    int                             LastKind;
    double                          LastEditTime;
    int                             LastEndLine, LastEndCol;

    // Decorations
    ImVector<ImGuiProseSpan>        Spans;

    // Interaction
    bool                            Focused;
    bool                            Dragging;
    bool                            ScrollToCaret;
    int                             ScrollLine;                 // 0-based, -1 = none
    float                           ScrollRatio;
    double                          BlinkStart;

    // Empty space the host asks above some paragraphs (to face the paragraphs of another editor)
    ImVector<float>                 Spacers;

    // Scroll synchronisation
    float                           PrevScrollY;
    float                           ScrollMaxY;
    bool                            UserScrolled;
    int                             ProgrammaticFrames;         // Frames during which a scroll change comes from a Set call
    bool                            PendingAnchor, PendingFraction;
    ImGuiProseScrollAnchor          PendingAnchorValue;
    float                           PendingFractionValue;
    ImGuiProseScrollAnchor          CurrentAnchor;
    float                           CurrentFraction;

    // Layout of the last Render, to map a screen position to a text position
    ImVec2                          ViewOrigin;
    float                           ViewTextLeft, ViewPad, ViewRowHeight, ViewFontSize;
    ImFont*                         ViewFont;
    bool                            ViewValid;

    ImGuiProseEditor()
    {
        CaretLine = CaretCol = AnchorLine = AnchorCol = 0;
        PreferredX = -1.0f;
        Version = 0;
        Changed = false;
        LayoutFont = NULL;
        LayoutFontSize = LayoutWrapWidth = TotalHeight = MaxLineWidth = 0.0f;
        LayoutWrap = true;
        ParaCount = 0;
        UndoIndex = 0;
        Group = 0;
        LastKind = 0;
        LastEditTime = 0.0;
        LastEndLine = LastEndCol = -1;
        Focused = Dragging = ScrollToCaret = false;
        ScrollLine = -1;
        ScrollRatio = 0.3f;
        BlinkStart = 0.0;
        PrevScrollY = ScrollMaxY = 0.0f;
        UserScrolled = false;
        ProgrammaticFrames = 0;
        PendingAnchor = PendingFraction = false;
        PendingFractionValue = 0.0f;
        CurrentFraction = 0.0f;
        ViewOrigin = ImVec2(0, 0);
        ViewTextLeft = ViewPad = ViewRowHeight = ViewFontSize = 0.0f;
        ViewFont = NULL;
        ViewValid = false;
        ImGuiProseLine first;
        ImGuiProseLine_Init(&first);
        Lines.push_back(first);
    }
    ~ImGuiProseEditor()
    {
        for (int n = 0; n < Lines.Size; n++)
            ImGuiProseLine_Free(&Lines[n]);
    }
};

static int ImGuiProse_Utf8Next(const char* data, int size, int col)
{
    if (col >= size)
        return size;
    col++;
    while (col < size && (data[col] & 0xC0) == 0x80)
        col++;
    return col;
}

static int ImGuiProse_Utf8Prev(const char* data, int col)
{
    if (col <= 0)
        return 0;
    col--;
    while (col > 0 && (data[col] & 0xC0) == 0x80)
        col--;
    return col;
}

static bool ImGuiProse_IsBlankLine(const ImGuiProseLine& l)
{
    for (int n = 0; n < l.Size; n++)
        if (l.Data[n] != ' ' && l.Data[n] != '\t')
            return false;
    return true;
}

//-----------------------------------------------------------------------------
// [SECTION] Edition and undo
//-----------------------------------------------------------------------------

// Make text fit the editor: CR dropped, tabs turned into spaces
static void ImGuiProse_Normalize(const char* text, int len, int tab_spaces, ImVector<char>* out)
{
    out->resize(0);
    out->reserve(len + 1);
    for (int n = 0; n < len; n++)
    {
        if (text[n] == '\r')
            continue;
        if (text[n] == '\t')
            for (int k = 0; k < tab_spaces; k++)
                out->push_back(' ');
        else
            out->push_back(text[n]);
    }
}

static void ImGuiProse_Advance(int line, int col, const char* text, int len, int* out_line, int* out_col)
{
    int last_break = -1;
    for (int n = 0; n < len; n++)
        if (text[n] == '\n')
        {
            line++;
            last_break = n;
        }
    *out_line = line;
    *out_col = (last_break < 0) ? col + len : len - last_break - 1;
}

static void ImGuiProse_Clamp(ImGuiProseEditor* ed, int* line, int* col)
{
    *line = ImClamp(*line, 0, ed->Lines.Size - 1);
    *col = ImClamp(*col, 0, ed->Lines[*line].Size);
    while (*col > 0 && *col < ed->Lines[*line].Size && (ed->Lines[*line].Data[*col] & 0xC0) == 0x80)
        (*col)--;
}

static void ImGuiProse_GetRange(ImGuiProseEditor* ed, int l0, int c0, int l1, int c1, ImVector<char>* out)
{
    out->resize(0);
    for (int l = l0; l <= l1; l++)
    {
        const ImGuiProseLine& line = ed->Lines[l];
        const int a = (l == l0) ? c0 : 0;
        const int b = (l == l1) ? c1 : line.Size;
        for (int n = a; n < b; n++)
            out->push_back(line.Data[n]);
        if (l < l1)
            out->push_back('\n');
    }
}

// Remove the range then insert the text, without undo. Positions must be ordered and valid.
static void ImGuiProse_ReplaceRaw(ImGuiProseEditor* ed, int l0, int c0, int l1, int c1, const char* text, int len)
{
    // Remove
    if (l0 == l1)
        ImGuiProseLine_Erase(&ed->Lines[l0], c0, c1 - c0);
    else
    {
        ImGuiProseLine& first = ed->Lines[l0];
        const ImGuiProseLine& last = ed->Lines[l1];
        const int tail = last.Size - c1;
        first.Size = c0;
        first.Data[c0] = 0;
        ImGuiProseLine_Insert(&first, c0, last.Data + c1, tail);
        for (int l = l0 + 1; l <= l1; l++)
            ImGuiProseLine_Free(&ed->Lines[l]);
        ed->Lines.erase(ed->Lines.Data + l0 + 1, ed->Lines.Data + l1 + 1);
    }

    // Insert
    int breaks = 0;
    for (int n = 0; n < len; n++)
        if (text[n] == '\n')
            breaks++;
    if (breaks == 0)
        ImGuiProseLine_Insert(&ed->Lines[l0], c0, text, len);
    else
    {
        // The end of the first line moves to the end of the last inserted line
        ImVector<char> tail;
        {
            ImGuiProseLine& first = ed->Lines[l0];
            for (int k = c0; k < first.Size; k++)
                tail.push_back(first.Data[k]);
            first.Size = c0;
            first.Data[c0] = 0;
            first.Rows = -1;
            first.Width = -1.0f;
        }
        int seg_start = 0, line = l0;
        for (int n = 0; n <= len; n++)
        {
            if (n < len && text[n] != '\n')
                continue;
            if (line == l0)
                ImGuiProseLine_Insert(&ed->Lines[l0], ed->Lines[l0].Size, text + seg_start, n - seg_start);
            else
            {
                ImGuiProseLine nl;
                ImGuiProseLine_Init(&nl);
                ImGuiProseLine_Insert(&nl, 0, text + seg_start, n - seg_start);
                ed->Lines.insert(ed->Lines.Data + line, nl);
            }
            seg_start = n + 1;
            line++;
        }
        // 'line' is now one past the last inserted line: the last segment sits in line - 1
        ImGuiProseLine& last = ed->Lines[line - 1];
        ImGuiProseLine_Insert(&last, last.Size, tail.Data, tail.Size);
    }    ed->Version++;
    ed->Changed = true;
}

// Edit with undo. kind: 0 other, 1 typing, 2 backspace, 3 delete (consecutive edits of the same kind are undone together).
static void ImGuiProse_Edit(ImGuiProseEditor* ed, int l0, int c0, int l1, int c1, const char* text, int len, int kind)
{
    ImGuiProse_Clamp(ed, &l0, &c0);
    ImGuiProse_Clamp(ed, &l1, &c1);
    if (l1 < l0 || (l1 == l0 && c1 < c0))
    {
        ImSwap(l0, l1);
        ImSwap(c0, c1);
    }
    if (l0 == l1 && c0 == c1 && len == 0)
        return;

    // Undo record
    ed->Undo.resize(ed->UndoIndex);
    ed->UndoText.resize(ed->UndoIndex > 0 ? ed->Undo.back().InsertedOffset + ed->Undo.back().InsertedLength : 0);
    ImGuiProseUndoRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.Line = l0;
    rec.Column = c0;
    rec.CaretBeforeLine = ed->CaretLine;
    rec.CaretBeforeColumn = ed->CaretCol;
    ImVector<char> removed;
    ImGuiProse_GetRange(ed, l0, c0, l1, c1, &removed);
    rec.RemovedOffset = ed->UndoText.Size;
    rec.RemovedLength = removed.Size;
    for (int n = 0; n < removed.Size; n++)
        ed->UndoText.push_back(removed[n]);
    rec.InsertedOffset = ed->UndoText.Size;
    rec.InsertedLength = len;
    for (int n = 0; n < len; n++)
        ed->UndoText.push_back(text[n]);

    const double now = ImGui::GetTime();
    const bool same_group = kind != 0 && kind == ed->LastKind && now - ed->LastEditTime < 1.0
        && ed->LastEndLine == ed->CaretLine && ed->LastEndCol == ed->CaretCol && ed->UndoIndex > 0 && !(kind == 1 && len > 0 && text[0] == '\n');
    if (!same_group)
        ed->Group++;
    rec.Group = ed->Group;

    ImGuiProse_ReplaceRaw(ed, l0, c0, l1, c1, text, len);
    int el, ec;
    ImGuiProse_Advance(l0, c0, text, len, &el, &ec);
    ed->CaretLine = ed->AnchorLine = el;
    ed->CaretCol = ed->AnchorCol = ec;
    ed->PreferredX = -1.0f;
    ed->ScrollToCaret = true;
    ed->BlinkStart = now;
    rec.CaretAfterLine = el;
    rec.CaretAfterColumn = ec;
    ed->Undo.push_back(rec);
    ed->UndoIndex = ed->Undo.Size;
    ed->LastKind = (kind == 1 && len > 0 && memchr(text, '\n', len)) ? 0 : kind;       // A line break ends the typing group
    ed->LastEditTime = now;
    ed->LastEndLine = el;
    ed->LastEndCol = ec;
}

static bool ImGuiProse_DoUndo(ImGuiProseEditor* ed)
{
    if (ed->UndoIndex <= 0)
        return false;
    const int group = ed->Undo[ed->UndoIndex - 1].Group;
    while (ed->UndoIndex > 0 && ed->Undo[ed->UndoIndex - 1].Group == group)
    {
        const ImGuiProseUndoRecord rec = ed->Undo[--ed->UndoIndex];
        int el, ec;
        ImGuiProse_Advance(rec.Line, rec.Column, ed->UndoText.Data + rec.InsertedOffset, rec.InsertedLength, &el, &ec);
        ImGuiProse_ReplaceRaw(ed, rec.Line, rec.Column, el, ec, ed->UndoText.Data + rec.RemovedOffset, rec.RemovedLength);
        ed->CaretLine = ed->AnchorLine = rec.CaretBeforeLine;
        ed->CaretCol = ed->AnchorCol = rec.CaretBeforeColumn;
    }
    ed->LastKind = 0;
    ed->PreferredX = -1.0f;
    ed->ScrollToCaret = true;
    return true;
}

static bool ImGuiProse_DoRedo(ImGuiProseEditor* ed)
{
    if (ed->UndoIndex >= ed->Undo.Size)
        return false;
    const int group = ed->Undo[ed->UndoIndex].Group;
    while (ed->UndoIndex < ed->Undo.Size && ed->Undo[ed->UndoIndex].Group == group)
    {
        const ImGuiProseUndoRecord rec = ed->Undo[ed->UndoIndex++];
        int el, ec;
        ImGuiProse_Advance(rec.Line, rec.Column, ed->UndoText.Data + rec.RemovedOffset, rec.RemovedLength, &el, &ec);
        ImGuiProse_ReplaceRaw(ed, rec.Line, rec.Column, el, ec, ed->UndoText.Data + rec.InsertedOffset, rec.InsertedLength);
        ed->CaretLine = ed->AnchorLine = rec.CaretAfterLine;
        ed->CaretCol = ed->AnchorCol = rec.CaretAfterColumn;
    }
    ed->LastKind = 0;
    ed->PreferredX = -1.0f;
    ed->ScrollToCaret = true;
    return true;
}

static bool ImGuiProse_HasSelection(const ImGuiProseEditor* ed)
{
    return ed->CaretLine != ed->AnchorLine || ed->CaretCol != ed->AnchorCol;
}

// Ordered selection
static void ImGuiProse_GetSel(const ImGuiProseEditor* ed, int* l0, int* c0, int* l1, int* c1)
{
    if (ed->AnchorLine < ed->CaretLine || (ed->AnchorLine == ed->CaretLine && ed->AnchorCol <= ed->CaretCol))
    {
        *l0 = ed->AnchorLine; *c0 = ed->AnchorCol; *l1 = ed->CaretLine; *c1 = ed->CaretCol;
    }
    else
    {
        *l0 = ed->CaretLine; *c0 = ed->CaretCol; *l1 = ed->AnchorLine; *c1 = ed->AnchorCol;
    }
}

static void ImGuiProse_DeleteSelection(ImGuiProseEditor* ed, int kind)
{
    int l0, c0, l1, c1;
    ImGuiProse_GetSel(ed, &l0, &c0, &l1, &c1);
    ImGuiProse_Edit(ed, l0, c0, l1, c1, "", 0, kind);
}

//-----------------------------------------------------------------------------
// [SECTION] Built-in shaper (left to right, one glyph per code point)
//-----------------------------------------------------------------------------

static float ImGuiProse_TextWidth(ImFont* font, float font_size, const char* a, const char* b)
{
    return (b > a) ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, a, b).x : 0.0f;
}

// Line breaking rules of East Asian text (kinsoku): some signs never start a line (closing brackets, stops, small kana, the prolonged sound mark),
// others never end it (opening brackets). Only signs of the CJK blocks are listed: the Latin ones are left to the word wrap.
static bool ImGuiProse_NoLineStart(unsigned int c)
{
    static const unsigned int list[] = { 0x2019, 0x201D, 0x2025, 0x2026, 0x3001, 0x3002, 0x3005, 0x3009, 0x300B, 0x300D, 0x300F, 0x3011, 0x3015, 0x3017, 0x3019, 0x301C, 0x301F, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049,
        0x3063, 0x3083, 0x3085, 0x3087, 0x308E, 0x309D, 0x309E, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE, 0x30F5, 0x30F6, 0x30FB, 0x30FC, 0x30FD, 0x30FE,
        0xFF01, 0xFF05, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF3D, 0xFF5D, 0xFF61, 0xFF63, 0xFF64, 0xFF65 };
    for (unsigned int u : list)
        if (u == c)
            return true;
    return false;
}

static bool ImGuiProse_NoLineEnd(unsigned int c)
{
    static const unsigned int list[] = { 0x2018, 0x201C, 0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014, 0x3016, 0x3018, 0x301D, 0xFF08, 0xFF3B, 0xFF5B, 0xFF62 };
    for (unsigned int u : list)
        if (u == c)
            return true;
    return false;
}

// Moves a break back until the rules above hold (never before the first glyph of the row)
static const char* ImGuiProse_AdjustBreak(const char* row_start, const char* e, const char* end)
{
    for (int guard = 0; guard < 4 && e > row_start && e < end; guard++)
    {
        unsigned int next = 0;
        ImTextCharFromUtf8(&next, e, end);
        const char* prev_pos = e;
        while (prev_pos > row_start && (*(prev_pos - 1) & 0xC0) == 0x80)
            prev_pos--;
        if (prev_pos > row_start)
            prev_pos--;
        while (prev_pos > row_start && (*prev_pos & 0xC0) == 0x80)
            prev_pos--;
        unsigned int prev = 0;
        ImTextCharFromUtf8(&prev, prev_pos, end);
        if (prev_pos > row_start && (ImGuiProse_NoLineStart(next) || ImGuiProse_NoLineEnd(prev)))
            e = prev_pos;
        else
            break;
    }
    return e;
}
static void ImGuiProse_DefaultBreakRows(const ImGuiProseShaper*, const char* text, int size, ImFont* font, float font_size, float wrap_width, ImVector<int>* out)
{
    const char* end = text + size;
    const char* s = text;
    while (s < end)
    {
        const char* e = font->CalcWordWrapPosition(font_size, s, end, wrap_width);
        if (e <= s)
            e = s + ImGuiProse_Utf8Next(s, (int)(end - s), 0);
        else
            e = ImGuiProse_AdjustBreak(s, e, end);
        if (e >= end)
            break;
        const char* n = e;
        while (n < end && *n == ' ')
            n++;
        if (n >= end)
            break;
        out->push_back((int)(n - text));
        s = n;
    }
}

static float ImGuiProse_DefaultColumnToX(const ImGuiProseShaper*, const char* text, int row_start, int row_end, int col, ImFont* font, float font_size)
{
    IM_UNUSED(row_end);
    return ImGuiProse_TextWidth(font, font_size, text + row_start, text + col);
}

// Byte column of a row nearest to x (relative to the start of the row)
static int ImGuiProse_DefaultXToColumn(const ImGuiProseShaper*, const char* text, int start, int stop, float x, ImFont* font, float font_size)
{
    if (x <= 0.0f)
        return start;
    int lo = start, hi = stop;      // Largest boundary whose width is <= x
    while (lo < hi)
    {
        int mid = (lo + hi + 1) / 2;
        while (mid > lo && (text[mid] & 0xC0) == 0x80)
            mid--;
        if (mid <= lo)
            break;
        if (ImGuiProse_TextWidth(font, font_size, text + start, text + mid) <= x)
            lo = mid;
        else
        {
            hi = mid - 1;
            while (hi > lo && (text[hi] & 0xC0) == 0x80)
                hi--;
        }
    }
    if (lo < stop)                  // Nearest of the two boundaries around x
    {
        const int next = ImGuiProse_Utf8Next(text, stop, lo);
        const float w0 = ImGuiProse_TextWidth(font, font_size, text + start, text + lo);
        const float w1 = ImGuiProse_TextWidth(font, font_size, text + start, text + next);
        if (x - w0 > w1 - x && next <= stop)
            lo = next;
    }
    return lo;
}

static void ImGuiProse_DefaultGetRanges(const ImGuiProseShaper* sh, const char* text, int row_start, int row_end, int from, int to, ImFont* font, float font_size, ImVector<ImVec2>* out)
{
    out->resize(0);
    from = ImClamp(from, row_start, row_end);
    to = ImClamp(to, row_start, row_end);
    out->push_back(ImVec2(ImGuiProse_DefaultColumnToX(sh, text, row_start, row_end, from, font, font_size), ImGuiProse_DefaultColumnToX(sh, text, row_start, row_end, to, font, font_size)));
}

static void ImGuiProse_DefaultDrawRun(const ImGuiProseShaper* sh, ImDrawList* dl, ImFont* font, float font_size, ImVec2 pos, const char* text, int row_start, int row_end, int from, int to, ImU32 color)
{
    if (to <= from)
        return;
    dl->AddText(font, font_size, ImVec2(pos.x + ImGuiProse_DefaultColumnToX(sh, text, row_start, row_end, from, font, font_size), pos.y), color, text + from, text + to);
}

static int ImGuiProse_DefaultMoveColumn(const ImGuiProseShaper*, const char* text, int size, int col, int direction)
{
    return direction < 0 ? ImGuiProse_Utf8Prev(text, col) : ImGuiProse_Utf8Next(text, size, col);
}

static ImGuiProseShaper ImGuiProse_MakeDefaultShaper()
{
    ImGuiProseShaper s;
    s.BreakRows = ImGuiProse_DefaultBreakRows;
    s.ColumnToX = ImGuiProse_DefaultColumnToX;
    s.XToColumn = ImGuiProse_DefaultXToColumn;
    s.GetRanges = ImGuiProse_DefaultGetRanges;
    s.DrawRun = ImGuiProse_DefaultDrawRun;
    s.MoveColumn = ImGuiProse_DefaultMoveColumn;
    return s;
}

const ImGuiProseShaper* ImGuiProse::GetDefaultShaper()
{
    static ImGuiProseShaper shaper = ImGuiProse_MakeDefaultShaper();
    return &shaper;
}

//-----------------------------------------------------------------------------
// [SECTION] Layout (word wrap)
//-----------------------------------------------------------------------------

// Start offsets of the visual rows of a line. Rows are [start, next start): the blanks at a wrap hang at the end of the row.
static void ImGuiProse_RowBreaks(const ImGuiProseEditor* ed, const ImGuiProseLine& l, ImFont* font, float font_size, float wrap_width, bool wrap, ImVector<int>* out)
{
    out->resize(0);
    out->push_back(0);
    if (!wrap || l.Size == 0)
        return;
    ed->Shaper->BreakRows(ed->Shaper, l.Data, l.Size, font, font_size, wrap_width, out);
}

// End of the visible text of a row (without the blanks which hang at a wrap)
static int ImGuiProse_RowVisibleEnd(const ImGuiProseLine& l, const ImVector<int>& rows, int k)
{
    if (k + 1 >= rows.Size)
        return l.Size;
    int e = rows[k + 1];
    while (e > rows[k] && l.Data[e - 1] == ' ')
        e--;
    return e;
}

static int ImGuiProse_RowOfCol(const ImVector<int>& rows, int col)
{
    int k = 0;
    while (k + 1 < rows.Size && rows[k + 1] <= col)
        k++;
    return k;
}

static void ImGuiProse_UpdateLayout(ImGuiProseEditor* ed, ImFont* font, float font_size, float wrap_width, bool wrap, float row_height)
{
    if (ed->LayoutFont != font || ed->LayoutFontSize != font_size || ed->LayoutWrapWidth != wrap_width || ed->LayoutWrap != wrap || ed->LayoutShaper != ed->Shaper)
    {
        for (int n = 0; n < ed->Lines.Size; n++)
        {
            ed->Lines[n].Rows = -1;
            ed->Lines[n].Width = -1.0f;
        }
        ed->LayoutFont = font;
        ed->LayoutFontSize = font_size;
        ed->LayoutWrapWidth = wrap_width;
        ed->LayoutWrap = wrap;
        ed->LayoutShaper = ed->Shaper;
    }
    float y = 0.0f;
    float pad_sum = 0.0f;
    float max_width = 0.0f;
    int para = 0;
    ed->ParaLines.resize(0);
    for (int n = 0; n < ed->Lines.Size; n++)
    {
        ImGuiProseLine& l = ed->Lines[n];
        if (l.Rows < 0)
        {
            ImGuiProse_RowBreaks(ed, l, font, font_size, wrap_width, wrap, &ed->TempRows);
            l.Rows = ed->TempRows.Size;
            l.Blank = ImGuiProse_IsBlankLine(l);
        }
        if (!wrap)
        {
            if (l.Width < 0.0f)
                l.Width = ed->Shaper->ColumnToX(ed->Shaper, l.Data, 0, l.Size, l.Size, font, font_size);
            max_width = ImMax(max_width, l.Width);
        }
        l.Para = l.Blank ? 0 : ++para;
        if (!l.Blank)
            ed->ParaLines.push_back(n);
        l.Pad = (!l.Blank && para - 1 < ed->Spacers.Size) ? ed->Spacers[para - 1] : 0.0f;
        y += l.Pad;
        pad_sum += l.Pad;
        l.PadSum = pad_sum;
        l.Y = y;
        y += l.Rows * row_height;
    }
    ed->ParaCount = para;
    ed->TotalHeight = y;
    ed->MaxLineWidth = max_width;
}

// Line which holds the layout offset y
static int ImGuiProse_LineAtY(const ImGuiProseEditor* ed, float y)
{
    int lo = 0, hi = ed->Lines.Size - 1;
    while (lo < hi)
    {
        const int mid = (lo + hi + 1) / 2;
        if (ed->Lines[mid].Y <= y)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

// Byte column of a row nearest to x (relative to the start of the row)
static int ImGuiProse_ColumnFromX(const ImGuiProseEditor* ed, const ImGuiProseLine& l, const ImVector<int>& rows, int k, ImFont* font, float font_size, float x)
{
    return ed->Shaper->XToColumn(ed->Shaper, l.Data, rows[k], ImGuiProse_RowVisibleEnd(l, rows, k), x, font, font_size);
}

//-----------------------------------------------------------------------------
// [SECTION] Caret movement
//-----------------------------------------------------------------------------

static void ImGuiProse_MoveCaret(ImGuiProseEditor* ed, int line, int col, bool extend)
{
    ImGuiProse_Clamp(ed, &line, &col);
    ed->CaretLine = line;
    ed->CaretCol = col;
    if (!extend)
    {
        ed->AnchorLine = line;
        ed->AnchorCol = col;
    }
    ed->ScrollToCaret = true;
    ed->BlinkStart = ImGui::GetTime();
    ed->LastKind = 0;
}

// Kind of invisible typographic character at the start of a UTF-8 sequence (0 = a normal character): 1 U+00A0, 2 U+202F, 3 thin space,
// 4 wide space, 5 zero width, 6 soft hyphen, 7 control (direction marks, line and paragraph separators, tab). len receives its size in bytes.
static int ImGuiProse_InvisibleKind(const char* p, int avail, int* len)
{
    const unsigned char b0 = (unsigned char)p[0];
    if (b0 == '\t')
    {
        *len = 1;
        return 7;
    }
    if (b0 == 0xC2 && avail >= 2)
    {
        *len = 2;
        if ((unsigned char)p[1] == 0xA0)
            return 1;
        if ((unsigned char)p[1] == 0xAD)
            return 6;
        return 0;
    }
    if (b0 == 0xE2 && avail >= 3)
    {
        const unsigned char b1 = (unsigned char)p[1];
        if (b1 != 0x80 && b1 != 0x81)
            return 0;
        const unsigned c = b1 == 0x80 ? 0x2000u + ((unsigned char)p[2] & 0x3F) : 0x2040u + ((unsigned char)p[2] & 0x3F);
        *len = 3;
        if (c == 0x202F)
            return 2;
        if (c == 0x2009 || c == 0x200A || c == 0x2006)
            return 3;
        if ((c >= 0x2000 && c <= 0x2008) || c == 0x205F)
            return 4;
        if (c == 0x200B || c == 0x200C || c == 0x200D || c == 0x2060)
            return 5;
        if (c == 0x200E || c == 0x200F || c == 0x2028 || c == 0x2029 || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069))
            return 7;
        return 0;
    }
    if (b0 == 0xEF && avail >= 3 && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
    {
        *len = 3;
        return 5;                   // U+FEFF
    }
    if (b0 == 0xE3 && avail >= 3 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x80)
    {
        *len = 3;
        return 4;                   // U+3000
    }
    if (b0 == 0xE1 && avail >= 3 && (unsigned char)p[1] == 0x9A && (unsigned char)p[2] == 0x80)
    {
        *len = 3;
        return 4;                   // U+1680
    }
    return 0;
}

static bool ImGuiProse_IsWordChar(unsigned char c)
{
    if (c == ' ' || c == '\t')
        return false;
    if (c >= 0x80)
        return true;                // Letters of other alphabets; no-break spaces are caught by the caller
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '\'';
}

static bool ImGuiProse_IsWordAt(const ImGuiProseLine& l, int col)
{
    if (col < 0 || col >= l.Size)
        return false;
    if ((unsigned char)l.Data[col] == 0xC2 && col + 1 < l.Size && (unsigned char)l.Data[col + 1] == 0xA0)
        return false;               // U+00A0
    if ((unsigned char)l.Data[col] == 0xE2 && col + 2 < l.Size && (unsigned char)l.Data[col + 1] == 0x80 && (unsigned char)l.Data[col + 2] == 0xAF)
        return false;               // U+202F
    if ((unsigned char)l.Data[col] == 0xE2 && col + 2 < l.Size && (unsigned char)l.Data[col + 1] == 0x80 && ((unsigned char)l.Data[col + 2] == 0x99 || (unsigned char)l.Data[col + 2] == 0xA6))
        return (unsigned char)l.Data[col + 2] == 0x99;      // Typographic apostrophe belongs to words
    return ImGuiProse_IsWordChar((unsigned char)l.Data[col]);
}

static int ImGuiProse_WordLeft(const ImGuiProseLine& l, int col)
{
    while (col > 0 && !ImGuiProse_IsWordAt(l, ImGuiProse_Utf8Prev(l.Data, col)))
        col = ImGuiProse_Utf8Prev(l.Data, col);
    while (col > 0 && ImGuiProse_IsWordAt(l, ImGuiProse_Utf8Prev(l.Data, col)))
        col = ImGuiProse_Utf8Prev(l.Data, col);
    return col;
}

static int ImGuiProse_WordRight(const ImGuiProseLine& l, int col)
{
    while (col < l.Size && !ImGuiProse_IsWordAt(l, col))
        col = ImGuiProse_Utf8Next(l.Data, l.Size, col);
    while (col < l.Size && ImGuiProse_IsWordAt(l, col))
        col = ImGuiProse_Utf8Next(l.Data, l.Size, col);
    return col;
}

// Caret x inside its row, and the row
static float ImGuiProse_CaretX(ImGuiProseEditor* ed, ImFont* font, float font_size, int* out_row)
{
    const ImGuiProseLine& l = ed->Lines[ed->CaretLine];
    ImGuiProse_RowBreaks(ed, l, font, font_size, ed->LayoutWrapWidth, ed->LayoutWrap, &ed->TempRows);
    const int k = ImGuiProse_RowOfCol(ed->TempRows, ed->CaretCol);
    *out_row = k;
    const int stop = ImGuiProse_RowVisibleEnd(l, ed->TempRows, k);
    return ed->Shaper->ColumnToX(ed->Shaper, l.Data, ed->TempRows[k], stop, ImMin(ed->CaretCol, stop), font, font_size);
}

// Move by visual rows (negative: up)
static void ImGuiProse_MoveRows(ImGuiProseEditor* ed, int delta, bool extend, ImFont* font, float font_size)
{
    int k;
    const float x = ImGuiProse_CaretX(ed, font, font_size, &k);
    if (ed->PreferredX < 0.0f)
        ed->PreferredX = x;
    int line = ed->CaretLine;
    int row = k + delta;
    while (row < 0 && line > 0)
    {
        line--;
        ImGuiProse_RowBreaks(ed, ed->Lines[line], font, font_size, ed->LayoutWrapWidth, ed->LayoutWrap, &ed->TempRows);
        row += ed->TempRows.Size;
    }
    for (;;)
    {
        ImGuiProse_RowBreaks(ed, ed->Lines[line], font, font_size, ed->LayoutWrapWidth, ed->LayoutWrap, &ed->TempRows);
        if (row < ed->TempRows.Size || line + 1 >= ed->Lines.Size)
            break;
        row -= ed->TempRows.Size;
        line++;
    }
    if (row < 0)
        row = 0;
    if (row >= ed->TempRows.Size)
        row = ed->TempRows.Size - 1;
    const float keep = ed->PreferredX;
    const int col = ImGuiProse_ColumnFromX(ed, ed->Lines[line], ed->TempRows, row, font, font_size, keep);
    ImGuiProse_MoveCaret(ed, line, col, extend);
    ed->PreferredX = keep;
}

//-----------------------------------------------------------------------------
// [SECTION] Input
//-----------------------------------------------------------------------------

static void ImGuiProse_Insert(ImGuiProseEditor* ed, const char* text, int len, int kind, const ImGuiProseConfig& config)
{
    ImVector<char> clean;
    ImGuiProse_Normalize(text, len, config.TabSpaces, &clean);
    int l0, c0, l1, c1;
    ImGuiProse_GetSel(ed, &l0, &c0, &l1, &c1);
    ImGuiProse_Edit(ed, l0, c0, l1, c1, clean.Data, clean.Size, kind);
}

static void ImGuiProse_Copy(ImGuiProseEditor* ed)
{
    if (!ImGuiProse_HasSelection(ed))
        return;
    int l0, c0, l1, c1;
    ImGuiProse_GetSel(ed, &l0, &c0, &l1, &c1);
    ImVector<char> text;
    ImGuiProse_GetRange(ed, l0, c0, l1, c1, &text);
    text.push_back(0);
    ImGui::SetClipboardText(text.Data);
}

static void ImGuiProse_HandleKeys(ImGuiProseEditor* ed, const ImGuiProseConfig& config, ImFont* font, float font_size, float row_height, float view_height)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = io.KeyCtrl && !io.KeyAlt;
    const bool shift = io.KeyShift;
    const bool read_only = config.ReadOnly;
    const ImGuiProseLine& line = ed->Lines[ed->CaretLine];

    io.WantTextInput = !read_only;

    // Typed characters
    if (!read_only && !(io.KeyCtrl && !io.KeyAlt))
    {
        for (int n = 0; n < io.InputQueueCharacters.Size; n++)
        {
            const unsigned int c = io.InputQueueCharacters[n];
            if (c < 0x20 || c == 0x7F || (c >= 0xE000 && c < 0xF900))
                continue;
            char buf[5] = { 0, 0, 0, 0, 0 };
            ImTextCharToUtf8(buf, c);
            ImGuiProse_Insert(ed, buf, (int)strlen(buf), 1, config);
            if (config.TypedCallback)
                config.TypedCallback(ed, buf, config.UserData);
        }
    }
    io.InputQueueCharacters.resize(0);

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
    {
        ed->AnchorLine = 0;
        ed->AnchorCol = 0;
        ed->CaretLine = ed->Lines.Size - 1;
        ed->CaretCol = ed->Lines[ed->CaretLine].Size;
    }
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_C, false) || ImGui::IsKeyPressed(ImGuiKey_Insert, false)))
        ImGuiProse_Copy(ed);
    if (!read_only && ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
    {
        ImGuiProse_Copy(ed);
        if (ImGuiProse_HasSelection(ed))
            ImGuiProse_DeleteSelection(ed, 0);
    }
    if (!read_only && ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
    {
        const char* clip = ImGui::GetClipboardText();
        if (clip && clip[0])
            ImGuiProse_Insert(ed, clip, (int)strlen(clip), 0, config);
    }
    if (!read_only && ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Z))
        ImGuiProse_DoUndo(ed);
    if (!read_only && ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (shift && ImGui::IsKeyPressed(ImGuiKey_Z))))
        ImGuiProse_DoRedo(ed);

    // Movement
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
    {
        if (ImGuiProse_HasSelection(ed) && !shift && !ctrl)
        {
            int l0, c0, l1, c1;
            ImGuiProse_GetSel(ed, &l0, &c0, &l1, &c1);
            ImGuiProse_MoveCaret(ed, l0, c0, false);
        }
        else if (ctrl)
        {
            if (ed->CaretCol == 0 && ed->CaretLine > 0)
                ImGuiProse_MoveCaret(ed, ed->CaretLine - 1, ed->Lines[ed->CaretLine - 1].Size, shift);
            else
                ImGuiProse_MoveCaret(ed, ed->CaretLine, ImGuiProse_WordLeft(line, ed->CaretCol), shift);
        }
        else if (ed->CaretCol == 0)
        {
            if (ed->CaretLine > 0)
                ImGuiProse_MoveCaret(ed, ed->CaretLine - 1, ed->Lines[ed->CaretLine - 1].Size, shift);
        }
        else
            ImGuiProse_MoveCaret(ed, ed->CaretLine, ed->Shaper->MoveColumn(ed->Shaper, line.Data, line.Size, ed->CaretCol, -1), shift);
        ed->PreferredX = -1.0f;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
    {
        if (ImGuiProse_HasSelection(ed) && !shift && !ctrl)
        {
            int l0, c0, l1, c1;
            ImGuiProse_GetSel(ed, &l0, &c0, &l1, &c1);
            ImGuiProse_MoveCaret(ed, l1, c1, false);
        }
        else if (ctrl)
        {
            if (ed->CaretCol >= line.Size && ed->CaretLine + 1 < ed->Lines.Size)
                ImGuiProse_MoveCaret(ed, ed->CaretLine + 1, 0, shift);
            else
                ImGuiProse_MoveCaret(ed, ed->CaretLine, ImGuiProse_WordRight(line, ed->CaretCol), shift);
        }
        else if (ed->CaretCol >= line.Size)
        {
            if (ed->CaretLine + 1 < ed->Lines.Size)
                ImGuiProse_MoveCaret(ed, ed->CaretLine + 1, 0, shift);
        }
        else
            ImGuiProse_MoveCaret(ed, ed->CaretLine, ed->Shaper->MoveColumn(ed->Shaper, line.Data, line.Size, ed->CaretCol, 1), shift);
        ed->PreferredX = -1.0f;
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        ImGuiProse_MoveRows(ed, -1, shift, font, font_size);
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        ImGuiProse_MoveRows(ed, 1, shift, font, font_size);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
        ImGuiProse_MoveRows(ed, -ImMax(1, (int)(view_height / row_height) - 1), shift, font, font_size);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
        ImGuiProse_MoveRows(ed, ImMax(1, (int)(view_height / row_height) - 1), shift, font, font_size);
    if (ImGui::IsKeyPressed(ImGuiKey_Home))
    {
        if (ctrl)
            ImGuiProse_MoveCaret(ed, 0, 0, shift);
        else
        {
            int k;
            ImGuiProse_CaretX(ed, font, font_size, &k);
            ImGuiProse_MoveCaret(ed, ed->CaretLine, ed->TempRows[k], shift);
        }
        ed->PreferredX = -1.0f;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End))
    {
        if (ctrl)
            ImGuiProse_MoveCaret(ed, ed->Lines.Size - 1, ed->Lines[ed->Lines.Size - 1].Size, shift);
        else
        {
            int k;
            ImGuiProse_CaretX(ed, font, font_size, &k);
            ImGuiProse_MoveCaret(ed, ed->CaretLine, ImGuiProse_RowVisibleEnd(line, ed->TempRows, k), shift);
        }
        ed->PreferredX = -1.0f;
    }

    if (read_only)
        return;

    // Deletion and line breaks
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
    {
        if (ImGuiProse_HasSelection(ed))
            ImGuiProse_DeleteSelection(ed, 0);
        else if (ed->CaretCol > 0)
        {
            const int from = ctrl ? ImGuiProse_WordLeft(line, ed->CaretCol) : ImGuiProse_Utf8Prev(line.Data, ed->CaretCol);
            ImGuiProse_Edit(ed, ed->CaretLine, from, ed->CaretLine, ed->CaretCol, "", 0, 2);
        }
        else if (ed->CaretLine > 0)
            ImGuiProse_Edit(ed, ed->CaretLine - 1, ed->Lines[ed->CaretLine - 1].Size, ed->CaretLine, 0, "", 0, 0);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (ImGuiProse_HasSelection(ed))
            ImGuiProse_DeleteSelection(ed, 0);
        else if (ed->CaretCol < line.Size)
        {
            const int to = ctrl ? ImGuiProse_WordRight(line, ed->CaretCol) : ImGuiProse_Utf8Next(line.Data, line.Size, ed->CaretCol);
            ImGuiProse_Edit(ed, ed->CaretLine, ed->CaretCol, ed->CaretLine, to, "", 0, 3);
        }
        else if (ed->CaretLine + 1 < ed->Lines.Size)
            ImGuiProse_Edit(ed, ed->CaretLine, ed->CaretCol, ed->CaretLine + 1, 0, "", 0, 0);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
        ImGuiProse_Insert(ed, "\n", 1, 1, config);
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Tab))
        ImGuiProse_Insert(ed, "\t", 1, 1, config);
}

//-----------------------------------------------------------------------------
// [SECTION] Drawing
//-----------------------------------------------------------------------------

static int ImGuiProse_SpanCompare(const void* a, const void* b)
{
    const ImGuiProseSpan* x = (const ImGuiProseSpan*)a;
    const ImGuiProseSpan* y = (const ImGuiProseSpan*)b;
    if (x->Line != y->Line)
        return x->Line < y->Line ? -1 : 1;
    return x->Start < y->Start ? -1 : (x->Start > y->Start ? 1 : 0);
}

static int ImGuiProse_FirstSpan(const ImGuiProseEditor* ed, int line_1based)
{
    int lo = 0, hi = ed->Spans.Size;
    while (lo < hi)
    {
        const int mid = (lo + hi) / 2;
        if (ed->Spans[mid].Line < line_1based)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static void ImGuiProse_Squiggle(ImDrawList* dl, float x1, float x2, float y, ImU32 col)
{
    bool up = true;
    ImVec2 prev(x1, y);
    for (float x = x1 + 2.0f; x < x2 + 2.0f; x += 2.0f, up = !up)
    {
        const ImVec2 p(ImMin(x, x2), y + (up ? -2.0f : 0.0f));
        dl->AddLine(prev, p, col);
        prev = p;
    }
}

static int ImGuiProse_DigitCount(int n)
{
    int d = 1;
    while (n >= 10)
    {
        n /= 10;
        d++;
    }
    return d;
}

static ImVec2 ImGuiProse_Mouse(const ImGuiProseEditor* ed, ImVec2 origin, float text_left, float padding)
{
    IM_UNUSED(ed);
    const ImVec2 m = ImGui::GetIO().MousePos;
    return ImVec2(m.x - origin.x - text_left, m.y - origin.y - padding);
}

static void ImGuiProse_PosFromMouse(ImGuiProseEditor* ed, ImVec2 rel, ImFont* font, float font_size, float row_height, int* out_line, int* out_col)
{
    const int line = ImGuiProse_LineAtY(ed, rel.y);
    const ImGuiProseLine& l = ed->Lines[line];
    ImGuiProse_RowBreaks(ed, l, font, font_size, ed->LayoutWrapWidth, ed->LayoutWrap, &ed->TempRows);
    const int row = ImClamp((int)((rel.y - l.Y) / row_height), 0, ed->TempRows.Size - 1);
    *out_line = line;
    *out_col = ImGuiProse_ColumnFromX(ed, l, ed->TempRows, row, font, font_size, rel.x);
}

bool ImGuiProse::Render(ImGuiProseEditor* ed, const char* str_id, const ImGuiProseConfig& config, ImVec2 size)
{
    ed->Changed = false;
    ed->Shaper = config.Shaper ? config.Shaper : ImGuiProse::GetDefaultShaper();
    const ImGuiProseColors& colors = config.Colors;
    const ImU32 col_text = colors.Text ? colors.Text : ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 col_back = colors.Background ? colors.Background : ImGui::GetColorU32(ImGuiCol_FrameBg);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, col_back);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs;
    if (!config.WordWrap)
        flags |= ImGuiWindowFlags_HorizontalScrollbar;
    const bool visible = ImGui::BeginChild(str_id, size, false, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (!visible)
    {
        ImGui::EndChild();
        return false;
    }
    if (config.Font)
        ImGui::PushFont(config.Font);

    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float rh = fs * config.LineSpacing;
    const float pad = config.Padding;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ed->Focused = ImGui::IsWindowFocused();
    const bool hovered = ImGui::IsWindowHovered();

    // Gutter
    const float digit_w = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "0").x;
    float gutter_w = 0.0f, line_num_w = 0.0f, para_num_w = 0.0f;
    if (config.ShowLineNumbers)
        gutter_w += (line_num_w = digit_w * ImMax(2, ImGuiProse_DigitCount(ed->Lines.Size)) + 10.0f);
    if (config.ShowParagraphNumbers)
        gutter_w += (para_num_w = digit_w * ImMax(2, ImGuiProse_DigitCount(ImMax(ed->ParaCount, 1))) + 10.0f);
    if (gutter_w > 0.0f)
        gutter_w += 6.0f;
    const float text_left = gutter_w + pad;
    const float wrap_width = ImMax(avail.x - text_left - pad, 50.0f);

    ImGuiProse_UpdateLayout(ed, font, fs, wrap_width, config.WordWrap, rh);
    const ImVec2 origin = ImGui::GetCursorScreenPos();         // Top-left of the content, scroll included
    ed->ViewOrigin = origin;
    ed->ViewTextLeft = text_left;
    ed->ViewPad = pad;
    ed->ViewRowHeight = rh;
    ed->ViewFontSize = fs;
    ed->ViewFont = font;
    ed->ViewValid = true;

    // Mouse
    ImGuiIO& io = ImGui::GetIO();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    if (hovered && ImGui::IsMouseClicked(0))
    {
        ImGui::SetWindowFocus();
        ed->Focused = true;
        int line, col;
        ImGuiProse_PosFromMouse(ed, ImGuiProse_Mouse(ed, origin, text_left, pad), font, fs, rh, &line, &col);
        const int clicks = io.MouseClickedCount[0];
        if (clicks >= 3)                                        // Paragraph
        {
            ed->AnchorLine = line;
            ed->AnchorCol = 0;
            ed->CaretLine = line;
            ed->CaretCol = ed->Lines[line].Size;
        }
        else if (clicks == 2)                                   // Word
        {
            const ImGuiProseLine& l = ed->Lines[line];
            int a = col, b = col;
            if (ImGuiProse_IsWordAt(l, col) || ImGuiProse_IsWordAt(l, ImGuiProse_Utf8Prev(l.Data, col)))
            {
                a = ImGuiProse_WordLeft(l, ImGuiProse_WordRight(l, col) > col || col == l.Size ? ImGuiProse_Utf8Next(l.Data, l.Size, col) : col);
                a = ImMin(a, col);
                b = ImGuiProse_WordRight(l, a);
            }
            ed->AnchorLine = ed->CaretLine = line;
            ed->AnchorCol = a;
            ed->CaretCol = b;
        }
        else
            ImGuiProse_MoveCaret(ed, line, col, io.KeyShift);
        ed->Dragging = true;
        ed->PreferredX = -1.0f;
        ed->BlinkStart = ImGui::GetTime();
        ed->LastKind = 0;
    }
    if (ed->Dragging)
    {
        if (ImGui::IsMouseDown(0))
        {
            int line, col;
            ImGuiProse_PosFromMouse(ed, ImGuiProse_Mouse(ed, origin, text_left, pad), font, fs, rh, &line, &col);
            if (io.MouseClickedCount[0] < 2)
            {
                ed->CaretLine = line;
                ed->CaretCol = col;
                ed->BlinkStart = ImGui::GetTime();
            }
            const ImVec2 wpos = ImGui::GetWindowPos();
            if (io.MousePos.y < wpos.y)
                ImGui::SetScrollY(ImGui::GetScrollY() - (wpos.y - io.MousePos.y) * 0.3f - 2.0f);
            else if (io.MousePos.y > wpos.y + ImGui::GetWindowHeight())
                ImGui::SetScrollY(ImGui::GetScrollY() + (io.MousePos.y - wpos.y - ImGui::GetWindowHeight()) * 0.3f + 2.0f);
        }
        else
            ed->Dragging = false;
    }

    // Keyboard
    if (ed->Focused)
        ImGuiProse_HandleKeys(ed, config, font, fs, rh, avail.y);
    if (ed->Changed)
        ImGuiProse_UpdateLayout(ed, font, fs, wrap_width, config.WordWrap, rh);

    // Scroll synchronisation: did the view move by itself since the last frame, and requests of the host
    {
        const float scroll_y = ImGui::GetScrollY();
        ed->UserScrolled = (ImFabs(scroll_y - ed->PrevScrollY) > 0.5f) && ed->ProgrammaticFrames == 0;
        if (ed->ProgrammaticFrames > 0)
            ed->ProgrammaticFrames--;
        ed->PrevScrollY = scroll_y;
        ed->ScrollMaxY = ImGui::GetScrollMaxY();
        if (ed->PendingAnchor)
        {
            ed->PendingAnchor = false;
            if (ed->ParaLines.Size > 0)
            {
                const bool past_end = ed->PendingAnchorValue.Paragraph > ed->ParaLines.Size;
                const int para = ImClamp(ed->PendingAnchorValue.Paragraph, 1, ed->ParaLines.Size);
                const ImGuiProseLine& l = ed->Lines[ed->ParaLines[para - 1]];
                const float frac = past_end ? 1.0f : ImClamp(ed->PendingAnchorValue.Fraction, -1.0f, 1.0f);
                if (frac < 0.0f && para == 1)
                    ImGui::SetScrollY(ImMax(0.0f, (l.Y + pad) * (1.0f + frac)));        // Space above the first paragraph
                else
                    ImGui::SetScrollY(pad + l.Y + ImMax(frac, 0.0f) * l.Rows * rh);
                ed->ProgrammaticFrames = 2;
            }
        }
        else if (ed->PendingFraction)
        {
            ed->PendingFraction = false;
            ImGui::SetScrollY(ImClamp(ed->PendingFractionValue, 0.0f, 1.0f) * ed->ScrollMaxY);
            ed->ProgrammaticFrames = 2;
        }
    }

    // Scrolling requests
    if (ed->ScrollLine >= 0 && ed->ScrollLine < ed->Lines.Size)
    {
        ImGui::SetScrollY(ImMax(0.0f, pad + ed->Lines[ed->ScrollLine].Y - avail.y * ed->ScrollRatio));
        ed->ScrollLine = -1;
    }
    if (ed->ScrollToCaret)
    {
        ed->ScrollToCaret = false;
        int k;
        const float cx = ImGuiProse_CaretX(ed, font, fs, &k);
        const float cy = pad + ed->Lines[ed->CaretLine].Y + k * rh;
        const float top = ImGui::GetScrollY(), bottom = top + ImGui::GetWindowHeight() - ImGui::GetStyle().ScrollbarSize;
        if (cy < top + rh)
            ImGui::SetScrollY(ImMax(0.0f, cy - rh));
        else if (cy + 2.0f * rh > bottom)
            ImGui::SetScrollY(cy + 2.0f * rh - (bottom - top));
        if (!config.WordWrap)
        {
            const float left = ImGui::GetScrollX(), right = left + avail.x - gutter_w;
            if (cx < left + 20.0f)
                ImGui::SetScrollX(ImMax(0.0f, cx - 20.0f));
            else if (cx + text_left + 20.0f > right + gutter_w)
                ImGui::SetScrollX(cx + 40.0f - (right - left));
        }
    }

    // Content size (also moves the cursor, which gives the scroll bars)
    const float content_w = config.WordWrap ? avail.x : text_left + ed->MaxLineWidth + pad * 2.0f;
    const float content_h = ed->TotalHeight + pad * 2.0f + (config.ScrollPastEnd ? ImMax(0.0f, avail.y - rh * 4.0f) : 0.0f);
    ImGui::Dummy(ImVec2(content_w, content_h));

    // Anchor of the view for the synchronisation: paragraph at the top and fraction of it scrolled out
    {
        const float view_top_y = ImGui::GetScrollY() - pad;
        int n = ImGuiProse_LineAtY(ed, ImMax(view_top_y, 0.0f));
        float frac = 0.0f;
        if (!ed->Lines[n].Blank)
        {
            if (view_top_y < ed->Lines[n].Y)
                frac = ImClamp((view_top_y - ed->Lines[n].Y) / ImMax(ed->Lines[n].Y + pad, 1.0f), -1.0f, 0.0f);     // Space above the first paragraph still visible: -1 = top of the text, 0 = paragraph at the top
            else
                frac = ImClamp((view_top_y - ed->Lines[n].Y) / ImMax(ed->Lines[n].Rows * rh, 1.0f), 0.0f, 1.0f);
        }
        else
        {
            while (n < ed->Lines.Size && ed->Lines[n].Blank)
                n++;
            if (n >= ed->Lines.Size && ed->ParaLines.Size > 0)
            {
                n = ed->ParaLines.back();
                frac = 1.0f;
            }
            else if (n < ed->Lines.Size && ed->ParaLines.Size > 0 && n == ed->ParaLines[0] && view_top_y < ed->Lines[n].Y)
                frac = ImClamp((view_top_y - ed->Lines[n].Y) / ImMax(ed->Lines[n].Y + pad, 1.0f), -1.0f, 0.0f);       // Blank lines above the first paragraph
        }
        ed->CurrentAnchor = ImGuiProseScrollAnchor((n < ed->Lines.Size) ? ed->Lines[n].Para : 0, frac);
        ed->CurrentFraction = ed->ScrollMaxY > 0.0f ? ImClamp(ImGui::GetScrollY() / ed->ScrollMaxY, 0.0f, 1.0f) : 0.0f;
    }

    // Drawing
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 clip_min = dl->GetClipRectMin(), clip_max = dl->GetClipRectMax();
    const float view_top = clip_min.y - origin.y - pad, view_bottom = clip_max.y - origin.y - pad;
    int first = ImGuiProse_LineAtY(ed, view_top);
    const int current = config.HighlightLine > 0 ? config.HighlightLine - 1
        : ((config.HighlightCaretParagraph && !ed->Lines[ed->CaretLine].Blank) ? ed->CaretLine : -1);
    int sel_l0, sel_c0, sel_l1, sel_c1;
    ImGuiProse_GetSel(ed, &sel_l0, &sel_c0, &sel_l1, &sel_c1);
    const bool has_sel = ImGuiProse_HasSelection(ed);
    const float x0 = origin.x + text_left;
    const float full_right = ImMax(clip_max.x, origin.x + content_w);
    const double now = ImGui::GetTime();
    const bool caret_on = ed->Focused && !config.ReadOnly && fmod(now - ed->BlinkStart, 1.06) < 0.53;

    ImVector<int> rows;
    for (int n = first; n < ed->Lines.Size; n++)
    {
        const ImGuiProseLine& l = ed->Lines[n];
        if (l.Y > view_bottom)
            break;
        ImGuiProse_RowBreaks(ed, l, font, fs, wrap_width, config.WordWrap, &rows);
        const int span0 = ImGuiProse_FirstSpan(ed, n + 1);
        if (l.Pad > 0.0f)
        {
            // Spacer: lines which are not in the text, only there to keep the paragraphs facing the ones of the other editor
            const float sy = origin.y + pad + l.Y - l.Pad;
            if (sy + l.Pad >= clip_min.y && sy <= clip_max.y)
            {
                const ImVec2 a(origin.x + gutter_w, sy), b(full_right, sy + l.Pad);
                dl->AddRectFilled(a, b, colors.Spacer);
                dl->PushClipRect(a, b, true);
                for (float hx = a.x - l.Pad; hx < b.x; hx += 10.0f)
                    dl->AddLine(ImVec2(hx, b.y), ImVec2(hx + l.Pad, a.y), colors.SpacerLines);
                dl->PopClipRect();
            }
        }
        for (int k = 0; k < rows.Size; k++)
        {
            const float y = origin.y + pad + l.Y + k * rh;
            if (y + rh < clip_min.y || y > clip_max.y)
                continue;
            const int rs = rows[k];
            const int re = ImGuiProse_RowVisibleEnd(l, rows, k);
            const ImGuiProseShaper* sh = ed->Shaper;
            auto col_x = [&](int col) { return x0 + sh->ColumnToX(sh, l.Data, rs, re, ImClamp(col, rs, re), font, fs); };
            // Extents of the bytes [a, b) of the row, in screen coordinates (several when the directions are mixed)
            ImVector<ImVec2> ext;
            auto ranges = [&](int a, int b) { sh->GetRanges(sh, l.Data, rs, re, a, b, font, fs, &ext); for (int r = 0; r < ext.Size; r++) { ext[r].x += x0; ext[r].y += x0; } };

            // Backgrounds: current paragraph, whole line spans, word spans, selection
            if (n == current && config.HighlightBackground)
                dl->AddRectFilled(ImVec2(origin.x + gutter_w, y), ImVec2(full_right, y + rh), colors.CurrentBackground);
            for (int s = span0; s < ed->Spans.Size && ed->Spans[s].Line == n + 1; s++)
            {
                const ImGuiProseSpan& sp = ed->Spans[s];
                if (sp.Kind != ImGuiProseSpanKind_Background)
                    continue;
                if (sp.Start < 0 && sp.End < 0)
                    dl->AddRectFilled(ImVec2(origin.x + gutter_w, y), ImVec2(full_right, y + rh), sp.Color);
                else if (sp.End > rs && sp.Start < re + (k + 1 == rows.Size ? 1 : 0))
                {
                    ranges(sp.Start, sp.End);
                    for (int r = 0; r < ext.Size; r++)
                        dl->AddRectFilled(ImVec2(ext[r].x, y), ImVec2(ext[r].y, y + rh), sp.Color, 2.0f);
                }
            }
            if (has_sel && n >= sel_l0 && n <= sel_l1)
            {
                const int a = (n == sel_l0) ? sel_c0 : 0;
                const int b = (n == sel_l1) ? sel_c1 : l.Size;
                if (b > rs && a <= re && !(a == b && n != sel_l1))
                {
                    ranges(ImMax(a, rs), ImMin(b, re));
                    const float extra = (n < sel_l1 && k + 1 == rows.Size) ? digit_w * 0.6f : 0.0f;
                    for (int r = 0; r < ext.Size; r++)
                        dl->AddRectFilled(ImVec2(ext[r].x, y), ImVec2(ext[r].y + (r + 1 == ext.Size ? extra : 0.0f), y + rh), colors.Selection);
                }
                else if (l.Size == 0 && n < sel_l1)
                    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + digit_w * 0.6f, y + rh), colors.Selection);
            }

            // Markers of the no-break spaces, and with ShowInvisible of the other invisible characters, one colour by kind
            if (config.ShowNbsp || config.ShowInvisible)
                for (int c = rs; c < re; c++)
                {
                    if ((unsigned char)l.Data[c] < 0x80 && l.Data[c] != '\t')
                        continue;
                    int len = 0;
                    const int kind = ImGuiProse_InvisibleKind(l.Data + c, re - c, &len);
                    if (kind == 0 || (!config.ShowInvisible && kind > 2))
                        continue;
                    const ImU32 color = kind == 1 ? colors.Nbsp : kind == 2 ? colors.NarrowNbsp : kind == 3 ? colors.ThinSpace : kind == 4 ? colors.WideSpace
                        : kind == 5 ? colors.ZeroWidth : kind == 6 ? colors.SoftHyphen : colors.Control;
                    // A character without width (zero-width space, joiner, soft hyphen) is drawn as a thin bar, so it can be seen
                    ranges(c, c + len);
                    for (int r = 0; r < ext.Size; r++)
                    {
                        const float mx0 = ext[r].x, mx1 = ImMax(ext[r].y, mx0 + 2.0f);
                        dl->AddRectFilled(ImVec2(mx0, y + 2.0f), ImVec2(mx1, y + rh - 2.0f), color);
                    }
                    c += len - 1;
                }

            // Text, split by the color spans
            const float ty = y + (rh - fs) * 0.5f;
            const ImU32 base = (n == current && config.HighlightText && colors.CurrentText) ? colors.CurrentText : col_text;
            int pos = rs;
            for (int s = span0; s < ed->Spans.Size && ed->Spans[s].Line == n + 1; s++)
            {
                const ImGuiProseSpan& sp = ed->Spans[s];
                if (sp.Kind != ImGuiProseSpanKind_TextColor || sp.End <= pos || sp.Start >= re)
                    continue;
                const int a = ImMax(sp.Start, pos), b = ImMin(sp.End, re);
                if (a > pos)
                    sh->DrawRun(sh, dl, font, fs, ImVec2(x0, ty), l.Data, rs, re, pos, a, base);
                sh->DrawRun(sh, dl, font, fs, ImVec2(x0, ty), l.Data, rs, re, a, b, sp.Color);
                pos = b;
            }
            if (pos < re)
                sh->DrawRun(sh, dl, font, fs, ImVec2(x0, ty), l.Data, rs, re, pos, re, base);

            // Underlines and squiggles
            for (int s = span0; s < ed->Spans.Size && ed->Spans[s].Line == n + 1; s++)
            {
                const ImGuiProseSpan& sp = ed->Spans[s];
                if ((sp.Kind != ImGuiProseSpanKind_Squiggle && sp.Kind != ImGuiProseSpanKind_Underline) || sp.End <= rs || sp.Start >= re)
                    continue;
                ranges(ImMax(sp.Start, rs), ImMin(sp.End, re));
                for (int r = 0; r < ext.Size; r++)
                {
                    if (sp.Kind == ImGuiProseSpanKind_Squiggle)
                        ImGuiProse_Squiggle(dl, ext[r].x, ext[r].y, ty + fs + 1.0f, sp.Color);
                    else
                        dl->AddLine(ImVec2(ext[r].x, ty + fs), ImVec2(ext[r].y, ty + fs), sp.Color);
                }
            }

            // Caret
            if (caret_on && n == ed->CaretLine && ((ed->CaretCol >= rs && ed->CaretCol < (k + 1 < rows.Size ? rows[k + 1] : l.Size + 1))))
            {
                const float cx = col_x(ed->CaretCol);
                if (!config.ReadOnly)
                {
                    // Where the input method shows its composition and candidate windows
                    ImGuiPlatformImeData& ime = ImGui::GetCurrentContext()->PlatformImeData;
                    ime.WantVisible = true;
                    ime.ViewportId = ImGui::GetMainViewport()->ID;
                    ime.InputPos = ImVec2(cx, y);
                    ime.InputLineHeight = rh;
                }
                dl->AddLine(ImVec2(cx, y + 1.0f), ImVec2(cx, y + rh - 1.0f), colors.Caret ? colors.Caret : col_text, 1.5f);
            }
        }
    }

    // Gutter, fixed on the left of the view
    if (gutter_w > 0.0f)
    {
        const ImVec2 wpos = ImGui::GetWindowPos();
        dl->AddRectFilled(ImVec2(wpos.x, clip_min.y), ImVec2(wpos.x + gutter_w, clip_max.y), col_back | IM_COL32_A_MASK);    // Opaque: the text scrolls under it
        dl->AddRectFilled(ImVec2(wpos.x, clip_min.y), ImVec2(wpos.x + gutter_w, clip_max.y), colors.Gutter);
        for (int n = first; n < ed->Lines.Size; n++)
        {
            const ImGuiProseLine& l = ed->Lines[n];
            if (l.Y > view_bottom)
                break;
            const float y = origin.y + pad + l.Y + (rh - fs) * 0.5f;
            if (y + rh < clip_min.y)
                continue;
            char buf[16];
            float x = wpos.x;
            if (config.ShowLineNumbers)
            {
                snprintf(buf, sizeof(buf), "%d", n + 1);
                const float w = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf).x;
                dl->AddText(font, fs, ImVec2(x + line_num_w - w - 6.0f, y), colors.LineNumber, buf);
                x += line_num_w;
            }
            if (config.ShowParagraphNumbers && l.Para > 0)
            {
                snprintf(buf, sizeof(buf), "%d", l.Para);
                const float w = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf).x;
                dl->AddText(font, fs, ImVec2(x + para_num_w - w - 6.0f, y), colors.ParagraphNumber, buf);
            }
        }
    }

    if (config.Font)
        ImGui::PopFont();
    ImGui::EndChild();
    return ed->Changed;
}

//-----------------------------------------------------------------------------
// [SECTION] Public API
//-----------------------------------------------------------------------------

ImGuiProseEditor* ImGuiProse::CreateEditor()
{
    ImGuiProseEditor* ed = IM_NEW(ImGuiProseEditor)();
    ed->Shaper = ed->LayoutShaper = ImGuiProse::GetDefaultShaper();
    return ed;
}

void ImGuiProse::DestroyEditor(ImGuiProseEditor* editor)
{
    IM_DELETE(editor);
}

void ImGuiProse::SetText(ImGuiProseEditor* ed, const char* text, const char* text_end)
{
    if (text_end == NULL)
        text_end = text + strlen(text);
    if (text_end - text >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text += 3;                                      // UTF-8 byte order mark
    for (int n = 0; n < ed->Lines.Size; n++)
        ImGuiProseLine_Free(&ed->Lines[n]);
    ed->Lines.resize(0);
    ImGuiProseLine line;
    ImGuiProseLine_Init(&line);
    for (const char* p = text; p < text_end; p++)
    {
        if (*p == '\r')
            continue;
        if (*p == '\n')
        {
            ed->Lines.push_back(line);
            ImGuiProseLine_Init(&line);
            continue;
        }
        ImGuiProseLine_Insert(&line, line.Size, p, 1);
    }
    ed->Lines.push_back(line);
    ed->CaretLine = ed->CaretCol = ed->AnchorLine = ed->AnchorCol = 0;
    ed->Undo.resize(0);
    ed->UndoText.resize(0);
    ed->UndoIndex = 0;
    ed->LastKind = 0;
    ed->PreferredX = -1.0f;
    ed->LayoutFont = NULL;
    ed->Spans.resize(0);
    ed->Version++;
    ed->ScrollLine = 0;
    ed->ScrollRatio = 0.0f;
}

void ImGuiProse::GetText(ImGuiProseEditor* ed, ImVector<char>* out)
{
    out->resize(0);
    for (int n = 0; n < ed->Lines.Size; n++)
    {
        for (int k = 0; k < ed->Lines[n].Size; k++)
            out->push_back(ed->Lines[n].Data[k]);
        if (n + 1 < ed->Lines.Size)
            out->push_back('\n');
    }
    out->push_back(0);
}

int ImGuiProse::GetLineCount(ImGuiProseEditor* ed)      { return ed->Lines.Size; }
int ImGuiProse::GetVersion(ImGuiProseEditor* ed)        { return ed->Version; }
bool ImGuiProse::HasFocus(ImGuiProseEditor* ed)         { return ed->Focused; }

const char* ImGuiProse::GetLine(ImGuiProseEditor* ed, int line, int* out_length)
{
    line = ImClamp(line - 1, 0, ed->Lines.Size - 1);
    if (out_length)
        *out_length = ed->Lines[line].Size;
    return ed->Lines[line].Data;
}

int ImGuiProse::GetParagraphOfLine(ImGuiProseEditor* ed, int line)
{
    line = line - 1;
    return (line >= 0 && line < ed->Lines.Size && !ImGuiProse_IsBlankLine(ed->Lines[line])) ? ed->Lines[line].Para : 0;
}

int ImGuiProse::GetParagraphCount(ImGuiProseEditor* ed) { return ed->ParaCount; }

int ImGuiProse::GetLineOfParagraph(ImGuiProseEditor* ed, int paragraph)
{
    return (paragraph >= 1 && paragraph <= ed->ParaLines.Size) ? ed->ParaLines[paragraph - 1] + 1 : 0;
}

void ImGuiProse::Replace(ImGuiProseEditor* ed, ImGuiProsePos start, ImGuiProsePos end, const char* text, const char* text_end)
{
    if (text_end == NULL)
        text_end = text + strlen(text);
    ImVector<char> clean;
    ImGuiProse_Normalize(text, (int)(text_end - text), 4, &clean);
    ImGuiProse_Edit(ed, start.Line - 1, start.Column, end.Line - 1, end.Column, clean.Data, clean.Size, 0);
}

bool ImGuiProse::Undo(ImGuiProseEditor* ed)             { return ImGuiProse_DoUndo(ed); }
bool ImGuiProse::Redo(ImGuiProseEditor* ed)             { return ImGuiProse_DoRedo(ed); }

ImGuiProsePos ImGuiProse::GetCaret(ImGuiProseEditor* ed)
{
    return ImGuiProsePos(ed->CaretLine + 1, ed->CaretCol);
}

void ImGuiProse::SetCaret(ImGuiProseEditor* ed, ImGuiProsePos pos, bool scroll_to)
{
    ImGuiProse_MoveCaret(ed, pos.Line - 1, pos.Column, false);
    ed->ScrollToCaret = scroll_to;
}

void ImGuiProse::SetSelection(ImGuiProseEditor* ed, ImGuiProsePos start, ImGuiProsePos end)
{
    int al = start.Line - 1, ac = start.Column, cl = end.Line - 1, cc = end.Column;
    ImGuiProse_Clamp(ed, &al, &ac);
    ImGuiProse_Clamp(ed, &cl, &cc);
    ed->AnchorLine = al;
    ed->AnchorCol = ac;
    ed->CaretLine = cl;
    ed->CaretCol = cc;
}

bool ImGuiProse::GetSelection(ImGuiProseEditor* ed, ImGuiProsePos* out_start, ImGuiProsePos* out_end)
{
    if (!ImGuiProse_HasSelection(ed))
        return false;
    int l0, c0, l1, c1;
    ImGuiProse_GetSel(ed, &l0, &c0, &l1, &c1);
    if (out_start)
        *out_start = ImGuiProsePos(l0 + 1, c0);
    if (out_end)
        *out_end = ImGuiProsePos(l1 + 1, c1);
    return true;
}

void ImGuiProse::ScrollToLine(ImGuiProseEditor* ed, int line, float ratio)
{
    ed->ScrollLine = ImClamp(line - 1, 0, ed->Lines.Size - 1);
    ed->ScrollRatio = ratio;
    ed->ScrollToCaret = false;                  // The caret is judged against the previous scroll position: it would move the view again
}

bool ImGuiProse::GetPositionFromScreen(ImGuiProseEditor* ed, ImVec2 screen_pos, ImGuiProsePos* out)
{
    if (!ed->ViewValid)
        return false;
    int line, col;
    const ImVec2 rel(screen_pos.x - ed->ViewOrigin.x - ed->ViewTextLeft, screen_pos.y - ed->ViewOrigin.y - ed->ViewPad);
    ImGuiProse_PosFromMouse(ed, rel, ed->ViewFont, ed->ViewFontSize, ed->ViewRowHeight, &line, &col);
    *out = ImGuiProsePos(line + 1, col);
    return true;
}

void ImGuiProse::SetParagraphSpacers(ImGuiProseEditor* ed, const float* heights, int count)
{
    ed->Spacers.resize(count);
    for (int i = 0; i < count; i++)
        ed->Spacers[i] = heights[i] > 0.0f ? heights[i] : 0.0f;
}

bool ImGuiProse::GetParagraphGeometry(ImGuiProseEditor* ed, int paragraph, float* top, float* height)
{
    if (paragraph < 1 || paragraph > ed->ParaLines.Size)
        return false;
    const ImGuiProseLine& l = ed->Lines[ed->ParaLines[paragraph - 1]];
    if (top)
        *top = l.Y - l.PadSum;                  // As if there were no spacer
    if (height)
        *height = ed->TotalHeight > 0.0f ? ed->ViewRowHeight * l.Rows : 0.0f;
    return true;
}

ImGuiProseScrollAnchor ImGuiProse::GetScrollAnchor(ImGuiProseEditor* ed)    { return ed->CurrentAnchor; }
float ImGuiProse::GetScrollFraction(ImGuiProseEditor* ed)                   { return ed->CurrentFraction; }
bool ImGuiProse::ScrolledByUser(ImGuiProseEditor* ed)                       { return ed->UserScrolled; }

void ImGuiProse::SetScrollAnchor(ImGuiProseEditor* ed, ImGuiProseScrollAnchor anchor)
{
    ed->PendingAnchor = true;
    ed->PendingAnchorValue = anchor;
}

void ImGuiProse::SetScrollFraction(ImGuiProseEditor* ed, float fraction)
{
    ed->PendingFraction = true;
    ed->PendingFractionValue = fraction;
}

void ImGuiProse::SetSpans(ImGuiProseEditor* ed, const ImGuiProseSpan* spans, int count)
{
    ed->Spans.resize(count);
    if (count > 0)
    {
        memcpy(ed->Spans.Data, spans, count * sizeof(ImGuiProseSpan));
        qsort(ed->Spans.Data, count, sizeof(ImGuiProseSpan), ImGuiProse_SpanCompare);
    }
}
