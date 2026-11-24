// Binote - www.rebisway.com - contact@rebisway.com
// Copyright (C) 2026 Thomas Gourgnier
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// Binote - proper names of the project: how often each one is written, and the spellings which are probably the same name

#include "ui/app.h"
#include "common/i18n.h"
#include <algorithm>
#include <map>
#include <set>

namespace
{
	unsigned int decode(const std::string& s, size_t i, int* n)
	{
		const unsigned char b = (unsigned char)s[i];
		*n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
		if (i + *n > s.size())
			*n = 1;
		unsigned int c = *n == 1 ? b : (b & (0xFF >> (*n + 1)));
		for (int k = 1; k < *n; k++)
			c = (c << 6) | ((unsigned char)s[i + k] & 0x3F);
		return c;
	}

	bool is_letter(unsigned int c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || c == 0x153 || c == 0x152;
	}

	bool is_upper(unsigned int c)
	{
		return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || c == 0x152;
	}

	// Lower case without accents, to compare spellings: "Étienne" and "Etienne" are the same
	std::string fold(const std::string& s)
	{
		static const char* const plain = "aaaaaaaceeeeiiiidnooooo/ouuuuyts";			// from U+00C0
		std::string o;
		for (size_t i = 0; i < s.size();)
		{
			int n;
			unsigned int c = decode(s, i, &n);
			i += n;
			if (c >= 'A' && c <= 'Z')
				c += 32;
			else if (c >= 0xC0 && c <= 0xDF)
				c = (unsigned char)plain[c - 0xC0];
			else if (c >= 0xE0 && c <= 0xFF)
				c = (unsigned char)plain[c - 0xE0];
			else if (c == 0x153 || c == 0x152)
				c = 'o';
			else if (c >= 0x100 && c <= 0x17F)
				c = '?';
			if (c < 0x80)
				o += (char)c;
			else
				o += '?';
		}
		return o;
	}

	int distance(const std::string& a, const std::string& b)
	{
		std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
		for (size_t j = 0; j <= b.size(); j++)
			prev[j] = (int)j;
		for (size_t i = 1; i <= a.size(); i++)
		{
			cur[0] = (int)i;
			for (size_t j = 1; j <= b.size(); j++)
				cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
			std::swap(prev, cur);
		}
		return prev[b.size()];
	}
}

// The capitalized words which are not at the start of a sentence, in every chapter
void App::scan_names()
{
	struct Count { int total = 0; std::set<int> docs; };
	std::map<std::string, Count> names;
	for (int di = 0; di < (int)_project.docs.size(); di++)
	{
		std::string text;
		if (di == _cur)
			text = editor_text(_dst.editor);
		else if (!(util::exists(_project.docs[(size_t)di].dst_path) && util::read_text(_project.docs[(size_t)di].dst_path, text)))
			util::read_text(_project.docs[(size_t)di].src_path, text);
		for (const std::string& line : util::split_lines(text))
		{
			if (!line.empty() && line.find_first_not_of(" \t") != std::string::npos && line[line.find_first_not_of(" \t")] == '#')
				continue;
			bool sentence_start = true;						// the next word starts a sentence
			for (size_t i = 0; i < line.size();)
			{
				int n;
				const unsigned int c = decode(line, i, &n);
				if (is_letter(c))
				{
					size_t j = i;
					while (j < line.size())
					{
						int m;
						const unsigned int d = decode(line, j, &m);
						if (!is_letter(d))
							break;
						j += m;
					}
					const std::string word = line.substr(i, j - i);
					if (is_upper(c) && !sentence_start && word.size() >= 4)
					{
						Count& cnt = names[word];
						cnt.total++;
						cnt.docs.insert(di);
					}
					sentence_start = false;
					i = j;
					continue;
				}
				if (c == '.' || c == '!' || c == '?' || c == 0x2026 || c == ':' || c == 0x2014 || c == 0x2013)
					sentence_start = true;
				else if (c == 0xAB || c == 0x201C || c == '"' || c == '(' || c == '*' || c == '_' || c == ' ' || c == '\t' || c == 0xA0 || c == 0x202F)
				{
					// opening marks and spaces do not change what comes before
				}
				else if (c == '\'' || c == 0x2019 || c == '-')
				{
				}
				else
					sentence_start = false;
				i += n;
			}
		}
	}
	_names.clear();
	for (const auto& kv : names)
		_names.push_back({ kv.first, kv.second.total, (int)kv.second.docs.size() });
	std::sort(_names.begin(), _names.end(), [](const NameCount& a, const NameCount& b) { return a.count > b.count; });
	// Spellings which look like the same name: only an accent or a letter apart
	_name_variants.clear();
	for (size_t i = 0; i < _names.size() && i < 600; i++)
		for (size_t j = i + 1; j < _names.size() && j < 600; j++)
		{
			const std::string a = fold(_names[i].name), b = fold(_names[j].name);
			const int d = a == b ? 0 : distance(a, b);
			if ((a == b) || (d == 1 && a.size() >= 5) || (d == 2 && a.size() >= 9 && b.size() >= 9))
				_name_variants.push_back({ (int)i, (int)j });
		}
	_names_scanned = true;
}

