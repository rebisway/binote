// imgui_md - www.rebisway.com - contact@rebisway.com
// Copyright (c) 2026 Thomas Gourgnier
// SPDX-License-Identifier: MIT
//
// Licensed under the MIT license: see the LICENSE file in the folder of this component.

// imgui_md : Markdown rendering for Dear ImGui (MIT license)

// Index of this file:
// [SECTION] Document structures
// [SECTION] Parsing (md4c callbacks)
// [SECTION] Layout and drawing of inline runs
// [SECTION] Layout and drawing of blocks
// [SECTION] Public API

#include "imgui_md.h"
#include "imgui_internal.h"
#include "md4c.h"
#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// [SECTION] Document structures
//-----------------------------------------------------------------------------

enum ImGuiMdRunFlags_
{
    ImGuiMdRunFlags_Bold        = 1 << 0,
    ImGuiMdRunFlags_Italic      = 1 << 1,
    ImGuiMdRunFlags_Code        = 1 << 2,
    ImGuiMdRunFlags_Strike      = 1 << 3,
    ImGuiMdRunFlags_Link        = 1 << 4,
    ImGuiMdRunFlags_Image       = 1 << 5,
    ImGuiMdRunFlags_HardBreak   = 1 << 6,
    ImGuiMdRunFlags_SoftBreak   = 1 << 7,
};

enum ImGuiMdLeafType_
{
    ImGuiMdLeafType_Paragraph,
    ImGuiMdLeafType_Heading,
    ImGuiMdLeafType_Code,
    ImGuiMdLeafType_HRule,
    ImGuiMdLeafType_Table,
};

enum ImGuiMdMarkerKind_
{
    ImGuiMdMarkerKind_None,
    ImGuiMdMarkerKind_Bullet,
    ImGuiMdMarkerKind_Text,         // "1." or "[x]"
};

struct ImGuiMdRun
{
    int     TextOffset;             // In ImGuiMdDocument::Pool
    int     TextLength;
    int     Flags;
    int     Link;                   // Index in ImGuiMdDocument::Links, or -1
};

struct ImGuiMdLink
{
    int     UrlOffset;              // In ImGuiMdDocument::Pool, zero terminated
    int     TitleOffset;            // Same, -1 if the link has no title
    float   Width;                  // Requested width of an HTML <img width="..."> in pixels, 0 if none (inline image)
};

struct ImGuiMdCell
{
    int     RunStart;
    int     RunCount;
    int     Row;
    int     Col;
    bool    Header;
    int     Align;                  // MD_ALIGN_*
};

// A leaf is a block holding inline content (or nothing), already flattened: quotes and lists only add indentation and decorations.
struct ImGuiMdLeaf
{
    int     Type;
    int     Level;                  // Heading level
    int     Depth;                  // Number of indentations (lists + quotes)
    int     QuoteDepth;
    int     RunStart, RunCount;
    int     CellStart, CellCount, ColCount;
    int     MarkerKind;
    int     MarkerOffset, MarkerLength;
    int     StartLine, EndLine;     // 1-based source lines, 0 if unknown
    int     LangOffset, LangLength; // Code block: info string language, in Pool
    int     CodeOffset, CodeLength; // Code block: the whole text in one piece, in Pool
    bool    Tight;                  // Item of a tight list: smaller spacing
    float   Y;                      // Layout, relative to the top of the document
    float   Height;
};

struct ImGuiMdCacheKey
{
    float       Width, FontSize, IndentSpacing, ParagraphSpacing, LineSpacing;
    int         SoftBreakAsNewLine;
    ImFont*     Fonts[11];
};

struct ImGuiMdDocument
{
    ImVector<char>          Source;
    ImVector<char>          Pool;
    ImVector<int>           LineStarts;
    ImVector<ImGuiMdRun>    Runs;
    ImVector<ImGuiMdLink>   Links;
    ImVector<ImGuiMdCell>   Cells;
    ImVector<ImGuiMdLeaf>   Leaves;
    ImGuiMdCacheKey         CacheKey;
    bool                    CacheValid;
    float                   TotalHeight;

    ImGuiMdDocument()       { CacheValid = false; TotalHeight = 0.0f; memset(&CacheKey, 0, sizeof(CacheKey)); }
    int                     PoolAdd(const char* text, int length);
    int                     LineOf(int offset) const;
};

int ImGuiMdDocument::PoolAdd(const char* text, int length)
{
    int offset = Pool.Size;
    Pool.resize(Pool.Size + length + 1);
    if (length > 0)
        memcpy(Pool.Data + offset, text, length);
    Pool[offset + length] = 0;
    return offset;
}

