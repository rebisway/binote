// imgui_md - www.rebisway.com - contact@rebisway.com
// Copyright (c) 2026 Thomas Gourgnier
// SPDX-License-Identifier: MIT
//
// Licensed under the MIT license: see the LICENSE file in the folder of this component.

// imgui_md_diagram : small diagram renderer for Dear ImGui (MIT license)

// Index of this file:
// [SECTION] Structures
// [SECTION] Parsing: flowcharts
// [SECTION] Parsing: sequence diagrams
// [SECTION] Layout: flowcharts (layers, ordering, placement)
// [SECTION] Layout: sequence diagrams
// [SECTION] Drawing
// [SECTION] Public API

#include "imgui_md_diagram.h"
#include "imgui_internal.h"
#include <string.h>
#include <ctype.h>

//-----------------------------------------------------------------------------
// [SECTION] Structures
//-----------------------------------------------------------------------------

enum ImGuiMdDiagramKind_ { ImGuiMdDiagramKind_Flow, ImGuiMdDiagramKind_Sequence };
enum ImGuiMdShape_ { ImGuiMdShape_Rect, ImGuiMdShape_Round, ImGuiMdShape_Circle, ImGuiMdShape_Diamond };
enum ImGuiMdDir_ { ImGuiMdDir_TD, ImGuiMdDir_BT, ImGuiMdDir_LR, ImGuiMdDir_RL };

struct ImGuiMdNode
{
    int     IdOffset;               // In ImGuiMdDiagram::Pool
    int     LabelOffset;            // -1 = the id is the label
    int     Shape;
    ImVec2  Size;
    ImVec2  Pos;                    // Center
};

struct ImGuiMdEdge
{
    int     From, To;
    int     Style;                  // 0 solid, 1 dotted, 2 thick
    bool    Arrow;                  // At 'To'
    bool    ArrowStart;             // At 'From'
    int     LabelOffset;            // -1 = none
    int     PathStart, PathCount;   // Control points in ImGuiMdDiagram::Path, in drawing order From -> To
};

struct ImGuiMdPart                  // Sequence diagram participant
{
    int     NameOffset;
    ImVec2  Size;
    float   X;                      // Center
};

struct ImGuiMdMsg                   // Sequence diagram message or note
{
    int     From, To;
    bool    Note;
    bool    Dashed, Head;
    int     TextOffset;
    float   Y;
    ImVec2  TextSize;
};

struct ImGuiMdDiagram
{
    int                     Kind;
    int                     Dir;
    ImVector<char>          Pool;
    ImVector<ImGuiMdNode>   Nodes;
    ImVector<ImGuiMdEdge>   Edges;
    ImVector<ImVec2>        Path;
    ImVector<ImGuiMdPart>   Parts;
    ImVector<ImGuiMdMsg>    Msgs;
    ImVec2                  Size;
    ImFont*                 LayoutFont;
    float                   LayoutFontSize;
    float                   SeqHeaderBottom;        // Sequence: y of the lifelines start

    ImGuiMdDiagram()        { Kind = 0; Dir = ImGuiMdDir_TD; Size = ImVec2(0, 0); LayoutFont = NULL; LayoutFontSize = 0.0f; SeqHeaderBottom = 0.0f; }
    int                     PoolAdd(const char* text, int length);
    const char*             Str(int offset) const { return Pool.Data + offset; }
};

int ImGuiMdDiagram::PoolAdd(const char* text, int length)
{
    const int offset = Pool.Size;
    Pool.resize(Pool.Size + length + 1);
    if (length > 0)
        memcpy(Pool.Data + offset, text, length);
    Pool[offset + length] = 0;
    return offset;
}

//-----------------------------------------------------------------------------
// [SECTION] Parsing: helpers
//-----------------------------------------------------------------------------

struct ImGuiMdCursor
{
    const char* p;
    const char* end;
    bool        Eof() const                 { return p >= end; }
    void        SkipSpaces()                { while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++; }
    bool        Starts(const char* s) const { const int n = (int)strlen(s); return end - p >= n && strncmp(p, s, n) == 0; }
};

static bool ImGuiMd_IsIdChar(char c)
{
    return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
}

// Add a label to the pool: quotes removed, <br/> turned into a line break
static int ImGuiMd_AddLabel(ImGuiMdDiagram* d, const char* a, const char* b)
{
    while (a < b && (*a == ' ' || *a == '\t'))
        a++;
    while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r'))
        b--;
    if (b - a >= 2 && *a == '"' && b[-1] == '"')
    {
        a++;
        b--;
    }
    ImVector<char> tmp;
    for (const char* s = a; s < b;)
    {
        if (b - s >= 4 && (strncmp(s, "<br>", 4) == 0))
        {
            tmp.push_back('\n');
            s += 4;
        }
        else if (b - s >= 5 && (strncmp(s, "<br/>", 5) == 0 || strncmp(s, "<br >", 5) == 0))
        {
            tmp.push_back('\n');
            s += 5;
        }
        else if (b - s >= 6 && strncmp(s, "<br />", 6) == 0)
        {
            tmp.push_back('\n');
            s += 6;
        }
        else
            tmp.push_back(*s++);
    }
    return d->PoolAdd(tmp.Data, tmp.Size);
}

static int ImGuiMd_FindOrAddNode(ImGuiMdDiagram* d, const char* id, int length)
{
    for (int n = 0; n < d->Nodes.Size; n++)
        if ((int)strlen(d->Str(d->Nodes[n].IdOffset)) == length && strncmp(d->Str(d->Nodes[n].IdOffset), id, length) == 0)
            return n;
    ImGuiMdNode node;
    memset(&node, 0, sizeof(node));
    node.IdOffset = d->PoolAdd(id, length);
    node.LabelOffset = -1;
    d->Nodes.push_back(node);
    return d->Nodes.Size - 1;
}

//-----------------------------------------------------------------------------
// [SECTION] Parsing: flowcharts
//-----------------------------------------------------------------------------

// Text between the opening already consumed and the closing sequence. Returns false if the closing is missing.
static bool ImGuiMd_ReadUntil(ImGuiMdCursor& c, const char* closing, const char** out_a, const char** out_b)
{
    const int n = (int)strlen(closing);
    const char* a = c.p;
    for (const char* s = a; s + n <= c.end; s++)
        if (strncmp(s, closing, n) == 0)
        {
            *out_a = a;
            *out_b = s;
            c.p = s + n;
            return true;
        }
    return false;
}

