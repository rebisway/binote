// Binote - table of characters: to insert the characters which are not on the keyboard
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app.h"
#include "i18n.h"
#include <imgui_internal.h>
#include <algorithm>

namespace
{
	struct Named { unsigned int cp; const char* abbreviation; const char* key; };

	// Spaces and invisible characters: they are shown by their abbreviation
	const Named SPACES[] = {
		{ 0x00A0, "NBSP", "CHAR_NBSP" }, { 0x202F, "NNBSP", "CHAR_NNBSP" }, { 0x2009, "THIN", "CHAR_THIN" }, { 0x200A, "HAIR", "CHAR_HAIR" }, { 0x2002, "EN", "CHAR_EN_SPACE" },
		{ 0x2003, "EM", "CHAR_EM_SPACE" }, { 0x2007, "FIG", "CHAR_FIGURE_SPACE" }, { 0x2008, "PUNC", "CHAR_PUNCT_SPACE" }, { 0x200B, "ZWSP", "CHAR_ZWSP" }, { 0x200C, "ZWNJ", "CHAR_ZWNJ" },
		{ 0x200D, "ZWJ", "CHAR_ZWJ" }, { 0x2060, "WJ", "CHAR_WORD_JOINER" }, { 0x00AD, "SHY", "CHAR_SHY" }, { 0x2011, "NB-", "CHAR_NBHYPHEN" }, { 0x200E, "LRM", "CHAR_LRM" },
		{ 0x200F, "RLM", "CHAR_RLM" }, { 0x0009, "TAB", "CHAR_TAB" }, { 0xFEFF, "BOM", "CHAR_BOM" } };

	struct Range { unsigned int first, last; };

	const char* const PUNCTUATION = "«»‹›“”‘’„‚…–—‑−·•‰†‡§¶¡¿′″‼⁇⁈⁉※⁂′‐‒―⸺⸻";
	const char* const SYMBOLS = "©®™°±×÷≈≠≤≥∞√∑∏πµ¼½¾²³¹⁰⁴⁵⁶⁷⁸⁹₀₁₂₃₄₅₆₇₈₉€£¥¢$¤¬¦←↑→↓↔⇒⇔✓✗★☆♥♪♫☀☁☂☎✉✂✎";

	// Characters of a text in UTF-8, one per entry
	std::vector<unsigned int> chars_of(const char* text)
	{
		std::vector<unsigned int> out;
		for (const char* p = text; *p;)
		{
			unsigned int c = 0;
			const int n = ImTextCharFromUtf8(&c, p, nullptr);
			if (n <= 0)
				break;
			out.push_back(c);
			p += n;
		}
		return out;
	}

	std::string utf8_of(unsigned int cp)
	{
		char b[8];
		ImTextCharToUtf8(b, cp);
		return b;
	}
}

void App::draw_charmap()
{
	ImGui::SetNextWindowSize(ImVec2(560.0f, 440.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x + 420.0f, ImGui::GetMainViewport()->WorkPos.y + 120.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("CHARMAP_TITLE"), &_show_charmap))
	{
		ImGui::End();
		return;
	}
	static const char* const groups[] = { "CHARMAP_RECENT", "CHARMAP_SPACES", "CHARMAP_PUNCTUATION", "CHARMAP_LATIN", "CHARMAP_LATIN_EXT", "CHARMAP_SYMBOLS", "CHARMAP_ARROWS", "CHARMAP_MATH", "CHARMAP_GREEK",
		"CHARMAP_CYRILLIC", "CHARMAP_BOXES" };
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::BeginCombo("##charmap_group", TR(groups[_charmap_group])))
	{
		for (int i = 0; i < IM_ARRAYSIZE(groups); i++)
			if (ImGui::Selectable(TR(groups[i]), i == _charmap_group))
				_charmap_group = i;
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", TR("CHARMAP_HELP"));

	// The characters of the group
	std::vector<unsigned int> list;
	bool named = false;
	auto add_range = [&](unsigned int a, unsigned int b) { for (unsigned int c = a; c <= b; c++) if (c != 0xD7 && c != 0xF7) list.push_back(c); };
	switch (_charmap_group)
	{
	case 0: list = _charmap_recent; break;
	case 1: named = true; break;
	case 2: list = chars_of(PUNCTUATION); break;
	case 3: add_range(0xC0, 0xFF); list.push_back(0x152); list.push_back(0x153); list.push_back(0x178); break;
	case 4: add_range(0x100, 0x17F); break;
	case 5: list = chars_of(SYMBOLS); break;
	case 6: add_range(0x2190, 0x21FF); break;
	case 7: add_range(0x2200, 0x22FF); break;
	case 8: add_range(0x391, 0x3C9); break;
	case 9: add_range(0x410, 0x44F); break;
	case 10: add_range(0x2500, 0x257F); break;
	}

	ImGui::BeginChild("##charmap_grid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.0f), true);
	const float cell = named ? 64.0f : 34.0f;
	const float width = ImGui::GetContentRegionAvail().x;
	const int per_row = std::max(1, (int)(width / (cell + ImGui::GetStyle().ItemSpacing.x)));
	int index = 0;
	auto insert = [&](unsigned int cp)
	{
		insert_text(utf8_of(cp));
		_charmap_recent.erase(std::remove(_charmap_recent.begin(), _charmap_recent.end(), cp), _charmap_recent.end());
		_charmap_recent.insert(_charmap_recent.begin(), cp);
		if (_charmap_recent.size() > 48)
			_charmap_recent.pop_back();
	};
	if (named)
	{
		for (const Named& n : SPACES)
		{
			if (index++ % per_row)
				ImGui::SameLine();
			ImGui::PushID((int)n.cp);
			if (ImGui::Button(n.abbreviation, ImVec2(cell, 28.0f)))
				insert(n.cp);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("U+%04X  %s", n.cp, TR(n.key));
			ImGui::PopID();
		}
	}
	else
		for (unsigned int cp : list)
		{
			if (index++ % per_row)
				ImGui::SameLine();
			ImGui::PushID((int)cp);
			const std::string glyph = utf8_of(cp);
			if (ImGui::Button(glyph.c_str(), ImVec2(cell, cell)))
				insert(cp);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("U+%04X", cp);
			ImGui::PopID();
		}
	if (!named && list.empty())
		ImGui::TextDisabled("%s", TR("CHARMAP_EMPTY"));
	ImGui::EndChild();

	// Any character by its code
	ImGui::SetNextItemWidth(110.0f);
	const bool enter = ImGui::InputTextWithHint("##charmap_code", "U+00A0", _charmap_code, sizeof(_charmap_code), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	if ((ImGui::Button(TR("CHARMAP_INSERT")) || enter) && _charmap_code[0])
	{
		const unsigned int cp = (unsigned int)strtoul(_charmap_code, nullptr, 16);
		if (cp > 0 && cp < 0x110000)
			insert(cp);
	}
	ImGui::End();
}