// 1-based line of a byte offset of the source
int ImGuiMdDocument::LineOf(int offset) const
{
    int lo = 0, hi = LineStarts.Size - 1;
    while (lo < hi)
    {
        int mid = (lo + hi + 1) / 2;
        if (LineStarts[mid] <= offset)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo + 1;
}

//-----------------------------------------------------------------------------
// [SECTION] Parsing (md4c callbacks)
//-----------------------------------------------------------------------------

struct ImGuiMdListInfo
{
    bool    Ordered;
    bool    Tight;
    int     Number;
};

struct ImGuiMdParser
{
    ImGuiMdDocument*        Doc;
    ImVector<ImGuiMdListInfo> Lists;
    int                     QuoteDepth;
    int                     Cur;                // Leaf being filled, -1 if none
    bool                    CurImplicit;        // Paragraph created for the text of a tight list item (md4c emits no paragraph block)
    int                     CurCell;
    int                     CellRow, CellCol;
    int                     MarkerKind, MarkerOffset, MarkerLength;
    int                     Bold, Italic, Strike;
    int                     LinkIdx;
    bool                    InImage, InHtml;
    int                     ImageLink;
    ImVector<char>          Alt;
    int                     LastLine;

    ImGuiMdParser()
    {
        Doc = NULL; QuoteDepth = 0; Cur = -1; CurImplicit = false; CurCell = -1; CellRow = CellCol = 0;
        MarkerKind = ImGuiMdMarkerKind_None; MarkerOffset = MarkerLength = 0;
        Bold = Italic = Strike = 0; LinkIdx = -1; InImage = InHtml = false; ImageLink = -1; LastLine = 0;
    }

    void BeginLeaf(int type, int level);
    void EndLeaf();
    void FlushImplicit()    { if (Cur >= 0 && CurImplicit) EndLeaf(); }
    void TouchLines(const char* text, int size);
    void AddRun(const char* text, int size, int flags);
    void AddHtml(const char* text, int size);
};

// Value of an attribute of an HTML tag (name="value" or name='value'), empty if absent
static bool ImGuiMd_HtmlAttr(const char* tag, const char* tag_end, const char* name, const char** out, int* out_len)
{
    const int name_len = (int)strlen(name);
    for (const char* s = tag; s + name_len + 2 < tag_end; s++)
    {
        const bool boundary = (s == tag) || s[-1] == ' ' || s[-1] == '\t' || s[-1] == '\n' || s[-1] == '\r';
        if (!boundary || ImStrnicmp(s, name, name_len) != 0)
            continue;
        const char* q = s + name_len;
        while (q < tag_end && *q == ' ')
            q++;
        if (q >= tag_end || *q != '=')
            continue;
        q++;
        while (q < tag_end && *q == ' ')
            q++;
        if (q >= tag_end)
            return false;
        const char quote = (*q == '"' || *q == '\'') ? *q++ : 0;
        const char* v = q;
        while (q < tag_end && (quote ? *q != quote : (*q != ' ' && *q != '>' && *q != '/')))
            q++;
        *out = v;
        *out_len = (int)(q - v);
        return true;
    }
    return false;
}

// Raw HTML: only <img src alt width title> is understood (readme-style rows of screenshots), the other tags are dropped
void ImGuiMdParser::AddHtml(const char* text, int size)
{
    const char* end = text + size;
    for (const char* s = text; s < end; s++)
    {
        if (*s != '<' || end - s < 5 || ImStrnicmp(s, "<img", 4) != 0 || !(s[4] == ' ' || s[4] == '\t' || s[4] == '\n' || s[4] == '\r'))
            continue;
        const char* tag_end = (const char*)memchr(s, '>', end - s);
        if (!tag_end)
            break;
        const char* src = NULL; int src_len = 0;
        if (ImGuiMd_HtmlAttr(s, tag_end, "src", &src, &src_len) && src_len > 0)
        {
            const char* alt = ""; int alt_len = 0;
            const char* title = NULL; int title_len = 0;
            const char* width = NULL; int width_len = 0;
            ImGuiMd_HtmlAttr(s, tag_end, "alt", &alt, &alt_len);
            ImGuiMd_HtmlAttr(s, tag_end, "title", &title, &title_len);
            ImGuiMd_HtmlAttr(s, tag_end, "width", &width, &width_len);
            ImGuiMdLink link;
            link.Width = 0.0f;
            if (width_len > 0 && width_len < 12)
            {
                char tmp[16];
                memcpy(tmp, width, width_len);
                tmp[width_len] = 0;
                link.Width = (float)atof(tmp);
            }
            link.UrlOffset = Doc->PoolAdd(src, src_len);
            link.TitleOffset = title_len > 0 ? Doc->PoolAdd(title, title_len) : -1;
            Doc->Links.push_back(link);
            if (Cur < 0)
            {
                BeginLeaf(ImGuiMdLeafType_Paragraph, 0);
                CurImplicit = true;
            }
            TouchLines(s, (int)(tag_end - s));
            const int saved = LinkIdx;
            LinkIdx = Doc->Links.Size - 1;
            AddRun(alt, alt_len, ImGuiMdRunFlags_Image);
            LinkIdx = saved;
        }
        s = tag_end;
    }
}

void ImGuiMdParser::BeginLeaf(int type, int level)
{
    FlushImplicit();
    ImGuiMdLeaf leaf;
    memset(&leaf, 0, sizeof(leaf));
    leaf.Type = type;
    leaf.Level = level;
    leaf.Depth = Lists.Size + QuoteDepth;
    leaf.QuoteDepth = QuoteDepth;
    leaf.RunStart = Doc->Runs.Size;
    leaf.CellStart = Doc->Cells.Size;
    leaf.MarkerKind = MarkerKind;
    leaf.MarkerOffset = MarkerOffset;
    leaf.MarkerLength = MarkerLength;
    leaf.Tight = Lists.Size > 0 && Lists.back().Tight;
    MarkerKind = ImGuiMdMarkerKind_None;
    Doc->Leaves.push_back(leaf);
    Cur = Doc->Leaves.Size - 1;
    CurImplicit = false;
}

void ImGuiMdParser::EndLeaf()
{
    if (Cur < 0)
        return;
    ImGuiMdLeaf& leaf = Doc->Leaves[Cur];
    leaf.RunCount = Doc->Runs.Size - leaf.RunStart;
    leaf.CellCount = Doc->Cells.Size - leaf.CellStart;
    if (leaf.StartLine == 0)
        leaf.StartLine = LastLine + 1;
    if (leaf.EndLine < leaf.StartLine)
        leaf.EndLine = leaf.StartLine;

    // Drop the final line break of a code block
    if (leaf.Type == ImGuiMdLeafType_Code && leaf.RunCount > 0)
    {
        ImGuiMdRun& run = Doc->Runs[leaf.RunStart + leaf.RunCount - 1];
        if (run.TextLength > 0 && Doc->Pool[run.TextOffset + run.TextLength - 1] == '\n')
            run.TextLength--;
    }

    // The text of a code block in one piece, for the code block callback
    if (leaf.Type == ImGuiMdLeafType_Code)
    {
        int total = 0;
        for (int n = 0; n < leaf.RunCount; n++)
            total += Doc->Runs[leaf.RunStart + n].TextLength;
        const int offset = Doc->Pool.Size;
        Doc->Pool.resize(offset + total + 1);
        int out = offset;
        for (int n = 0; n < leaf.RunCount; n++)
        {
            const ImGuiMdRun& r = Doc->Runs[leaf.RunStart + n];
            memcpy(Doc->Pool.Data + out, Doc->Pool.Data + r.TextOffset, r.TextLength);
            out += r.TextLength;
        }
        Doc->Pool[out] = 0;
        leaf.CodeOffset = offset;
        leaf.CodeLength = total;
    }
    Cur = -1;
    CurImplicit = false;
}

// Source lines covered by a piece of text which points into the source buffer
void ImGuiMdParser::TouchLines(const char* text, int size)
{
    const char* base = Doc->Source.Data;
    if (Cur < 0 || text < base || text > base + Doc->Source.Size)
        return;
    int offset = (int)(text - base);
    int line_first = Doc->LineOf(offset);
    int line_last = Doc->LineOf(offset + (size > 0 ? size - 1 : 0));
    ImGuiMdLeaf& leaf = Doc->Leaves[Cur];
    if (leaf.StartLine == 0)
        leaf.StartLine = line_first;
    if (line_last > leaf.EndLine)
        leaf.EndLine = line_last;
    LastLine = line_last;
}

void ImGuiMdParser::AddRun(const char* text, int size, int flags)
{
    ImGuiMdRun run;
    run.TextOffset = Doc->Pool.Size;
    run.TextLength = size;
    run.Flags = flags;
    run.Link = LinkIdx;
    if (LinkIdx >= 0)
        run.Flags |= ImGuiMdRunFlags_Link;
    // Tabs would have no width in the font
    for (int n = 0; n < size; n++)
    {
        if (text[n] == '\t')
            for (int k = 0; k < 4; k++)
                Doc->Pool.push_back(' ');
        else
            Doc->Pool.push_back(text[n]);
    }
    run.TextLength = Doc->Pool.Size - run.TextOffset;
    Doc->Pool.push_back(0);
    Doc->Runs.push_back(run);
}

static int ImGuiMd_StyleFlags(const ImGuiMdParser* p)
{
    int flags = 0;
    if (p->Bold > 0)    flags |= ImGuiMdRunFlags_Bold;
    if (p->Italic > 0)  flags |= ImGuiMdRunFlags_Italic;
    if (p->Strike > 0)  flags |= ImGuiMdRunFlags_Strike;
    return flags;
}

static int ImGuiMd_EnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    ImGuiMdParser* p = (ImGuiMdParser*)userdata;
    ImGuiMdDocument* doc = p->Doc;
    switch (type)
    {
    case MD_BLOCK_QUOTE:
        p->FlushImplicit();
        p->QuoteDepth++;
        break;
    case MD_BLOCK_UL:
    {
        p->FlushImplicit();
        ImGuiMdListInfo info = { false, ((MD_BLOCK_UL_DETAIL*)detail)->is_tight != 0, 0 };
        p->Lists.push_back(info);
        break;
    }
    case MD_BLOCK_OL:
    {
        p->FlushImplicit();
        MD_BLOCK_OL_DETAIL* d = (MD_BLOCK_OL_DETAIL*)detail;
        ImGuiMdListInfo info = { true, d->is_tight != 0, (int)d->start };
        p->Lists.push_back(info);
        break;
    }
    case MD_BLOCK_LI:
    {
        p->FlushImplicit();
        MD_BLOCK_LI_DETAIL* d = (MD_BLOCK_LI_DETAIL*)detail;
        char buf[32];
        if (d->is_task)
        {
            int n = snprintf(buf, sizeof(buf), "%s", (d->task_mark == 'x' || d->task_mark == 'X') ? "[x]" : "[ ]");
            p->MarkerKind = ImGuiMdMarkerKind_Text;
            p->MarkerOffset = doc->PoolAdd(buf, n);
            p->MarkerLength = n;
        }
        else if (!p->Lists.empty() && p->Lists.back().Ordered)
        {
            int n = snprintf(buf, sizeof(buf), "%d.", p->Lists.back().Number++);
            p->MarkerKind = ImGuiMdMarkerKind_Text;
            p->MarkerOffset = doc->PoolAdd(buf, n);
            p->MarkerLength = n;
        }
        else
        {
            p->MarkerKind = ImGuiMdMarkerKind_Bullet;
        }
        break;
    }
    case MD_BLOCK_HR:
        p->BeginLeaf(ImGuiMdLeafType_HRule, 0);
        p->EndLeaf();
        break;
    case MD_BLOCK_H:
        p->BeginLeaf(ImGuiMdLeafType_Heading, ((MD_BLOCK_H_DETAIL*)detail)->level);
        break;
    case MD_BLOCK_CODE:
    {
        p->BeginLeaf(ImGuiMdLeafType_Code, 0);
        MD_BLOCK_CODE_DETAIL* d = (MD_BLOCK_CODE_DETAIL*)detail;
        if (d->lang.size > 0)
        {
            doc->Leaves[p->Cur].LangOffset = doc->PoolAdd(d->lang.text, (int)d->lang.size);
            doc->Leaves[p->Cur].LangLength = (int)d->lang.size;
        }
        break;
    }
    case MD_BLOCK_HTML:
        p->InHtml = true;
        break;
    case MD_BLOCK_P:
        p->BeginLeaf(ImGuiMdLeafType_Paragraph, 0);
        break;
    case MD_BLOCK_TABLE:
        p->BeginLeaf(ImGuiMdLeafType_Table, 0);
        doc->Leaves[p->Cur].ColCount = (int)((MD_BLOCK_TABLE_DETAIL*)detail)->col_count;
        p->CellRow = -1;
        break;
    case MD_BLOCK_TR:
        p->CellRow++;
        p->CellCol = 0;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
    {
        ImGuiMdCell cell;
        cell.RunStart = doc->Runs.Size;
        cell.RunCount = 0;
        cell.Row = p->CellRow;
        cell.Col = p->CellCol++;
        cell.Header = (type == MD_BLOCK_TH);
        cell.Align = (int)((MD_BLOCK_TD_DETAIL*)detail)->align;
        doc->Cells.push_back(cell);
        p->CurCell = doc->Cells.Size - 1;
        break;
    }
    default:
        break;
    }
    return 0;
}