static bool ImGuiMd_ParseNode(ImGuiMdDiagram* d, ImGuiMdCursor& c, int* out_node)
{
    c.SkipSpaces();
    const char* id = c.p;
    while (!c.Eof() && ImGuiMd_IsIdChar(*c.p))
        c.p++;
    if (c.p == id)
        return false;
    const int node = ImGuiMd_FindOrAddNode(d, id, (int)(c.p - id));
    const char* a;
    const char* b;
    int shape = -1;
    if (c.Starts("(("))         { c.p += 2; shape = ImGuiMdShape_Circle;  if (!ImGuiMd_ReadUntil(c, "))", &a, &b)) return false; }
    else if (c.Starts("(["))    { c.p += 2; shape = ImGuiMdShape_Round;   if (!ImGuiMd_ReadUntil(c, "])", &a, &b)) return false; }
    else if (c.Starts("[["))    { c.p += 2; shape = ImGuiMdShape_Rect;    if (!ImGuiMd_ReadUntil(c, "]]", &a, &b)) return false; }
    else if (c.Starts("{{"))    { c.p += 2; shape = ImGuiMdShape_Diamond; if (!ImGuiMd_ReadUntil(c, "}}", &a, &b)) return false; }
    else if (c.Starts("["))     { c.p += 1; shape = ImGuiMdShape_Rect;    if (!ImGuiMd_ReadUntil(c, "]", &a, &b))  return false; }
    else if (c.Starts("("))     { c.p += 1; shape = ImGuiMdShape_Round;   if (!ImGuiMd_ReadUntil(c, ")", &a, &b))  return false; }
    else if (c.Starts("{"))     { c.p += 1; shape = ImGuiMdShape_Diamond; if (!ImGuiMd_ReadUntil(c, "}", &a, &b))  return false; }
    else if (c.Starts(">"))     { c.p += 1; shape = ImGuiMdShape_Rect;    if (!ImGuiMd_ReadUntil(c, "]", &a, &b))  return false; }
    if (shape >= 0)
    {
        d->Nodes[node].Shape = shape;
        d->Nodes[node].LabelOffset = ImGuiMd_AddLabel(d, a, b);
    }
    if (c.Starts(":::"))
    {
        c.p += 3;
        while (!c.Eof() && ImGuiMd_IsIdChar(*c.p))
            c.p++;
    }
    *out_node = node;
    return true;
}

// -->  ---  -.->  ==>  --text-->  -- text -->  with an optional |label|
static bool ImGuiMd_ParseEdge(ImGuiMdDiagram* d, ImGuiMdCursor& c, ImGuiMdEdge* e)
{
    c.SkipSpaces();
    memset(e, 0, sizeof(*e));
    e->LabelOffset = -1;
    if (c.Eof())
        return false;
    if (*c.p == '<')
    {
        e->ArrowStart = true;
        c.p++;
    }
    if (c.Eof() || (*c.p != '-' && *c.p != '=' && *c.p != '.'))
        return false;

    const char* op = c.p;
    while (!c.Eof() && (*c.p == '-' || *c.p == '=' || *c.p == '.'))
        c.p++;
    int op_len = (int)(c.p - op);
    bool dotted = memchr(op, '.', op_len) != NULL;
    bool thick = memchr(op, '=', op_len) != NULL;

    // "-- text -->" : the first operator is only an opening
    if (op_len == 2 && !c.Eof() && *c.p == ' ')
    {
        const char* text_a = c.p;
        const char* q = c.p;
        while (q + 1 < c.end && !((q[0] == '-' && q[1] == '-') || (q[0] == '=' && q[1] == '=') || (q[0] == '-' && q[1] == '.') || (q[0] == '.' && q[1] == '-')))
            q++;
        if (q + 1 >= c.end)
            return false;
        e->LabelOffset = ImGuiMd_AddLabel(d, text_a, q);
        c.p = q;
        op = c.p;
        while (!c.Eof() && (*c.p == '-' || *c.p == '=' || *c.p == '.'))
            c.p++;
        op_len = (int)(c.p - op);
        dotted = dotted || memchr(op, '.', op_len) != NULL;
        thick = thick || memchr(op, '=', op_len) != NULL;
    }
    if (op_len < 2 && !(op_len == 1 && *op == '.'))
        return false;
    if (!c.Eof() && *c.p == '>')
    {
        e->Arrow = true;
        c.p++;
    }
    e->Style = thick ? 2 : (dotted ? 1 : 0);

    if (!c.Eof() && *c.p == '|')
    {
        c.p++;
        const char* a;
        const char* b;
        if (!ImGuiMd_ReadUntil(c, "|", &a, &b))
            return false;
        e->LabelOffset = ImGuiMd_AddLabel(d, a, b);
    }
    return true;
}

static bool ImGuiMd_ParseFlowLine(ImGuiMdDiagram* d, const char* a, const char* b)
{
    ImGuiMdCursor c = { a, b };
    c.SkipSpaces();
    if (c.Eof() || c.Starts("%%"))
        return true;
    static const char* ignored[] = { "style ", "classDef ", "class ", "linkStyle ", "click ", "subgraph ", "direction ", "accTitle", "accDescr" };
    for (int n = 0; n < IM_ARRAYSIZE(ignored); n++)
        if (c.Starts(ignored[n]))
            return true;
    if (c.Starts("end") && (c.end - c.p == 3 || !ImGuiMd_IsIdChar(c.p[3])))
        return true;                                    // End of a subgraph (subgraphs are not drawn, their nodes are)

    int from;
    if (!ImGuiMd_ParseNode(d, c, &from))
        return true;                                    // Unknown statement: ignored
    for (;;)
    {
        c.SkipSpaces();
        if (c.Eof() || c.Starts("%%"))
            break;
        if (*c.p == '&')                                // "A & B": the nodes are only declared
        {
            c.p++;
            int other;
            if (!ImGuiMd_ParseNode(d, c, &other))
                break;
            continue;
        }
        ImGuiMdEdge e;
        if (!ImGuiMd_ParseEdge(d, c, &e))
            break;
        int to;
        if (!ImGuiMd_ParseNode(d, c, &to))
            break;
        e.From = from;
        e.To = to;
        d->Edges.push_back(e);
        from = to;
    }
    return true;
}

//-----------------------------------------------------------------------------
// [SECTION] Parsing: sequence diagrams
//-----------------------------------------------------------------------------

static int ImGuiMd_FindOrAddPart(ImGuiMdDiagram* d, const char* id, int length)
{
    // The id is kept in Nodes (id + optional display name), the participant list indexes them
    const int node = ImGuiMd_FindOrAddNode(d, id, length);
    for (int n = 0; n < d->Parts.Size; n++)
        if (d->Parts[n].NameOffset == node)             // NameOffset holds the node index until the layout
            return n;
    ImGuiMdPart part;
    memset(&part, 0, sizeof(part));
    part.NameOffset = node;
    d->Parts.push_back(part);
    return d->Parts.Size - 1;
}