// Names window: proper names found in the project with their count and chapters, and the variants (probable misspellings of the same name)
void App::draw_names()
{
	ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("NAMES_TITLE"), &_show_names))
	{
		ImGui::End();
		return;
	}
	if (ImGui::IsWindowAppearing() || !_names_scanned)
		scan_names();
	if (ImGui::Button(TR("NAMES_RESCAN")))
		scan_names();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", TR("NAMES_HELP"));
	ImGui::Separator();
	ImGui::TextUnformatted(i18n::fmt(TR("NAMES_VARIANTS"), (int)_name_variants.size()).c_str());
	ImGui::BeginChild("##name_variants", ImVec2(0, 150.0f), true);
	for (size_t k = 0; k < _name_variants.size(); k++)
	{
		const NameCount& a = _names[(size_t)_name_variants[k].first];
		const NameCount& b = _names[(size_t)_name_variants[k].second];
		// The less frequent spelling is the one to replace
		const NameCount& from = a.count < b.count ? a : b;
		const NameCount& to = a.count < b.count ? b : a;
		const std::string label = from.name + " (" + std::to_string(from.count) + ")  \xE2\x86\x92  " + to.name + " (" + std::to_string(to.count) + ")##variant" + std::to_string(k);
		if (ImGui::Selectable(label.c_str()))
		{
			snprintf(_find_text, sizeof(_find_text), "%s", from.name.c_str());
			snprintf(_replace_text, sizeof(_replace_text), "%s", to.name.c_str());
			_find_regex = false;
			_find_word = true;
			_find_case = true;
			_show_find = true;
			update_matches();
			rebuild_spans();
		}
	}
	ImGui::EndChild();
	ImGui::Text("%s", i18n::fmt(TR("NAMES_LIST"), (int)_names.size()).c_str());
	ImGui::BeginChild("##names_list", ImVec2(0, 0), true);
	if (ImGui::BeginTable("##names", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable))
	{
		ImGui::TableSetupColumn(TR("NAMES_NAME"));
		ImGui::TableSetupColumn(TR("NAMES_COUNT"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn(TR("NAMES_CHAPTERS"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableHeadersRow();
		for (const NameCount& n : _names)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (ImGui::Selectable((n.name + "##nm" + n.name).c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
			{
				snprintf(_find_text, sizeof(_find_text), "%s", n.name.c_str());
				_find_regex = false;
				_find_word = true;
				_find_case = true;
				_show_find = true;
				_find_all_chapters = true;
				update_matches();
			}
			ImGui::TableNextColumn();
			ImGui::Text("%d", n.count);
			ImGui::TableNextColumn();
			ImGui::Text("%d", n.chapters);
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();
	ImGui::End();
}