static int ImGuiMd_LeaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    IM_UNUSED(detail);
    ImGuiMdParser* p = (ImGuiMdParser*)userdata;
    ImGuiMdDocument* doc = p->Doc;
    switch (type)
    {
    case MD_BLOCK_QUOTE:
        p->FlushImplicit();
        p->QuoteDepth--;
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        p->FlushImplicit();
        p->Lists.pop_back();
        break;
    case MD_BLOCK_LI:
        p->FlushImplicit();
        p->MarkerKind = ImGuiMdMarkerKind_None;
        break;
    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
    case MD_BLOCK_P:
    case MD_BLOCK_TABLE:
        p->EndLeaf();
        break;
    case MD_BLOCK_HTML:
        p->FlushImplicit();
        p->InHtml = false;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        if (p->CurCell >= 0)
            doc->Cells[p->CurCell].RunCount = doc->Runs.Size - doc->Cells[p->CurCell].RunStart;
        p->CurCell = -1;
        break;
    default:
        break;
    }
    return 0;
}

static int ImGuiMd_EnterSpan(MD_SPANTYPE type, void* detail, void* userdata)
{
    ImGuiMdParser* p = (ImGuiMdParser*)userdata;
    switch (type)
    {
    case MD_SPAN_EM:        p->Italic++; break;
    case MD_SPAN_STRONG:    p->Bold++; break;
    case MD_SPAN_DEL:       p->Strike++; break;
    case MD_SPAN_A:
    {
        MD_SPAN_A_DETAIL* d = (MD_SPAN_A_DETAIL*)detail;
        ImGuiMdLink link;
        link.Width = 0.0f;
        link.UrlOffset = p->Doc->PoolAdd(d->href.text, (int)d->href.size);
        link.TitleOffset = d->title.size > 0 ? p->Doc->PoolAdd(d->title.text, (int)d->title.size) : -1;
        p->Doc->Links.push_back(link);
        p->LinkIdx = p->Doc->Links.Size - 1;
        break;
    }
    case MD_SPAN_IMG:
    {
        MD_SPAN_IMG_DETAIL* d = (MD_SPAN_IMG_DETAIL*)detail;
        ImGuiMdLink link;
        link.Width = 0.0f;
        link.UrlOffset = p->Doc->PoolAdd(d->src.text, (int)d->src.size);
        link.TitleOffset = d->title.size > 0 ? p->Doc->PoolAdd(d->title.text, (int)d->title.size) : -1;
        p->Doc->Links.push_back(link);
        p->ImageLink = p->Doc->Links.Size - 1;
        p->InImage = true;
        p->Alt.resize(0);
        break;
    }
    default:
        break;
    }
    return 0;
}

static int ImGuiMd_LeaveSpan(MD_SPANTYPE type, void* detail, void* userdata)
{
    IM_UNUSED(detail);
    ImGuiMdParser* p = (ImGuiMdParser*)userdata;
    switch (type)
    {
    case MD_SPAN_EM:        p->Italic--; break;
    case MD_SPAN_STRONG:    p->Bold--; break;
    case MD_SPAN_DEL:       p->Strike--; break;
    case MD_SPAN_A:         p->LinkIdx = -1; break;
    case MD_SPAN_IMG:
    {
        if (p->Cur < 0)
        {
            p->BeginLeaf(ImGuiMdLeafType_Paragraph, 0);
            p->CurImplicit = true;
        }
        int saved = p->LinkIdx;
        p->LinkIdx = p->ImageLink;
        p->AddRun(p->Alt.Data, p->Alt.Size, ImGuiMd_StyleFlags(p) | ImGuiMdRunFlags_Image);
        p->LinkIdx = saved;
        p->InImage = false;
        break;
    }
    default:
        break;
    }
    return 0;
}