static bool ImGuiMd_ParseSequenceLine(ImGuiMdDiagram* d, const char* a, const char* b)
{
    ImGuiMdCursor c = { a, b };
    c.SkipSpaces();
    if (c.Eof() || c.Starts("%%"))
        return true;

    if (c.Starts("participant ") || c.Starts("actor "))
    {
        c.p += c.Starts("participant ") ? 12 : 6;
        c.SkipSpaces();
        const char* id = c.p;
        while (!c.Eof() && ImGuiMd_IsIdChar(*c.p))
            c.p++;
        if (c.p == id)
            return true;
        const int part = ImGuiMd_FindOrAddPart(d, id, (int)(c.p - id));
        c.SkipSpaces();
        if (c.Starts("as ") || c.Starts("as\t"))
        {
            c.p += 3;
            d->Nodes[d->Parts[part].NameOffset].LabelOffset = ImGuiMd_AddLabel(d, c.p, c.end);
        }
        return true;
    }

    if (c.Starts("Note ") || c.Starts("note "))
    {
        c.p += 5;
        c.SkipSpaces();
        int mode = 0;                                   // 0 over, 1 left of, 2 right of
        if (c.Starts("over "))          { c.p += 5; mode = 0; }
        else if (c.Starts("left of "))  { c.p += 8; mode = 1; }
        else if (c.Starts("right of ")) { c.p += 9; mode = 2; }
        else
            return true;
        const char* colon = (const char*)memchr(c.p, ':', c.end - c.p);
        if (!colon)
            return true;
        ImGuiMdCursor names = { c.p, colon };
        int first = -1, last = -1;
        while (!names.Eof())
        {
            names.SkipSpaces();
            const char* id = names.p;
            while (!names.Eof() && ImGuiMd_IsIdChar(*names.p))
                names.p++;
            if (names.p > id)
            {
                const int part = ImGuiMd_FindOrAddPart(d, id, (int)(names.p - id));
                if (first < 0)
                    first = part;
                last = part;
            }
            else if (!names.Eof())
                names.p++;
        }
        if (first < 0)
            return true;
        ImGuiMdMsg m;
        memset(&m, 0, sizeof(m));
        m.Note = true;
        m.From = first;
        m.To = last;
        m.Dashed = (mode == 1);                         // Dashed: left of
        m.Head = (mode == 2);                           // Head: right of
        m.TextOffset = ImGuiMd_AddLabel(d, colon + 1, c.end);
        d->Msgs.push_back(m);
        return true;
    }

    // Message:  A->>B: text
    const char* id1 = c.p;
    while (!c.Eof() && ImGuiMd_IsIdChar(*c.p))
        c.p++;
    if (c.p == id1)
        return true;                                    // Keyword we do not draw (loop, alt, end, autonumber...)
    const char* id1_end = c.p;
    c.SkipSpaces();
    static const char* arrows[] = { "-->>", "->>", "--x", "-->", "--)", "->", "-x", "-)" };
    int arrow = -1;
    for (int n = 0; n < IM_ARRAYSIZE(arrows); n++)
        if (c.Starts(arrows[n]))
        {
            arrow = n;
            break;
        }
    if (arrow < 0)
        return true;
    c.p += strlen(arrows[arrow]);
    if (!c.Eof() && (*c.p == '+' || *c.p == '-'))
        c.p++;
    c.SkipSpaces();
    const char* id2 = c.p;
    while (!c.Eof() && ImGuiMd_IsIdChar(*c.p))
        c.p++;
    if (c.p == id2)
        return false;
    const char* id2_end = c.p;
    c.SkipSpaces();
    ImGuiMdMsg m;
    memset(&m, 0, sizeof(m));
    m.From = ImGuiMd_FindOrAddPart(d, id1, (int)(id1_end - id1));
    m.To = ImGuiMd_FindOrAddPart(d, id2, (int)(id2_end - id2));
    m.Dashed = (arrow == 0 || arrow == 2 || arrow == 3 || arrow == 4);
    m.Head = (arrow != 3 && arrow != 5);
    m.TextOffset = -1;
    if (!c.Eof() && *c.p == ':')
        m.TextOffset = ImGuiMd_AddLabel(d, c.p + 1, c.end);
    d->Msgs.push_back(m);
    return true;
}

ImGuiMdDiagram* ImGuiMdDiagrams::Parse(const char* text, int length)
{
    ImGuiMdDiagram* d = IM_NEW(ImGuiMdDiagram)();
    bool header_done = false;
    bool ok = true;
    const char* end = text + length;
    for (const char* line = text; line <= end && ok;)
    {
        const char* eol = line;
        while (eol < end && *eol != '\n')
            eol++;
        if (!header_done)
        {
            ImGuiMdCursor c = { line, eol };
            c.SkipSpaces();
            if (!c.Eof() && !c.Starts("%%"))
            {
                header_done = true;
                if (c.Starts("graph") || c.Starts("flowchart"))
                {
                    d->Kind = ImGuiMdDiagramKind_Flow;
                    c.p += c.Starts("graph") ? 5 : 9;
                    c.SkipSpaces();
                    if (c.Starts("LR"))         d->Dir = ImGuiMdDir_LR;
                    else if (c.Starts("RL"))    d->Dir = ImGuiMdDir_RL;
                    else if (c.Starts("BT"))    d->Dir = ImGuiMdDir_BT;
                    else                        d->Dir = ImGuiMdDir_TD;
                }
                else if (c.Starts("sequenceDiagram"))
                    d->Kind = ImGuiMdDiagramKind_Sequence;
                else
                    ok = false;
            }
        }
        else if (d->Kind == ImGuiMdDiagramKind_Flow)
        {
            // ';' also separates statements
            const char* s = line;
            while (s <= eol)
            {
                const char* semi = s;
                while (semi < eol && *semi != ';')
                    semi++;
                ok = ImGuiMd_ParseFlowLine(d, s, semi);
                s = semi + 1;
            }
        }
        else
            ok = ImGuiMd_ParseSequenceLine(d, line, eol);
        line = eol + 1;
    }
    if (!ok || !header_done || (d->Kind == ImGuiMdDiagramKind_Flow ? d->Nodes.Size == 0 : d->Parts.Size == 0))
    {
        IM_DELETE(d);
        return NULL;
    }
    return d;
}

void ImGuiMdDiagrams::DestroyDiagram(ImGuiMdDiagram* diagram)
{
    IM_DELETE(diagram);
}

//-----------------------------------------------------------------------------
// [SECTION] Layout: flowcharts (layers, ordering, placement)
//-----------------------------------------------------------------------------

static const float IMGUI_MD_PAD_X = 12.0f;
static const float IMGUI_MD_PAD_Y = 7.0f;
static const float IMGUI_MD_NODE_GAP = 28.0f;
static const float IMGUI_MD_LAYER_GAP = 44.0f;
static const float IMGUI_MD_MARGIN = 10.0f;

