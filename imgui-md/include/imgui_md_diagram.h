// imgui_md_diagram : small diagram renderer for Dear ImGui (MIT license)
// Reads a subset of the Mermaid syntax and draws it with the ImGui draw list (vector, follows the ImGui style).
// Written for imgui_md (fenced code blocks), usable alone.
//
// Supported:
// - flowcharts: graph/flowchart TD|TB|BT|LR|RL, nodes A[text] A(text) A((text)) A{text}, edges --> --- -.-> ==> with |label| or -- label -->
// - sequence diagrams: participant/actor, messages ->> -->> -> --> with text, notes (over, left of, right of)
// Anything else (subgraphs, styling, other diagram types) is ignored or makes Parse() return NULL.

// Index of this file:
// [SECTION] Forward declarations
// [SECTION] Functions

#pragma once
#include "imgui.h"

//-----------------------------------------------------------------------------
// [SECTION] Forward declarations
//-----------------------------------------------------------------------------

#ifndef IMGUI_MD_API
#define IMGUI_MD_API
#endif

struct ImGuiMdDiagram;                  // A parsed diagram
struct ImGuiMdDiagramCache;             // Parsed and laid out diagrams, keyed by their text

//-----------------------------------------------------------------------------
// [SECTION] Functions
//-----------------------------------------------------------------------------

namespace ImGuiMdDiagrams
{
    // Low level
    IMGUI_MD_API ImGuiMdDiagram*    Parse(const char* text, int text_length);       // NULL if the text is not a supported diagram
    IMGUI_MD_API void               DestroyDiagram(ImGuiMdDiagram* diagram);
    IMGUI_MD_API ImVec2             GetSize(ImGuiMdDiagram* diagram, ImFont* font, float font_size);   // Natural size (laid out on first call)
    IMGUI_MD_API void               Draw(ImGuiMdDiagram* diagram, ImDrawList* draw_list, ImVec2 pos, float scale);

    // High level, matching ImGuiMdConfig::CodeBlockCallback. Returns a negative value if 'language' is not a diagram language.
    IMGUI_MD_API ImGuiMdDiagramCache* CreateCache();
    IMGUI_MD_API void               DestroyCache(ImGuiMdDiagramCache* cache);
    IMGUI_MD_API float              CodeBlock(ImGuiMdDiagramCache* cache, const char* language, const char* text, int text_length, float width, bool draw, ImVec2 pos);
}