// Decode the entities worth it (&nbsp; &amp; &#233; ...). Returns the length written in out (at most 8 bytes), 0 if unknown.
static int ImGuiMd_DecodeEntity(const char* text, int size, char* out)
{
    unsigned int c = 0;
    if (size >= 4 && text[1] == '#')
    {
        const bool hex = (text[2] == 'x' || text[2] == 'X');
        char tmp[16];
        int n = size - (hex ? 4 : 3);
        if (n <= 0 || n >= 12)
            return 0;
        memcpy(tmp, text + (hex ? 3 : 2), n);
        tmp[n] = 0;
        c = (unsigned int)strtoul(tmp, NULL, hex ? 16 : 10);
    }
    else if (size == 6 && strncmp(text, "&nbsp;", 6) == 0)  c = 0x00A0;
    else if (size == 5 && strncmp(text, "&amp;", 5) == 0)   c = '&';
    else if (size == 4 && strncmp(text, "&lt;", 4) == 0)    c = '<';
    else if (size == 4 && strncmp(text, "&gt;", 4) == 0)    c = '>';
    else if (size == 6 && strncmp(text, "&quot;", 6) == 0)  c = '"';
    else if (size == 6 && strncmp(text, "&apos;", 6) == 0)  c = '\'';
    else if (size == 7 && strncmp(text, "&laquo;", 7) == 0) c = 0x00AB;
    else if (size == 7 && strncmp(text, "&raquo;", 7) == 0) c = 0x00BB;
    else
        return 0;
    if (c == 0)
        return 0;
    char buf[5];
    ImTextCharToUtf8(buf, c);                       // Zero terminated
    int n = (int)strlen(buf);
    memcpy(out, buf, n);
    return n;
}

static int ImGuiMd_Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    ImGuiMdParser* p = (ImGuiMdParser*)userdata;
    if (type == MD_TEXT_HTML)
    {
        if (!p->InImage)
            p->AddHtml(text, (int)size);
        return 0;
    }
    if (p->InHtml)
        return 0;

    if (p->InImage)
    {
        if (type == MD_TEXT_NORMAL || type == MD_TEXT_CODE)
            for (MD_SIZE n = 0; n < size; n++)
                p->Alt.push_back(text[n]);
        return 0;
    }

    if (p->Cur < 0)
    {
        p->BeginLeaf(ImGuiMdLeafType_Paragraph, 0);
        p->CurImplicit = true;
    }

    const int flags = ImGuiMd_StyleFlags(p);
    switch (type)
    {
    case MD_TEXT_NORMAL:
        p->TouchLines(text, (int)size);
        p->AddRun(text, (int)size, flags);
        break;
    case MD_TEXT_CODE:
        p->TouchLines(text, (int)size);
        p->AddRun(text, (int)size, flags | ImGuiMdRunFlags_Code);
        break;
    case MD_TEXT_ENTITY:
    {
        char out[8];
        int n = ImGuiMd_DecodeEntity(text, (int)size, out);
        if (n > 0)
            p->AddRun(out, n, flags);
        else
            p->AddRun(text, (int)size, flags);
        break;
    }
    case MD_TEXT_BR:
        p->AddRun("", 0, ImGuiMdRunFlags_HardBreak);
        break;
    case MD_TEXT_SOFTBR:
        p->AddRun("", 0, ImGuiMdRunFlags_SoftBreak);
        break;
    default:
        break;
    }
    return 0;
}

//-----------------------------------------------------------------------------
// [SECTION] Layout and drawing of inline runs
//-----------------------------------------------------------------------------

// State shared by the layout and the drawing of a flow of inline runs.
// The same code measures (Draw = false) and draws, so both always agree.
struct ImGuiMdFlow
{
    const ImGuiMdDocument*  Doc;
    const ImGuiMdConfig*    Config;
    ImDrawList*             DrawList;
    ImVec2                  Origin;             // Screen position of the top-left corner of the flow
    float                   WrapWidth;
    float                   Scale;
    bool                    Draw;
    bool                    KeepSpaces;         // Code: leading spaces are kept
    bool                    ForceBold;
    ImFont*                 ForceFont;          // Heading font
    ImU32                   TextColor;
    ImVec2                  Mouse;
    bool                    MouseValid;
    int                     HoverLink;          // [Out]
    const char*             HoverTip;           // [Out] Tooltip of the hovered link or image
    float                   UsedWidth;          // [Out] Widest line
};

// Choose the font of a run. When a style has no dedicated font it is faked: bold is drawn twice, italic is sheared.
static ImFont* ImGuiMd_PickFont(const ImGuiMdConfig& config, int flags, bool force_bold, ImFont* force_font, bool* faux_bold, bool* faux_italic)
{
    ImFont* regular = config.Fonts.Regular ? config.Fonts.Regular : ImGui::GetFont();
    *faux_bold = *faux_italic = false;
    if (flags & ImGuiMdRunFlags_Code)
        return config.Fonts.Code ? config.Fonts.Code : regular;
    if (force_font)
        return force_font;
    const bool bold = force_bold || (flags & ImGuiMdRunFlags_Bold);
    const bool italic = (flags & ImGuiMdRunFlags_Italic) != 0;
    if (bold && italic)
    {
        if (config.Fonts.BoldItalic)
            return config.Fonts.BoldItalic;
        if (config.Fonts.Bold)
        {
            *faux_italic = true;
            return config.Fonts.Bold;
        }
        if (config.Fonts.Italic)
        {
            *faux_bold = true;
            return config.Fonts.Italic;
        }
        *faux_bold = *faux_italic = true;
        return regular;
    }
    if (bold)
    {
        if (config.Fonts.Bold)
            return config.Fonts.Bold;
        *faux_bold = true;
        return regular;
    }
    if (italic)
    {
        if (config.Fonts.Italic)
            return config.Fonts.Italic;
        *faux_italic = true;
        return regular;
    }
    return regular;
}

// Shear the glyphs added to the draw list since 'vtx_begin' (fake italic)
static void ImGuiMd_Shear(ImDrawList* draw_list, int vtx_begin, float baseline_y, float amount)
{
    for (int n = vtx_begin; n < draw_list->VtxBuffer.Size; n++)
        draw_list->VtxBuffer[n].pos.x += (baseline_y - draw_list->VtxBuffer[n].pos.y) * amount;
}

static int ImGuiMd_Utf8Len(const char* s, const char* end)
{
    unsigned int c;
    int n = ImTextCharFromUtf8(&c, s, end);
    return n > 0 ? n : 1;
}

// Fonts are baked per size since ImGui 1.92: metrics come from the size the font was added with
static float ImGuiMd_Advance(ImFont* font, ImWchar c)  { return font->GetFontBaked(font->LegacySize)->GetCharAdvance(c); }
static float ImGuiMd_Ascent(ImFont* font)              { return font->GetFontBaked(font->LegacySize)->Ascent; }