// Size of a text with line breaks
static ImVec2 ImGuiMd_TextSize(ImFont* font, float font_size, const char* text)
{
    float w = 0.0f;
    int lines = 1;
    const char* line = text;
    for (const char* s = text;; s++)
    {
        if (*s == '\n' || *s == 0)
        {
            w = ImMax(w, font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, line, s).x);
            if (*s == 0)
                break;
            line = s + 1;
            lines++;
        }
    }
    return ImVec2(w, lines * font_size);
}

// Layout node of the layered drawing: a real node or a dummy one where a long edge crosses a layer
struct ImGuiMdLNode
{
    int     Real;                   // Index in Nodes, -1 for a dummy
    int     Layer;
    float   SizeA, SizeB;           // A: along the layer, B: across the layers
    float   A, B;                   // Center
    int     Index;                  // Position in its layer
};

struct ImGuiMdSeg { int A, B; };    // Between a node of a layer and a node of the next layer

static void ImGuiMd_LayoutFlow(ImGuiMdDiagram* d, ImFont* font, float fs)
{
    const int n = d->Nodes.Size;
    const bool horizontal = (d->Dir == ImGuiMdDir_LR || d->Dir == ImGuiMdDir_RL);

    for (int i = 0; i < n; i++)
    {
        ImGuiMdNode& node = d->Nodes[i];
        const char* label = d->Str(node.LabelOffset >= 0 ? node.LabelOffset : node.IdOffset);
        ImVec2 ts = ImGuiMd_TextSize(font, fs, label);
        ImVec2 sz(ts.x + 2.0f * IMGUI_MD_PAD_X, ts.y + 2.0f * IMGUI_MD_PAD_Y);
        if (node.Shape == ImGuiMdShape_Circle)
            sz.x = sz.y = ImMax(sz.x, sz.y) * 1.05f;
        else if (node.Shape == ImGuiMdShape_Diamond)
            sz = ImVec2(sz.x * 1.5f, sz.y * 1.6f);
        node.Size = sz;
    }

    // Cycle removal: the edges which close a cycle are reversed for the layering
    ImVector<int> state;
    state.resize(n);
    memset(state.Data, 0, n * sizeof(int));
    ImVector<char> reversed;
    reversed.resize(d->Edges.Size);
    memset(reversed.Data, 0, d->Edges.Size);
    for (int root = 0; root < n; root++)
    {
        if (state[root] != 0)
            continue;
        ImVector<int> stack_node, stack_next;
        stack_node.push_back(root);
        stack_next.push_back(0);
        state[root] = 1;
        while (!stack_node.empty())
        {
            const int u = stack_node.back();
            int& next = stack_next.back();
            bool pushed = false;
            while (next < d->Edges.Size)
            {
                const int ei = next++;
                const ImGuiMdEdge& e = d->Edges[ei];
                if (e.From != u || e.From == e.To)
                    continue;
                if (state[e.To] == 1)
                    reversed[ei] = 1;
                else if (state[e.To] == 0)
                {
                    state[e.To] = 1;
                    stack_node.push_back(e.To);
                    stack_next.push_back(0);
                    pushed = true;
                    break;
                }
            }
            if (!pushed)
            {
                state[u] = 2;
                stack_node.pop_back();
                stack_next.pop_back();
            }
        }
    }

    // Layers: longest path from the sources
    ImVector<int> layer, indegree;
    layer.resize(n);
    indegree.resize(n);
    memset(layer.Data, 0, n * sizeof(int));
    memset(indegree.Data, 0, n * sizeof(int));
    for (int ei = 0; ei < d->Edges.Size; ei++)
    {
        const ImGuiMdEdge& e = d->Edges[ei];
        if (e.From == e.To)
            continue;
        indegree[reversed[ei] ? e.From : e.To]++;
    }
    ImVector<int> queue;
    for (int i = 0; i < n; i++)
        if (indegree[i] == 0)
            queue.push_back(i);
    for (int head = 0; head < queue.Size; head++)
    {
        const int u = queue[head];
        for (int ei = 0; ei < d->Edges.Size; ei++)
        {
            const ImGuiMdEdge& e = d->Edges[ei];
            if (e.From == e.To)
                continue;
            const int from = reversed[ei] ? e.To : e.From, to = reversed[ei] ? e.From : e.To;
            if (from != u)
                continue;
            layer[to] = ImMax(layer[to], layer[u] + 1);
            if (--indegree[to] == 0)
                queue.push_back(to);
        }
    }

    // Layout nodes, with dummies for the edges which span several layers
    ImVector<ImGuiMdLNode> ln;
    for (int i = 0; i < n; i++)
    {
        ImGuiMdLNode l;
        l.Real = i;
        l.Layer = layer[i];
        l.SizeA = horizontal ? d->Nodes[i].Size.y : d->Nodes[i].Size.x;
        l.SizeB = horizontal ? d->Nodes[i].Size.x : d->Nodes[i].Size.y;
        l.A = l.B = 0.0f;
        l.Index = 0;
        ln.push_back(l);
    }
    ImVector<ImGuiMdSeg> segs;
    ImVector<int> edge_path_start, edge_path_count, paths;      // Per edge: layout nodes from its effective source to its effective target
    edge_path_start.resize(d->Edges.Size);
    edge_path_count.resize(d->Edges.Size);
    for (int ei = 0; ei < d->Edges.Size; ei++)
    {
        const ImGuiMdEdge& e = d->Edges[ei];
        edge_path_start[ei] = paths.Size;
        edge_path_count[ei] = 0;
        if (e.From == e.To)
            continue;
        const int u = reversed[ei] ? e.To : e.From, v = reversed[ei] ? e.From : e.To;
        int prev = u;
        paths.push_back(u);
        for (int l = layer[u] + 1; l < layer[v]; l++)
        {
            ImGuiMdLNode dummy;
            dummy.Real = -1;
            dummy.Layer = l;
            dummy.SizeA = 8.0f;
            dummy.SizeB = 0.0f;
            dummy.A = dummy.B = 0.0f;
            dummy.Index = 0;
            ln.push_back(dummy);
            const int di = ln.Size - 1;
            ImGuiMdSeg s = { prev, di };
            segs.push_back(s);
            paths.push_back(di);
            prev = di;
        }
        ImGuiMdSeg s = { prev, v };
        segs.push_back(s);
        paths.push_back(v);
        edge_path_count[ei] = paths.Size - edge_path_start[ei];
    }

    int layer_count = 1;
    for (int i = 0; i < ln.Size; i++)
        layer_count = ImMax(layer_count, ln[i].Layer + 1);

    // Initial order: by index inside each layer
    ImVector<int> order;                // Nodes grouped by layer
    ImVector<int> first, count;
    first.resize(layer_count + 1);
    count.resize(layer_count);
    memset(count.Data, 0, layer_count * sizeof(int));
    for (int i = 0; i < ln.Size; i++)
        count[ln[i].Layer]++;
    first[0] = 0;
    for (int l = 0; l < layer_count; l++)
        first[l + 1] = first[l] + count[l];
    order.resize(ln.Size);
    {
        ImVector<int> fill;
        fill.resize(layer_count);
        memset(fill.Data, 0, layer_count * sizeof(int));
        for (int i = 0; i < ln.Size; i++)
        {
            const int l = ln[i].Layer;
            order[first[l] + fill[l]] = i;
            ln[i].Index = fill[l]++;
        }
    }

    // Ordering: barycenter sweeps to reduce the crossings
    ImVector<float> bary;
    bary.resize(ln.Size);
    for (int pass = 0; pass < 8; pass++)
    {
        const bool down = (pass % 2) == 0;
        for (int step = 0; step < layer_count; step++)
        {
            const int l = down ? step : layer_count - 1 - step;
            if ((down && l == 0) || (!down && l == layer_count - 1))
                continue;
            for (int k = first[l]; k < first[l + 1]; k++)
            {
                const int node = order[k];
                float sum = 0.0f;
                int num = 0;
                for (int s = 0; s < segs.Size; s++)
                {
                    const int other = down ? (segs[s].B == node ? segs[s].A : -1) : (segs[s].A == node ? segs[s].B : -1);
                    if (other >= 0)
                    {
                        sum += (float)ln[other].Index;
                        num++;
                    }
                }
                bary[node] = num ? sum / (float)num : (float)ln[node].Index;
            }
            for (int a = first[l] + 1; a < first[l + 1]; a++)       // Insertion sort, stable
            {
                const int node = order[a];
                int b = a - 1;
                while (b >= first[l] && bary[order[b]] > bary[node])
                {
                    order[b + 1] = order[b];
                    b--;
                }
                order[b + 1] = node;
            }
            for (int k = first[l]; k < first[l + 1]; k++)
                ln[order[k]].Index = k - first[l];
        }
    }

    // Placement along the layers: neighbours pull, then the nodes are pushed apart
    for (int l = 0; l < layer_count; l++)
    {
        float a = 0.0f;
        for (int k = first[l]; k < first[l + 1]; k++)
        {
            ImGuiMdLNode& node = ln[order[k]];
            node.A = a + node.SizeA * 0.5f;
            a += node.SizeA + IMGUI_MD_NODE_GAP;
        }
    }
    ImVector<float> target;
    target.resize(ln.Size);
    for (int pass = 0; pass < 12; pass++)
    {
        for (int step = 0; step < layer_count; step++)
        {
            const int l = (pass % 2 == 0) ? step : layer_count - 1 - step;
            const int begin = first[l], finish = first[l + 1];
            if (begin == finish)
                continue;
            for (int k = begin; k < finish; k++)
            {
                const int node = order[k];
                float sum = 0.0f;
                int num = 0;
                for (int s = 0; s < segs.Size; s++)
                {
                    if (segs[s].A == node) { sum += ln[segs[s].B].A; num++; }
                    else if (segs[s].B == node) { sum += ln[segs[s].A].A; num++; }
                }
                target[node] = num ? sum / (float)num : ln[node].A;
            }
            float shift = 0.0f;
            float prev_a = 0.0f, prev_size = 0.0f;
            for (int k = begin; k < finish; k++)
            {
                const int node = order[k];
                float a = target[node];
                if (k > begin)
                    a = ImMax(a, prev_a + (prev_size + ln[node].SizeA) * 0.5f + IMGUI_MD_NODE_GAP);
                shift += target[node] - a;
                ln[node].A = a;
                prev_a = a;
                prev_size = ln[node].SizeA;
            }
            shift /= (float)(finish - begin);
            for (int k = begin; k < finish; k++)
                ln[order[k]].A += shift;
        }
    }

    // Placement across the layers
    float b = 0.0f;
    for (int l = 0; l < layer_count; l++)
    {
        float thick = 0.0f;
        for (int k = first[l]; k < first[l + 1]; k++)
            thick = ImMax(thick, ln[order[k]].SizeB);
        for (int k = first[l]; k < first[l + 1]; k++)
            ln[order[k]].B = b + thick * 0.5f;
        b += thick + IMGUI_MD_LAYER_GAP;
    }
    const float total_b = b - IMGUI_MD_LAYER_GAP;

    // To screen coordinates
    ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
    ImVector<ImVec2> screen;
    screen.resize(ln.Size);
    for (int i = 0; i < ln.Size; i++)
    {
        float bb = (d->Dir == ImGuiMdDir_BT || d->Dir == ImGuiMdDir_RL) ? total_b - ln[i].B : ln[i].B;
        ImVec2 p = horizontal ? ImVec2(bb, ln[i].A) : ImVec2(ln[i].A, bb);
        screen[i] = p;
        const ImVec2 half = ln[i].Real >= 0 ? ImVec2(d->Nodes[ln[i].Real].Size.x * 0.5f, d->Nodes[ln[i].Real].Size.y * 0.5f) : ImVec2(0, 0);
        mn = ImVec2(ImMin(mn.x, p.x - half.x), ImMin(mn.y, p.y - half.y));
        mx = ImVec2(ImMax(mx.x, p.x + half.x), ImMax(mx.y, p.y + half.y));
    }
    const ImVec2 offset(IMGUI_MD_MARGIN - mn.x, IMGUI_MD_MARGIN - mn.y);
    for (int i = 0; i < ln.Size; i++)
    {
        screen[i].x += offset.x;
        screen[i].y += offset.y;
        if (ln[i].Real >= 0)
            d->Nodes[ln[i].Real].Pos = screen[i];
    }
    d->Size = ImVec2(mx.x - mn.x + 2.0f * IMGUI_MD_MARGIN, mx.y - mn.y + 2.0f * IMGUI_MD_MARGIN);

    // Edge paths in drawing order (From -> To), the ends on the faces of the nodes
    d->Path.resize(0);
    for (int ei = 0; ei < d->Edges.Size; ei++)
    {
        ImGuiMdEdge& e = d->Edges[ei];
        e.PathStart = d->Path.Size;
        e.PathCount = 0;
        const int cnt = edge_path_count[ei];
        if (cnt == 0)
            continue;
        ImVector<int> pts;
        for (int k = 0; k < cnt; k++)
            pts.push_back(paths[edge_path_start[ei] + (reversed[ei] ? cnt - 1 - k : k)]);
        for (int k = 0; k < pts.Size; k++)
        {
            ImVec2 p = screen[pts[k]];
            if ((k == 0 || k == pts.Size - 1) && ln[pts[k]].Real >= 0)
            {
                const ImVec2 other = screen[pts[k == 0 ? 1 : pts.Size - 2]];
                const ImVec2 sz = d->Nodes[ln[pts[k]].Real].Size;
                if (horizontal)
                    p.x += (other.x > p.x ? 1.0f : -1.0f) * sz.x * 0.5f;
                else
                    p.y += (other.y > p.y ? 1.0f : -1.0f) * sz.y * 0.5f;
            }
            d->Path.push_back(p);
        }
        e.PathCount = pts.Size;
    }
}

