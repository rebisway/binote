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

// Binote - list of the spelling and grammar problems of the destination, with their line numbers

#include "ui/app.h"
#include "common/i18n.h"
#include <algorithm>

// Ctrl+F7: spelling on or off
void App::toggle_spell()
{
	if (!_spell.available())
	{
		status(TR("MSG_SPELL_MISSING"));
		return;
	}
	_s.spell_check = !_s.spell_check;
	_s.save();
	_problems_version = -1;
	rebuild_spans();
	status(TR(_s.spell_check ? "MSG_SPELL_ON" : "MSG_SPELL_OFF"));
}

// Ctrl+F8: grammar on or off
void App::toggle_grammar()
{
	_s.grammar_check = !_s.grammar_check;
	if (_s.grammar_check)
		python_notice();
	_s.save();
	_problems_version = -1;
	rebuild_spans();
	status(TR(_s.grammar_check ? "MSG_GRAMMAR_ON" : "MSG_GRAMMAR_OFF"));
}

// Every problem of the destination, in the order of the text (the same lines are skipped as for the underlines: code)
void App::collect_problems()
{
	_problems.clear();
	ImGuiProseEditor* ed = _dst.editor;
	const int lines = ImGuiProse::GetLineCount(ed);
	bool fence = false;
	std::vector<std::pair<int, int>> errors;
	for (int i = 1; i <= lines; i++)
	{
		int len = 0;
		const char* s = ImGuiProse::GetLine(ed, i, &len);
		bool heading = false;
		if (_s.markdown)
		{
			int a = 0;
			while (a < len && (s[a] == ' ' || s[a] == '\t')) a++;
			if (len - a >= 3 && (strncmp(s + a, "```", 3) == 0 || strncmp(s + a, "~~~", 3) == 0))
			{
				fence = !fence;
				continue;
			}
			heading = a < len && s[a] == '#';
		}
		if (fence)
			continue;
		if (_s.spell_check && _spell.available())
		{
			_spell.find_errors(s, len, _s.markdown, errors);
			for (const auto& e : errors)
			{
				Problem p;
				p.line = i;
				p.start = e.first;
				p.end = e.second;
				p.kind = 0;
				p.text.assign(s + e.first, (size_t)(e.second - e.first));
				const std::vector<std::string> sug = _spell.suggest(p.text);
				p.detail = sug.empty() ? "" : sug[0];
				_problems.push_back(p);
			}
		}
		if (_s.grammar_check && !heading)
			for (const grammar::Issue& is : grammar_issues(std::string(s, (size_t)len)))
			{
				Problem p;
				p.line = i;
				p.start = is.start;
				p.end = is.end;
				p.kind = 1;
				p.text.assign(s + is.start, (size_t)(is.end - is.start));
				p.detail = is.suggestions.empty() ? "" : (is.suggestions[0].empty() ? std::string(TR("GRAM_DELETE")) : is.suggestions[0]);
				p.message = !is.message.empty() ? is.message : i18n::fmt(TR(grammar::message_key(is.rule).c_str()), is.arg.c_str());
				_problems.push_back(p);
			}
	}
	std::stable_sort(_problems.begin(), _problems.end(), [](const Problem& a, const Problem& b) { return a.line != b.line ? a.line < b.line : a.start < b.start; });
	_problems_version = ImGuiProse::GetVersion(ed);
}

// Window of the problems (Ctrl+F9): updated a moment after each change; a click on a row selects the passage
void App::draw_problems()
{
	ImGui::SetNextWindowSize(ImVec2(620.0f, 420.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("PROBLEMS_TITLE"), &_show_problems))
	{
		ImGui::End();
		return;
	}
	// The list follows the text, a little after each change
	const int version = ImGuiProse::GetVersion(_dst.editor);
	if (_problems_version != version && (_problems_due < 0.0 || ImGui::GetTime() >= _problems_due))
	{
		if (_problems_due < 0.0 && _problems_version >= 0)
			_problems_due = ImGui::GetTime() + 0.5;
		else
		{
			collect_problems();
			_problems_due = -1.0;
		}
	}
	bool refresh = ImGui::Button(TR("PROBLEMS_REFRESH"));
	ImGui::SameLine();
	if (ImGui::Checkbox(TR("PROBLEMS_SPELLING"), &_s.spell_check))
	{
		_s.save();
		refresh = true;
		rebuild_spans();
	}
	ImGui::SameLine();
	if (ImGui::Checkbox(TR("PROBLEMS_GRAMMAR"), &_s.grammar_check))
	{
		_s.save();
		refresh = true;
		rebuild_spans();
	}
	if (refresh)
		collect_problems();
	int spelling = 0, grammar = 0;
	for (const Problem& p : _problems)
		(p.kind == 0 ? spelling : grammar)++;
	ImGui::SameLine();
	ImGui::TextDisabled("%s", i18n::fmt(TR("PROBLEMS_COUNT"), spelling, grammar).c_str());
	ImGui::BeginChild("##problems_list", ImVec2(0, 0), true);
	if (ImGui::BeginTable("##problems", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn(TR("PROBLEMS_LINE"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn(TR("PROBLEMS_KIND"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn(TR("PROBLEMS_TEXT"), ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn(TR("PROBLEMS_SUGGESTION"), ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn(TR("PROBLEMS_MESSAGE"));
		ImGui::TableHeadersRow();
		ImGuiListClipper clipper;
		clipper.Begin((int)_problems.size());
		while (clipper.Step())
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				const Problem& p = _problems[(size_t)i];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable((std::to_string(p.line) + "##problem" + std::to_string(i)).c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
				{
					_active = &_dst;
					ImGuiProse::SetSelection(_dst.editor, ImGuiProsePos(p.line, p.start), ImGuiProsePos(p.line, p.end));
					ImGuiProse::SetCaret(_dst.editor, ImGuiProsePos(p.line, p.end), false);
					ImGuiProse::ScrollToLine(_dst.editor, p.line, 0.3f);
				}
				ImGui::TableNextColumn();
				ImGui::TextColored(p.kind == 0 ? ImVec4(0.95f, 0.45f, 0.45f, 1.0f) : ImVec4(0.4f, 0.65f, 0.95f, 1.0f), "%s", TR(p.kind == 0 ? "PROBLEMS_KIND_SPELLING" : "PROBLEMS_KIND_GRAMMAR"));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(p.text.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(p.detail.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(p.message.c_str());
			}
		ImGui::EndTable();
	}
	ImGui::EndChild();
	ImGui::End();
}

// Window of the excluded files: the files of the folder which are not chapters of the project, with a button to put each one back
void App::draw_excluded()
{
	ImGui::SetNextWindowSize(ImVec2(420.0f, 260.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("DLG_EXCLUDED_TITLE"), &_show_excluded))
	{
		ImGui::End();
		return;
	}
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("%s", TR("EXCLUDED_HELP"));
	ImGui::PopTextWrapPos();
	ImGui::Separator();
	if (!_has_project || !_project.is_folder || _project.excluded.empty())
		ImGui::TextUnformatted(TR("EXCLUDED_NONE"));
	else
	{
		std::string back;
		if (ImGui::BeginTable("##excluded", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			for (const std::string& name : _project.excluded)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::PushID(name.c_str());
				if (ImGui::SmallButton(TR("TREE_INCLUDE")))
					back = name;
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (!back.empty())
			include_doc(back);
	}
	ImGui::End();
}