// Flow inline runs [run_start, run_start + run_count) in a column. Returns the height used.
static float ImGuiMd_Flow(ImGuiMdFlow& f, int run_start, int run_count)
{
    const ImGuiMdConfig& config = *f.Config;
    ImFont* base_font = config.Fonts.Regular ? config.Fonts.Regular : ImGui::GetFont();
    const float default_h = base_font->LegacySize * f.Scale * config.LineSpacing;
    float x = 0.0f, y = 0.0f, line_h = 0.0f;

    for (int run_n = run_start; run_n < run_start + run_count; run_n++)
    {
        const ImGuiMdRun& run = f.Doc->Runs[run_n];
        const int flags = run.Flags;

        if ((flags & ImGuiMdRunFlags_HardBreak) || ((flags & ImGuiMdRunFlags_SoftBreak) && config.SoftBreakAsNewLine))
        {
            y += (line_h > 0.0f) ? line_h : default_h;
            x = 0.0f;
            line_h = 0.0f;
            continue;
        }
        if (flags & ImGuiMdRunFlags_SoftBreak)
        {
            if (x > 0.0f)
                x += ImGuiMd_Advance(base_font, ' ') * f.Scale;
            continue;
        }

        bool faux_bold, faux_italic;
        ImFont* font = ImGuiMd_PickFont(config, flags, f.ForceBold, f.ForceFont, &faux_bold, &faux_italic);
        const float size = font->LegacySize * f.Scale;
        const float baseline_dy = ImGuiMd_Ascent(f.ForceFont ? f.ForceFont : base_font) * f.Scale - ImGuiMd_Ascent(font) * f.Scale;    // Align baselines of mixed fonts
        const float space_advance = ImGuiMd_Advance(font, ' ') * f.Scale;
        const bool is_link = (flags & ImGuiMdRunFlags_Link) != 0;
        const ImU32 col = is_link ? config.Colors.Link : f.TextColor;
        const char* p = f.Doc->Pool.Data + run.TextOffset;
        const char* end = p + run.TextLength;

        if (flags & ImGuiMdRunFlags_Image)
        {
            ImVec2 img_size(0.0f, 0.0f);
            ImTextureID tex = 0;
            if (config.ImageCallback && run.Link >= 0)
                tex = config.ImageCallback(f.Doc->Pool.Data + f.Doc->Links[run.Link].UrlOffset, &img_size, config.UserData);
            const ImGuiMdLink* html_link = (run.Link >= 0 && f.Doc->Links[run.Link].Width > 0.0f) ? &f.Doc->Links[run.Link] : NULL;
            if (tex != 0 && img_size.x > 0.0f && img_size.y > 0.0f && html_link)
            {
                // HTML image with a width: placed in the line, next ones follow on the same row while they fit
                const float w = ImMin(html_link->Width, f.WrapWidth);
                const float h = img_size.y * w / img_size.x;
                if (x > 0.0f && x + w > f.WrapWidth)
                {
                    y += line_h;
                    x = 0.0f;
                    line_h = 0.0f;
                }
                if (f.Draw)
                {
                    const ImVec2 img_min(f.Origin.x + x, f.Origin.y + y), img_max(img_min.x + w, img_min.y + h);
                    f.DrawList->AddImage(tex, img_min, img_max);
                    if (f.MouseValid && f.Mouse.x >= img_min.x && f.Mouse.x < img_max.x && f.Mouse.y >= img_min.y && f.Mouse.y < img_max.y)
                        f.HoverTip = (html_link->TitleOffset >= 0) ? f.Doc->Pool.Data + html_link->TitleOffset : (run.TextLength > 0 ? p : NULL);
                }
                x += w + ImGuiMd_Advance(base_font, ' ') * f.Scale * 1.5f;
                line_h = ImMax(line_h, h);
                f.UsedWidth = ImMax(f.UsedWidth, x);
                continue;
            }
            if (tex != 0 && img_size.x > 0.0f && img_size.y > 0.0f)
            {
                if (img_size.x > f.WrapWidth)
                {
                    img_size.y *= f.WrapWidth / img_size.x;
                    img_size.x = f.WrapWidth;
                }
                if (x > 0.0f)
                {
                    y += line_h;
                    x = 0.0f;
                    line_h = 0.0f;
                }
                if (f.Draw)
                {
                    const ImVec2 img_min(f.Origin.x, f.Origin.y + y), img_max(f.Origin.x + img_size.x, f.Origin.y + y + img_size.y);
                    f.DrawList->AddImage(tex, img_min, img_max);
                    if (f.MouseValid && f.Mouse.x >= img_min.x && f.Mouse.x < img_max.x && f.Mouse.y >= img_min.y && f.Mouse.y < img_max.y)
                    {
                        const ImGuiMdLink& link = f.Doc->Links[run.Link];
                        f.HoverTip = (link.TitleOffset >= 0) ? f.Doc->Pool.Data + link.TitleOffset : (run.TextLength > 0 ? p : NULL);
                    }
                }
                f.UsedWidth = ImMax(f.UsedWidth, img_size.x);
                y += img_size.y;
                continue;
            }
            // No texture: the alternative text is drawn instead
        }

        while (p < end)
        {
            if (*p == '\n')
            {
                y += (line_h > 0.0f) ? line_h : size * config.LineSpacing;
                x = 0.0f;
                line_h = 0.0f;
                p++;
                continue;
            }

            // A token is a word followed by its spaces. Only ASCII spaces are break opportunities, so no-break spaces glue words together.
            const char* q = p;
            while (q < end && *q != ' ' && *q != '\n')
                q++;
            const char* r = q;
            while (r < end && *r == ' ')
                r++;
            const float word_w = (q > p) ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, p, q).x : 0.0f;
            const float space_w = (float)(r - q) * space_advance;

            bool at_start = (x == 0.0f);
            if (at_start && q == p && !f.KeepSpaces)
            {
                p = r;
                continue;
            }
            if (!at_start && x + word_w > f.WrapWidth)
            {
                y += line_h;
                x = 0.0f;
                line_h = 0.0f;
                at_start = true;
            }

            // Pieces: normally the whole word, several if it is wider than the column
            const char* s = p;
            while (s < q || s == p)
            {
                const char* e = q;
                float piece_w = word_w;
                if (word_w > f.WrapWidth && at_start && q > p)
                {
                    e = font->CalcWordWrapPosition(size, s, q, ImMax(f.WrapWidth - x, size));	// size = LegacySize * Scale (Dear ImGui 1.92 takes the size, not the scale)
                    if (e <= s)
                        e = s + ImGuiMd_Utf8Len(s, q);
                    piece_w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s, e).x;
                }
                const float adv = piece_w + (e == q ? space_w : 0.0f);
                line_h = ImMax(line_h, size * config.LineSpacing);

                if (f.Draw && e > s)
                {
                    const ImVec2 pos(f.Origin.x + x, f.Origin.y + y + baseline_dy + size * (config.LineSpacing - 1.0f) * 0.5f);    // Text centered in its row
                    if (flags & ImGuiMdRunFlags_Code)
                        f.DrawList->AddRectFilled(ImVec2(pos.x - 1.0f, pos.y), ImVec2(pos.x + adv, pos.y + size), config.Colors.CodeBg, 2.0f);
                    const int vtx_begin = f.DrawList->VtxBuffer.Size;
                    f.DrawList->AddText(font, size, pos, col, s, e);
                    if (faux_bold)
                        f.DrawList->AddText(font, size, ImVec2(pos.x + 0.6f, pos.y), col, s, e);
                    if (faux_italic)
                        ImGuiMd_Shear(f.DrawList, vtx_begin, pos.y + size * 0.8f, 0.2f);
                    if (flags & ImGuiMdRunFlags_Strike)
                        f.DrawList->AddLine(ImVec2(pos.x, pos.y + size * 0.55f), ImVec2(pos.x + piece_w, pos.y + size * 0.55f), col);
                    if (is_link)
                    {
                        const bool hover = f.MouseValid && f.Mouse.x >= pos.x && f.Mouse.x < pos.x + piece_w && f.Mouse.y >= pos.y && f.Mouse.y < pos.y + size;
                        if (hover)
                        {
                            f.HoverLink = run.Link;
                            const ImGuiMdLink& link = f.Doc->Links[run.Link];
                            f.HoverTip = f.Doc->Pool.Data + (link.TitleOffset >= 0 ? link.TitleOffset : link.UrlOffset);
                        }
                        // The underline goes on through the space of a link which continues in the same run
                        const float under_w = piece_w + ((e == q && r < end) ? space_w : 0.0f);
                        f.DrawList->AddLine(ImVec2(pos.x, pos.y + size * 0.95f), ImVec2(pos.x + under_w, pos.y + size * 0.95f), col, hover ? 2.0f : 1.0f);
                    }
                }
                f.UsedWidth = ImMax(f.UsedWidth, x + piece_w);
                x += adv;
                s = e;
                if (e < q)
                {
                    y += line_h;
                    x = 0.0f;
                    line_h = 0.0f;
                }
                else
                    break;
            }
            p = r;
        }
    }
    return y + ((line_h > 0.0f) ? line_h : default_h);
}