//-----------------------------------------------------------------------------
// [SECTION] Layout: sequence diagrams
//-----------------------------------------------------------------------------

static void ImGuiMd_LayoutSequence(ImGuiMdDiagram* d, ImFont* font, float fs)
{
    const int np = d->Parts.Size;
    const float row = fs * 2.0f + 14.0f;
    float header_h = 0.0f;
    for (int i = 0; i < np; i++)
    {
        const ImGuiMdNode& node = d->Nodes[d->Parts[i].NameOffset];
        const ImVec2 ts = ImGuiMd_TextSize(font, fs, d->Str(node.LabelOffset >= 0 ? node.LabelOffset : node.IdOffset));
        d->Parts[i].Size = ImVec2(ts.x + 2.0f * IMGUI_MD_PAD_X, ts.y + 2.0f * IMGUI_MD_PAD_Y);
        header_h = ImMax(header_h, d->Parts[i].Size.y);
    }

    // Distance between the lifelines: wide enough for the texts of the messages between them
    ImVector<float> gap;
    gap.resize(np > 1 ? np - 1 : 1);
    for (int i = 0; i + 1 < np; i++)
        gap[i] = (d->Parts[i].Size.x + d->Parts[i + 1].Size.x) * 0.5f + 20.0f;
    for (int m = 0; m < d->Msgs.Size; m++)
    {
        ImGuiMdMsg& msg = d->Msgs[m];
        msg.TextSize = msg.TextOffset >= 0 ? ImGuiMd_TextSize(font, fs, d->Str(msg.TextOffset)) : ImVec2(0, 0);
        const int lo = ImMin(msg.From, msg.To), hi = ImMax(msg.From, msg.To);
        if (hi > lo && !msg.Note)
            for (int k = lo; k < hi; k++)
                gap[k] = ImMax(gap[k], (msg.TextSize.x + 40.0f) / (float)(hi - lo));
        else if (hi > lo)
            for (int k = lo; k < hi; k++)
                gap[k] = ImMax(gap[k], (msg.TextSize.x + 2.0f * IMGUI_MD_PAD_X) / (float)(hi - lo));
    }
    float x = IMGUI_MD_MARGIN + d->Parts[0].Size.x * 0.5f;
    // Room on the left for a note on the first participant
    for (int m = 0; m < d->Msgs.Size; m++)
        if (d->Msgs[m].Note && d->Msgs[m].Dashed && d->Msgs[m].From == 0)
            x = ImMax(x, IMGUI_MD_MARGIN + d->Msgs[m].TextSize.x + 2.0f * IMGUI_MD_PAD_X + 10.0f);
    d->Parts[0].X = x;
    for (int i = 1; i < np; i++)
        d->Parts[i].X = d->Parts[i - 1].X + gap[i - 1];

    float y = IMGUI_MD_MARGIN + header_h + 14.0f;
    d->SeqHeaderBottom = y - 14.0f;
    for (int m = 0; m < d->Msgs.Size; m++)
    {
        ImGuiMdMsg& msg = d->Msgs[m];
        const float h = msg.Note ? (msg.TextSize.y + 2.0f * IMGUI_MD_PAD_Y + 10.0f) : row;
        msg.Y = y + (msg.Note ? 0.0f : row * 0.6f);
        y += h;
    }
    const float footer_y = y + 6.0f;
    d->Size = ImVec2(d->Parts[np - 1].X + d->Parts[np - 1].Size.x * 0.5f + IMGUI_MD_MARGIN, footer_y + header_h + IMGUI_MD_MARGIN);
    d->Path.resize(0);
    d->Path.push_back(ImVec2(0.0f, footer_y));                  // Path[0].y: top of the bottom participant boxes
    d->Path.push_back(ImVec2(header_h, 0.0f));
}

ImVec2 ImGuiMdDiagrams::GetSize(ImGuiMdDiagram* d, ImFont* font, float font_size)
{
    if (d->LayoutFont != font || d->LayoutFontSize != font_size)
    {
        if (d->Kind == ImGuiMdDiagramKind_Flow)
            ImGuiMd_LayoutFlow(d, font, font_size);
        else
            ImGuiMd_LayoutSequence(d, font, font_size);
        d->LayoutFont = font;
        d->LayoutFontSize = font_size;
    }
    return d->Size;
}

//-----------------------------------------------------------------------------
// [SECTION] Drawing
//-----------------------------------------------------------------------------

struct ImGuiMdPainter
{
    ImDrawList* DrawList;
    ImVec2      Origin;
    float       Scale;
    ImFont*     Font;
    float       FontSize;
    ImU32       Text, Border, Fill, Back, NoteFill;

    ImVec2      P(ImVec2 p) const { return ImVec2(Origin.x + p.x * Scale, Origin.y + p.y * Scale); }
    void        Label(ImVec2 center, const char* text, ImU32 col) const
    {
        const float fs = FontSize * Scale;
        const ImVec2 ts = ImGuiMd_TextSize(Font, fs, text);
        float y = center.y - ts.y * 0.5f;
        const char* line = text;
        for (const char* s = text;; s++)
        {
            if (*s == '\n' || *s == 0)
            {
                const float w = Font->CalcTextSizeA(fs, FLT_MAX, 0.0f, line, s).x;
                DrawList->AddText(Font, fs, ImVec2(center.x - w * 0.5f, y), col, line, s);
                y += fs;
                if (*s == 0)
                    break;
                line = s + 1;
            }
        }
    }
};

static void ImGuiMd_Arrow(const ImGuiMdPainter& pt, ImVec2 tip, ImVec2 from, ImU32 col)
{
    ImVec2 dir(tip.x - from.x, tip.y - from.y);
    const float len = ImSqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.001f)
        return;
    dir = ImVec2(dir.x / len, dir.y / len);
    const float s = 9.0f * pt.Scale;
    const ImVec2 base(tip.x - dir.x * s, tip.y - dir.y * s);
    pt.DrawList->AddTriangleFilled(tip, ImVec2(base.x - dir.y * s * 0.45f, base.y + dir.x * s * 0.45f), ImVec2(base.x + dir.y * s * 0.45f, base.y - dir.x * s * 0.45f), col);
}