//-----------------------------------------------------------------------------
// [SECTION] Layout and drawing of blocks
//-----------------------------------------------------------------------------

static const float IMGUI_MD_HEADING_SCALE[6] = { 1.7f, 1.45f, 1.25f, 1.1f, 1.0f, 0.95f };

struct ImGuiMdFrame
{
    ImGuiMdDocument*        Doc;
    const ImGuiMdConfig*    Config;
    ImDrawList*             DrawList;
    ImVec2                  Origin;
    float                   Width;
    float                   Base;               // Base font size
    ImU32                   TextColor;
    ImVec2                  Mouse;
    bool                    MouseValid;
    int                     HoverLink;
    const char*             HoverTip;
};

static void ImGuiMd_InitFlow(ImGuiMdFlow& flow, const ImGuiMdFrame& fr, bool draw, ImVec2 origin, float wrap_width)
{
    memset(&flow, 0, sizeof(flow));
    flow.Doc = fr.Doc;
    flow.Config = fr.Config;
    flow.DrawList = fr.DrawList;
    flow.Origin = origin;
    flow.WrapWidth = wrap_width;
    flow.Scale = 1.0f;
    flow.Draw = draw;
    flow.TextColor = fr.TextColor;
    flow.Mouse = fr.Mouse;
    flow.MouseValid = fr.MouseValid;
    flow.HoverLink = -1;
}

static float ImGuiMd_IndentOf(const ImGuiMdFrame& fr, const ImGuiMdLeaf& leaf)
{
    return leaf.Depth * fr.Config->IndentSpacing;
}

// Lay out (draw = false) or draw (draw = true) the content of a leaf at 'origin'. Returns its height.
static float ImGuiMd_Leaf(ImGuiMdFrame& fr, const ImGuiMdLeaf& leaf, bool draw, ImVec2 origin)
{
    const ImGuiMdConfig& config = *fr.Config;
    const float indent = ImGuiMd_IndentOf(fr, leaf);
    const float wrap = ImMax(fr.Width - indent, 20.0f);
    const ImVec2 pos(origin.x + indent, origin.y);
    ImGuiMdFlow flow;

    switch (leaf.Type)
    {
    case ImGuiMdLeafType_Paragraph:
    {
        ImGuiMd_InitFlow(flow, fr, draw, pos, wrap);
        const float h = ImGuiMd_Flow(flow, leaf.RunStart, leaf.RunCount);
        if (flow.HoverLink >= 0)
            fr.HoverLink = flow.HoverLink;
        if (flow.HoverTip)
            fr.HoverTip = flow.HoverTip;
        return h;
    }
    case ImGuiMdLeafType_Heading:
    {
        ImFont* heading_font = config.Fonts.Heading[ImClamp(leaf.Level, 1, 6) - 1];
        ImGuiMd_InitFlow(flow, fr, draw, pos, wrap);
        flow.ForceFont = heading_font;
        flow.ForceBold = (heading_font == NULL);
        flow.Scale = heading_font ? 1.0f : IMGUI_MD_HEADING_SCALE[ImClamp(leaf.Level, 1, 6) - 1];
        float h = ImGuiMd_Flow(flow, leaf.RunStart, leaf.RunCount);
        if (flow.HoverLink >= 0)
            fr.HoverLink = flow.HoverLink;
        if (flow.HoverTip)
            fr.HoverTip = flow.HoverTip;
        if (leaf.Level <= 2)
        {
            if (draw)
                fr.DrawList->AddLine(ImVec2(pos.x, pos.y + h + 2.0f), ImVec2(pos.x + wrap, pos.y + h + 2.0f), config.Colors.Rule);
            h += 5.0f;
        }
        return h;
    }
    case ImGuiMdLeafType_Code:
    {
        if (config.CodeBlockCallback && leaf.LangLength > 0)
        {
            const float h = config.CodeBlockCallback(fr.Doc->Pool.Data + leaf.LangOffset, fr.Doc->Pool.Data + leaf.CodeOffset, leaf.CodeLength, wrap, draw, pos, config.UserData);
            if (h >= 0.0f)
                return h;
        }
        const float pad = 6.0f;
        ImGuiMd_InitFlow(flow, fr, false, ImVec2(pos.x + pad, pos.y + pad), wrap - 2.0f * pad);
        flow.KeepSpaces = true;
        const float total = ImGuiMd_Flow(flow, leaf.RunStart, leaf.RunCount) + 2.0f * pad;
        if (draw)
        {
            fr.DrawList->AddRectFilled(pos, ImVec2(pos.x + wrap, pos.y + total), config.Colors.CodeBg, 4.0f);
            ImGuiMd_InitFlow(flow, fr, true, ImVec2(pos.x + pad, pos.y + pad), wrap - 2.0f * pad);
            flow.KeepSpaces = true;
            ImGuiMd_Flow(flow, leaf.RunStart, leaf.RunCount);
        }
        return total;
    }
    case ImGuiMdLeafType_HRule:
    {
        const float h = fr.Base * 0.9f;
        if (draw)
            fr.DrawList->AddLine(ImVec2(pos.x, pos.y + h * 0.5f), ImVec2(pos.x + wrap, pos.y + h * 0.5f), config.Colors.Rule, 1.5f);
        return h;
    }
    case ImGuiMdLeafType_Table:
    {
        const float pad = 4.0f;
        const int cols = ImMax(leaf.ColCount, 1);
        const float col_w = wrap / (float)cols;
        int rows = 0;
        for (int n = 0; n < leaf.CellCount; n++)
            rows = ImMax(rows, fr.Doc->Cells[leaf.CellStart + n].Row + 1);
        ImVector<float> row_h, row_y;
        row_h.resize(rows);
        row_y.resize(rows + 1);
        for (int n = 0; n < rows; n++)
            row_h[n] = fr.Base + 2.0f * pad;
        for (int n = 0; n < leaf.CellCount; n++)
        {
            const ImGuiMdCell& cell = fr.Doc->Cells[leaf.CellStart + n];
            ImGuiMd_InitFlow(flow, fr, false, ImVec2(0, 0), col_w - 2.0f * pad);
            flow.ForceBold = cell.Header;
            row_h[cell.Row] = ImMax(row_h[cell.Row], ImGuiMd_Flow(flow, cell.RunStart, cell.RunCount) + 2.0f * pad);
        }
        row_y[0] = 0.0f;
        for (int n = 0; n < rows; n++)
            row_y[n + 1] = row_y[n] + row_h[n];
        if (draw)
        {
            for (int n = 0; n < leaf.CellCount; n++)
            {
                const ImGuiMdCell& cell = fr.Doc->Cells[leaf.CellStart + n];
                const ImVec2 cell_pos(pos.x + cell.Col * col_w, pos.y + row_y[cell.Row]);
                if (cell.Header)
                    fr.DrawList->AddRectFilled(cell_pos, ImVec2(cell_pos.x + col_w, cell_pos.y + row_h[cell.Row]), config.Colors.CodeBg);
                // Alignment of the column: measure the widest line, then shift the cell content
                ImGuiMd_InitFlow(flow, fr, false, ImVec2(0, 0), col_w - 2.0f * pad);
                flow.ForceBold = cell.Header;
                ImGuiMd_Flow(flow, cell.RunStart, cell.RunCount);
                float dx = (col_w - 2.0f * pad) - flow.UsedWidth;
                dx = (cell.Align == MD_ALIGN_CENTER) ? dx * 0.5f : (cell.Align == MD_ALIGN_RIGHT) ? dx : 0.0f;
                ImGuiMd_InitFlow(flow, fr, true, ImVec2(cell_pos.x + pad + ImMax(dx, 0.0f), cell_pos.y + pad), col_w - 2.0f * pad);
                flow.ForceBold = cell.Header;
                ImGuiMd_Flow(flow, cell.RunStart, cell.RunCount);
                if (flow.HoverLink >= 0)
                    fr.HoverLink = flow.HoverLink;
                if (flow.HoverTip)
                    fr.HoverTip = flow.HoverTip;
            }
            for (int n = 0; n <= rows; n++)
                fr.DrawList->AddLine(ImVec2(pos.x, pos.y + row_y[n]), ImVec2(pos.x + wrap, pos.y + row_y[n]), config.Colors.Rule);
            for (int n = 0; n <= cols; n++)
                fr.DrawList->AddLine(ImVec2(pos.x + n * col_w, pos.y), ImVec2(pos.x + n * col_w, pos.y + row_y[rows]), config.Colors.Rule);
        }
        return row_y[rows];
    }
    }
    return 0.0f;
}