static void ImGuiMd_DrawFlow(ImGuiMdDiagram* d, const ImGuiMdPainter& pt)
{
    const bool horizontal = (d->Dir == ImGuiMdDir_LR || d->Dir == ImGuiMdDir_RL);
    const float thin = ImMax(1.0f, pt.Scale);

    // Edges first, so that nodes cover their ends
    for (int ei = 0; ei < d->Edges.Size; ei++)
    {
        const ImGuiMdEdge& e = d->Edges[ei];
        if (e.PathCount < 2)
            continue;
        ImVector<ImVec2> poly;
        for (int k = 0; k + 1 < e.PathCount; k++)
        {
            const ImVec2 p0 = pt.P(d->Path[e.PathStart + k]), p3 = pt.P(d->Path[e.PathStart + k + 1]);
            ImVec2 p1, p2;
            if (horizontal)
            {
                p1 = ImVec2((p0.x + p3.x) * 0.5f, p0.y);
                p2 = ImVec2((p0.x + p3.x) * 0.5f, p3.y);
            }
            else
            {
                p1 = ImVec2(p0.x, (p0.y + p3.y) * 0.5f);
                p2 = ImVec2(p3.x, (p0.y + p3.y) * 0.5f);
            }
            for (int s = (k == 0 ? 0 : 1); s <= 16; s++)
                poly.push_back(ImBezierCubicCalc(p0, p1, p2, p3, (float)s / 16.0f));
        }
        const float thick = (e.Style == 2 ? 3.0f : 1.5f) * thin;
        if (e.Style == 1)
        {
            for (int s = 0; s + 1 < poly.Size; s += 2)
                pt.DrawList->AddLine(poly[s], poly[s + 1], pt.Border, thick);
        }
        else
            pt.DrawList->AddPolyline(poly.Data, poly.Size, pt.Border, ImDrawFlags_None, thick);
        if (e.Arrow && poly.Size >= 3)
            ImGuiMd_Arrow(pt, poly[poly.Size - 1], poly[poly.Size - 3], pt.Border);
        if (e.ArrowStart && poly.Size >= 3)
            ImGuiMd_Arrow(pt, poly[0], poly[2], pt.Border);

        if (e.LabelOffset >= 0)
        {
            float total = 0.0f;
            for (int s = 0; s + 1 < poly.Size; s++)
                total += ImSqrt(ImLengthSqr(ImVec2(poly[s + 1].x - poly[s].x, poly[s + 1].y - poly[s].y)));
            float acc = 0.0f;
            ImVec2 mid = poly[poly.Size / 2];
            for (int s = 0; s + 1 < poly.Size; s++)
            {
                acc += ImSqrt(ImLengthSqr(ImVec2(poly[s + 1].x - poly[s].x, poly[s + 1].y - poly[s].y)));
                if (acc >= total * 0.5f)
                {
                    mid = poly[s];
                    break;
                }
            }
            const char* label = d->Str(e.LabelOffset);
            const ImVec2 ts = ImGuiMd_TextSize(pt.Font, pt.FontSize * pt.Scale, label);
            pt.DrawList->AddRectFilled(ImVec2(mid.x - ts.x * 0.5f - 3.0f, mid.y - ts.y * 0.5f - 1.0f), ImVec2(mid.x + ts.x * 0.5f + 3.0f, mid.y + ts.y * 0.5f + 1.0f), pt.Back, 3.0f);
            pt.Label(mid, label, pt.Text);
        }
    }

    for (int i = 0; i < d->Nodes.Size; i++)
    {
        const ImGuiMdNode& node = d->Nodes[i];
        const ImVec2 c = pt.P(node.Pos);
        const ImVec2 hs(node.Size.x * 0.5f * pt.Scale, node.Size.y * 0.5f * pt.Scale);
        const ImVec2 mn(c.x - hs.x, c.y - hs.y), mx(c.x + hs.x, c.y + hs.y);
        switch (node.Shape)
        {
        case ImGuiMdShape_Round:
            pt.DrawList->AddRectFilled(mn, mx, pt.Fill, hs.y);
            pt.DrawList->AddRect(mn, mx, pt.Border, hs.y, 0, 1.5f * thin);
            break;
        case ImGuiMdShape_Circle:
            pt.DrawList->AddCircleFilled(c, hs.x, pt.Fill, 32);
            pt.DrawList->AddCircle(c, hs.x, pt.Border, 32, 1.5f * thin);
            break;
        case ImGuiMdShape_Diamond:
        {
            const ImVec2 pts[4] = { ImVec2(c.x, mn.y), ImVec2(mx.x, c.y), ImVec2(c.x, mx.y), ImVec2(mn.x, c.y) };
            pt.DrawList->AddConvexPolyFilled(pts, 4, pt.Fill);
            pt.DrawList->AddPolyline(pts, 4, pt.Border, ImDrawFlags_Closed, 1.5f * thin);
            break;
        }
        default:
            pt.DrawList->AddRectFilled(mn, mx, pt.Fill, 4.0f * pt.Scale);
            pt.DrawList->AddRect(mn, mx, pt.Border, 4.0f * pt.Scale, 0, 1.5f * thin);
            break;
        }
        pt.Label(c, d->Str(node.LabelOffset >= 0 ? node.LabelOffset : node.IdOffset), pt.Text);
    }
}