static float ImGuiMd_SpaceBefore(const ImGuiMdFrame& fr, const ImGuiMdDocument* doc, int n)
{
    const ImGuiMdLeaf& leaf = doc->Leaves[n];
    if (n == 0 || leaf.Type != ImGuiMdLeafType_Heading)
        return 0.0f;
    return fr.Base * (leaf.Level <= 2 ? 0.8f : 0.5f);
}

static float ImGuiMd_SpaceAfter(const ImGuiMdFrame& fr, const ImGuiMdDocument* doc, int n)
{
    const ImGuiMdLeaf& leaf = doc->Leaves[n];
    if (leaf.Type == ImGuiMdLeafType_Heading)
        return fr.Base * 0.3f;
    if (leaf.Tight && n + 1 < doc->Leaves.Size && doc->Leaves[n + 1].Tight && doc->Leaves[n + 1].Depth >= leaf.Depth)
        return fr.Base * 0.15f;
    return fr.Base * fr.Config->ParagraphSpacing;
}

// Lay out all the leaves for the current width. Heights are cached until the width, the fonts or the settings change.
static void ImGuiMd_UpdateLayout(ImGuiMdFrame& fr)
{
    ImGuiMdDocument* doc = fr.Doc;
    ImGuiMdCacheKey key;
    memset(&key, 0, sizeof(key));
    key.Width = fr.Width;
    key.FontSize = fr.Base;
    key.IndentSpacing = fr.Config->IndentSpacing;
    key.ParagraphSpacing = fr.Config->ParagraphSpacing;
    key.LineSpacing = fr.Config->LineSpacing;
    key.SoftBreakAsNewLine = fr.Config->SoftBreakAsNewLine ? 1 : 0;
    key.Fonts[0] = fr.Config->Fonts.Regular;
    key.Fonts[1] = fr.Config->Fonts.Bold;
    key.Fonts[2] = fr.Config->Fonts.Italic;
    key.Fonts[3] = fr.Config->Fonts.BoldItalic;
    key.Fonts[4] = fr.Config->Fonts.Code;
    for (int n = 0; n < 6; n++)
        key.Fonts[5 + n] = fr.Config->Fonts.Heading[n];
    if (doc->CacheValid && memcmp(&key, &doc->CacheKey, sizeof(key)) == 0)
        return;

    float y = 0.0f;
    for (int n = 0; n < doc->Leaves.Size; n++)
    {
        ImGuiMdLeaf& leaf = doc->Leaves[n];
        y += ImGuiMd_SpaceBefore(fr, doc, n);
        leaf.Y = y;
        leaf.Height = ImGuiMd_Leaf(fr, leaf, false, ImVec2(0.0f, 0.0f));
        y += leaf.Height + ImGuiMd_SpaceAfter(fr, doc, n);
    }
    doc->TotalHeight = y;
    doc->CacheKey = key;
    doc->CacheValid = true;
}

//-----------------------------------------------------------------------------
// [SECTION] Public API
//-----------------------------------------------------------------------------

ImGuiMdDocument* ImGuiMd::CreateDocument()
{
    return IM_NEW(ImGuiMdDocument)();
}

void ImGuiMd::DestroyDocument(ImGuiMdDocument* doc)
{
    IM_DELETE(doc);
}