static void ImGuiMd_DrawSequence(ImGuiMdDiagram* d, const ImGuiMdPainter& pt)
{
    const float thin = ImMax(1.0f, pt.Scale);
    const float footer_y = d->Path[0].y;
    const float header_h = d->Path[1].x;

    for (int i = 0; i < d->Parts.Size; i++)
    {
        const ImGuiMdPart& part = d->Parts[i];
        const ImGuiMdNode& node = d->Nodes[part.NameOffset];
        const char* name = d->Str(node.LabelOffset >= 0 ? node.LabelOffset : node.IdOffset);
        // Lifeline
        const ImVec2 a = pt.P(ImVec2(part.X, IMGUI_MD_MARGIN + header_h)), b = pt.P(ImVec2(part.X, footer_y));
        for (float y = a.y; y < b.y; y += 10.0f * pt.Scale)
            pt.DrawList->AddLine(ImVec2(a.x, y), ImVec2(a.x, ImMin(y + 5.0f * pt.Scale, b.y)), pt.Border, thin);
        // Boxes at the top and at the bottom
        for (int k = 0; k < 2; k++)
        {
            const float top = (k == 0) ? IMGUI_MD_MARGIN : footer_y;
            const ImVec2 mn = pt.P(ImVec2(part.X - part.Size.x * 0.5f, top)), mx = pt.P(ImVec2(part.X + part.Size.x * 0.5f, top + header_h));
            pt.DrawList->AddRectFilled(mn, mx, pt.Fill, 4.0f * pt.Scale);
            pt.DrawList->AddRect(mn, mx, pt.Border, 4.0f * pt.Scale, 0, 1.5f * thin);
            pt.Label(ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), name, pt.Text);
        }
    }

    for (int m = 0; m < d->Msgs.Size; m++)
    {
        const ImGuiMdMsg& msg = d->Msgs[m];
        const char* text = msg.TextOffset >= 0 ? d->Str(msg.TextOffset) : "";
        if (msg.Note)
        {
            const float w = msg.TextSize.x + 2.0f * IMGUI_MD_PAD_X;
            const float h = msg.TextSize.y + 2.0f * IMGUI_MD_PAD_Y;
            float x0, x1;
            if (msg.Dashed)         { x1 = d->Parts[msg.From].X - 8.0f; x0 = x1 - w; }                         // left of
            else if (msg.Head)      { x0 = d->Parts[msg.From].X + 8.0f; x1 = x0 + w; }                         // right of
            else
            {
                const float xa = d->Parts[ImMin(msg.From, msg.To)].X, xb = d->Parts[ImMax(msg.From, msg.To)].X;
                const float cx = (xa + xb) * 0.5f;
                x0 = ImMin(xa - 10.0f, cx - w * 0.5f);
                x1 = ImMax(xb + 10.0f, cx + w * 0.5f);
            }
            const ImVec2 mn = pt.P(ImVec2(x0, msg.Y)), mx = pt.P(ImVec2(x1, msg.Y + h));
            pt.DrawList->AddRectFilled(mn, mx, pt.NoteFill, 3.0f * pt.Scale);
            pt.DrawList->AddRect(mn, mx, pt.Border, 3.0f * pt.Scale, 0, thin);
            pt.Label(ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), text, pt.Text);
            continue;
        }
        const float xa = d->Parts[msg.From].X, xb = d->Parts[msg.To].X;
        const ImVec2 a = pt.P(ImVec2(xa, msg.Y)), b = pt.P(ImVec2(xb, msg.Y));
        if (msg.From == msg.To)                                     // Message to self: a small loop
        {
            const float w = 26.0f * pt.Scale;
            pt.DrawList->AddLine(a, ImVec2(a.x + w, a.y), pt.Border, 1.5f * thin);
            pt.DrawList->AddLine(ImVec2(a.x + w, a.y), ImVec2(a.x + w, a.y + 14.0f * pt.Scale), pt.Border, 1.5f * thin);
            pt.DrawList->AddLine(ImVec2(a.x + w, a.y + 14.0f * pt.Scale), ImVec2(a.x, a.y + 14.0f * pt.Scale), pt.Border, 1.5f * thin);
            ImGuiMd_Arrow(pt, ImVec2(a.x, a.y + 14.0f * pt.Scale), ImVec2(a.x + w, a.y + 14.0f * pt.Scale), pt.Border);
            pt.DrawList->AddText(pt.Font, pt.FontSize * pt.Scale, ImVec2(a.x + w + 6.0f, a.y - 2.0f), pt.Text, text);
            continue;
        }
        if (msg.Dashed)
        {
            const float dir = (b.x > a.x) ? 1.0f : -1.0f;
            for (float x = a.x; (b.x - x) * dir > 0.0f; x += 8.0f * pt.Scale * dir)
                pt.DrawList->AddLine(ImVec2(x, a.y), ImVec2(x + 4.0f * pt.Scale * dir, a.y), pt.Border, 1.5f * thin);
        }
        else
            pt.DrawList->AddLine(a, b, pt.Border, 1.5f * thin);
        if (msg.Head)
            ImGuiMd_Arrow(pt, b, a, pt.Border);
        if (msg.TextOffset >= 0)
            pt.Label(ImVec2((a.x + b.x) * 0.5f, a.y - msg.TextSize.y * 0.5f * pt.Scale - 4.0f * pt.Scale), text, pt.Text);
    }
}

void ImGuiMdDiagrams::Draw(ImGuiMdDiagram* d, ImDrawList* draw_list, ImVec2 pos, float scale)
{
    ImGuiMdPainter pt;
    pt.DrawList = draw_list;
    pt.Origin = pos;
    pt.Scale = scale;
    pt.Font = d->LayoutFont;
    pt.FontSize = d->LayoutFontSize;
    pt.Text = ImGui::GetColorU32(ImGuiCol_Text);
    ImVec4 border = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    border.w *= 0.75f;
    pt.Border = ImGui::GetColorU32(border);
    pt.Fill = ImGui::GetColorU32(ImGuiCol_FrameBg);
    pt.Back = ImGui::GetColorU32(ImGuiCol_WindowBg);
    ImVec4 note = ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram);
    note.w = 0.25f;
    pt.NoteFill = ImGui::GetColorU32(note);
    if (d->Kind == ImGuiMdDiagramKind_Flow)
        ImGuiMd_DrawFlow(d, pt);
    else
        ImGuiMd_DrawSequence(d, pt);
}

//-----------------------------------------------------------------------------
// [SECTION] Public API
//-----------------------------------------------------------------------------

struct ImGuiMdDiagramCacheEntry
{
    ImGuiID         Hash;
    ImGuiMdDiagram* Diagram;        // NULL: not a supported diagram
};

struct ImGuiMdDiagramCache
{
    ImVector<ImGuiMdDiagramCacheEntry> Entries;
};

ImGuiMdDiagramCache* ImGuiMdDiagrams::CreateCache()
{
    return IM_NEW(ImGuiMdDiagramCache)();
}

void ImGuiMdDiagrams::DestroyCache(ImGuiMdDiagramCache* cache)
{
    for (int n = 0; n < cache->Entries.Size; n++)
        IM_DELETE(cache->Entries[n].Diagram);
    IM_DELETE(cache);
}

float ImGuiMdDiagrams::CodeBlock(ImGuiMdDiagramCache* cache, const char* language, const char* text, int text_length, float width, bool draw, ImVec2 pos)
{
    if (ImStricmp(language, "mermaid") != 0)
        return -1.0f;
    const ImGuiID hash = ImHashData(text, text_length, 0x4D4D);
    ImGuiMdDiagram* diagram = NULL;
    bool found = false;
    for (int n = 0; n < cache->Entries.Size; n++)
        if (cache->Entries[n].Hash == hash)
        {
            diagram = cache->Entries[n].Diagram;
            found = true;
            break;
        }
    if (!found)
    {
        if (cache->Entries.Size >= 64)
        {
            for (int n = 0; n < cache->Entries.Size; n++)
                IM_DELETE(cache->Entries[n].Diagram);
            cache->Entries.resize(0);
        }
        ImGuiMdDiagramCacheEntry entry;
        entry.Hash = hash;
        entry.Diagram = diagram = ImGuiMdDiagrams::Parse(text, text_length);
        cache->Entries.push_back(entry);
    }
    if (diagram == NULL)
        return -1.0f;

    const ImVec2 size = ImGuiMdDiagrams::GetSize(diagram, ImGui::GetFont(), ImGui::GetFontSize());
    const float scale = (size.x > width && size.x > 0.0f) ? width / size.x : 1.0f;
    if (draw)
        ImGuiMdDiagrams::Draw(diagram, ImGui::GetWindowDrawList(), ImVec2(pos.x + (width - size.x * scale) * 0.5f, pos.y + 4.0f), scale);
    return size.y * scale + 8.0f;
}