void ImGuiMd::SetText(ImGuiMdDocument* doc, const char* text, const char* text_end)
{
    if (text_end == NULL)
        text_end = text + strlen(text);
    if (text_end - text >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text += 3;                                  // Skip the UTF-8 byte order mark
    const int length = (int)(text_end - text);

    doc->Source.resize(0);
    doc->Pool.resize(0);
    doc->Runs.resize(0);
    doc->Links.resize(0);
    doc->Cells.resize(0);
    doc->Leaves.resize(0);
    doc->LineStarts.resize(0);
    doc->CacheValid = false;
    doc->TotalHeight = 0.0f;
    if (length <= 0)
        return;

    doc->Source.resize(length);
    memcpy(doc->Source.Data, text, length);
    doc->LineStarts.push_back(0);
    for (int n = 0; n < length; n++)
        if (text[n] == '\n')
            doc->LineStarts.push_back(n + 1);

    ImGuiMdParser parser;
    parser.Doc = doc;
    MD_PARSER md;
    memset(&md, 0, sizeof(md));
    md.abi_version = 0;
    md.flags = MD_DIALECT_GITHUB;
    md.enter_block = ImGuiMd_EnterBlock;
    md.leave_block = ImGuiMd_LeaveBlock;
    md.enter_span = ImGuiMd_EnterSpan;
    md.leave_span = ImGuiMd_LeaveSpan;
    md.text = ImGuiMd_Text;
    md_parse(doc->Source.Data, (MD_SIZE)length, &md, &parser);
    parser.FlushImplicit();
}

static const ImWchar IMGUI_MD_SYMBOL_RANGES[] = { 0x2190, 0x21FF, 0x2500, 0x257F, 0x2580, 0x259F, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0x2700, 0x27BF, 0 };
static const ImWchar IMGUI_MD_RANGES[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x2000, 0x206F, 0x20AC, 0x20AC, 0x2190, 0x21FF, 0x2500, 0x257F, 0x2580, 0x259F, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0x2700, 0x27BF, 0 };

const ImWchar* ImGuiMd::GetGlyphRanges()          { return IMGUI_MD_RANGES; }
const ImWchar* ImGuiMd::GetSymbolGlyphRanges()    { return IMGUI_MD_SYMBOL_RANGES; }

void ImGuiMd::Render(ImGuiMdDocument* doc, const ImGuiMdConfig& config, ImGuiMdState* state)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;
    if (state)
        state->ClickedLine = 0;

    ImGuiMdFrame fr;
    fr.Doc = doc;
    fr.Config = &config;
    fr.DrawList = window->DrawList;
    fr.Origin = ImGui::GetCursorScreenPos();
    fr.Width = ImMax(ImGui::GetContentRegionAvail().x, 50.0f);
    fr.Base = ImGui::GetFontSize();
    fr.TextColor = config.Colors.Text ? config.Colors.Text : ImGui::GetColorU32(ImGuiCol_Text);
    fr.Mouse = ImGui::GetIO().MousePos;
    fr.MouseValid = ImGui::IsWindowHovered();
    fr.HoverLink = -1;
    fr.HoverTip = NULL;
    ImGuiMd_UpdateLayout(fr);

    const ImVec2 clip_min = fr.DrawList->GetClipRectMin();
    const ImVec2 clip_max = fr.DrawList->GetClipRectMax();

    // Scroll request
    if (state && state->ScrollToLine > 0)
    {
        int target = -1;
        for (int n = 0; n < doc->Leaves.Size; n++)
            if (doc->Leaves[n].StartLine > 0 && doc->Leaves[n].StartLine <= state->ScrollToLine)
                target = n;
        if (target >= 0)
        {
            const ImGuiMdLeaf& leaf = doc->Leaves[target];
            const float top = fr.Origin.y + leaf.Y;
            if (top < clip_min.y || top + leaf.Height > clip_max.y)
                ImGui::SetScrollFromPosY(top - window->Pos.y + window->Scroll.y, 0.3f);
        }
        state->ScrollToLine = 0;
    }

    // First visible leaf (leaves are sorted by Y)
    int first = 0;
    {
        int lo = 0, hi = doc->Leaves.Size;
        while (lo < hi)
        {
            const int mid = (lo + hi) / 2;
            if (fr.Origin.y + doc->Leaves[mid].Y + doc->Leaves[mid].Height < clip_min.y)
                lo = mid + 1;
            else
                hi = mid;
        }
        first = lo;
    }

    const bool clicked = fr.MouseValid && ImGui::IsMouseClicked(0);
    bool link_clicked = false;
    for (int n = first; n < doc->Leaves.Size; n++)
    {
        const ImGuiMdLeaf& leaf = doc->Leaves[n];
        const float top = fr.Origin.y + leaf.Y;
        if (top > clip_max.y)
            break;
        const float indent = ImGuiMd_IndentOf(fr, leaf);
        const float bottom = top + leaf.Height;

        if (state && state->HighlightLine > 0 && leaf.StartLine > 0 && state->HighlightLine >= leaf.StartLine && state->HighlightLine <= leaf.EndLine)
            fr.DrawList->AddRectFilled(ImVec2(fr.Origin.x + indent - 4.0f, top - 2.0f), ImVec2(fr.Origin.x + fr.Width, bottom + 2.0f), config.Colors.HighlightBg, 3.0f);

        for (int q = 0; q < leaf.QuoteDepth; q++)
        {
            const float x = fr.Origin.x + q * config.IndentSpacing + config.IndentSpacing * 0.35f;
            // The bar goes on across the spacing when the next block is in the same quote
            float bar_bottom = bottom + 2.0f;
            if (n + 1 < doc->Leaves.Size && doc->Leaves[n + 1].QuoteDepth > q)
                bar_bottom = fr.Origin.y + doc->Leaves[n + 1].Y - 2.0f;
            fr.DrawList->AddRectFilled(ImVec2(x, top - 2.0f), ImVec2(x + 3.0f, bar_bottom), config.Colors.QuoteBar);
        }

        if (leaf.MarkerKind == ImGuiMdMarkerKind_Bullet)
        {
            fr.DrawList->AddCircleFilled(ImVec2(fr.Origin.x + indent - fr.Base * 0.6f, top + fr.Base * 0.55f), fr.Base * 0.14f, fr.TextColor, 8);
        }
        else if (leaf.MarkerKind == ImGuiMdMarkerKind_Text)
        {
            ImFont* font = config.Fonts.Regular ? config.Fonts.Regular : ImGui::GetFont();
            const char* marker = doc->Pool.Data + leaf.MarkerOffset;
            const ImVec2 sz = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, marker);
            fr.DrawList->AddText(font, font->LegacySize, ImVec2(fr.Origin.x + indent - sz.x - fr.Base * 0.35f, top), fr.TextColor, marker);
        }

        fr.HoverLink = -1;
        ImGuiMd_Leaf(fr, leaf, true, ImVec2(fr.Origin.x, top));
        if (fr.HoverLink >= 0)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (clicked && !link_clicked)
            {
                link_clicked = true;
                if (config.LinkCallback)
                    config.LinkCallback(doc->Pool.Data + doc->Links[fr.HoverLink].UrlOffset, config.UserData);
            }
        }
        else if (clicked && state && !link_clicked && fr.Mouse.x >= fr.Origin.x && fr.Mouse.x < fr.Origin.x + fr.Width && fr.Mouse.y >= top - fr.Base * 0.3f && fr.Mouse.y < bottom + fr.Base * 0.3f)
        {
            state->ClickedLine = leaf.StartLine;
        }
    }
    if (link_clicked && state)
        state->ClickedLine = 0;
    if (config.ShowTooltips && fr.HoverTip && fr.HoverTip[0] && fr.MouseValid)
        ImGui::SetTooltip("%s", fr.HoverTip);

    ImGui::Dummy(ImVec2(fr.Width, doc->TotalHeight));
}
