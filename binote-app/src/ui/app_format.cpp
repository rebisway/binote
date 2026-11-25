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

// Binote - Markdown buttons: bold, italic, headings, lists, quote, code, link, rule. Each action is one undoable replacement of the text.

#include "ui/app.h"
#include "common/i18n.h"
#include <algorithm>
#include <cctype>

namespace
{
	// The text of a line of the editor
	std::string line_of(ImGuiProseEditor* ed, int line)
	{
		int len = 0;
		const char* s = ImGuiProse::GetLine(ed, line, &len);
		return std::string(s ? s : "", (size_t)len);
	}

	bool is_word_byte(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

	std::string trim_right(std::string s)
	{
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
			s.pop_back();
		return s;
	}

	bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
	bool ends_with(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

	// A segment (a piece of a line) which already carries the marks
	bool wrapped(const std::string& s, const std::string& left, const std::string& right)
	{
		return s.size() >= left.size() + right.size() + 1 && starts_with(s, left) && ends_with(s, right);
	}

	// Leading and trailing spaces of a segment stay outside the marks
	void split_blanks(const std::string& s, std::string& before, std::string& core, std::string& after)
	{
		size_t a = 0, b = s.size();
		while (a < b && (s[a] == ' ' || s[a] == '\t'))
			a++;
		while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
			b--;
		before = s.substr(0, a);
		core = s.substr(a, b - a);
		after = s.substr(b);
	}

	// The marks of a line of a list or of a quote at its start: [indent][marker][space]
	struct Marker
	{
		enum Kind { None, Heading, Bullet, Task, Numbered, Quote } kind = None;
		int level = 0;					// heading
		size_t length = 0;				// bytes of indent + marker + space
		std::string indent;
	};

	Marker marker_of(const std::string& line)
	{
		Marker m;
		size_t i = 0;
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
			i++;
		m.indent = line.substr(0, i);
		size_t j = i;
		if (j < line.size() && line[j] == '#')
		{
			int n = 0;
			while (j < line.size() && line[j] == '#')
			{
				n++;
				j++;
			}
			if (n <= 6 && j < line.size() && line[j] == ' ')
			{
				m.kind = Marker::Heading;
				m.level = n;
				m.length = j + 1;
				return m;
			}
			return m;
		}
		if (j < line.size() && line[j] == '>')
		{
			m.kind = Marker::Quote;
			m.length = j + 1 + (j + 1 < line.size() && line[j + 1] == ' ' ? 1 : 0);
			return m;
		}
		if (j + 1 < line.size() && (line[j] == '-' || line[j] == '*' || line[j] == '+') && line[j + 1] == ' ')
		{
			m.kind = Marker::Bullet;
			m.length = j + 2;
			if (starts_with(line.substr(m.length), "[ ] ") || starts_with(line.substr(m.length), "[x] ") || starts_with(line.substr(m.length), "[X] "))
			{
				m.kind = Marker::Task;
				m.length += 4;
			}
			return m;
		}
		size_t d = j;
		while (d < line.size() && std::isdigit((unsigned char)line[d]))
			d++;
		if (d > j && d + 1 < line.size() && (line[d] == '.' || line[d] == ')') && line[d + 1] == ' ')
		{
			m.kind = Marker::Numbered;
			m.length = d + 2;
			return m;
		}
		return m;
	}
}

// The editor which the buttons act on: the work, or the original when it is being edited in the reading mode. NULL when none can be edited.
ImGuiProseEditor* App::format_editor() const
{
	if (!_has_project)
		return nullptr;
	if (reading())
		return _edit_source ? _src.editor : nullptr;
	return _dst.editor;
}

// Replaces text, and selects (or places the caret at) a range of the new text
static void replace_and_select(ImGuiProseEditor* ed, ImGuiProsePos a, ImGuiProsePos b, const std::string& text, ImGuiProsePos sel_a, ImGuiProsePos sel_b)
{
	ImGuiProse::Replace(ed, a, b, text.c_str());
	if (sel_a.Line == sel_b.Line && sel_a.Column == sel_b.Column)
		ImGuiProse::SetCaret(ed, sel_a, false);
	else
		ImGuiProse::SetSelection(ed, sel_a, sel_b);
}

// Marks around the selection (or the word at the caret): bold, italic, strikethrough, code. Again on the same text removes the marks.
void App::md_wrap(const char* left_c, const char* right_c)
{
	ImGuiProseEditor* ed = format_editor();
	if (!ed)
		return;
	const std::string left = left_c, right = right_c;
	ImGuiProsePos a, b;
	if (!ImGuiProse::GetSelection(ed, &a, &b))
	{
		// No selection: the word at the caret, or an empty pair with the caret between the marks
		const ImGuiProsePos c = ImGuiProse::GetCaret(ed);
		const std::string line = line_of(ed, c.Line);
		int s = std::min(c.Column, (int)line.size()), e = s;
		while (s > 0 && is_word_byte((unsigned char)line[s - 1]))
			s--;
		while (e < (int)line.size() && is_word_byte((unsigned char)line[e]))
			e++;
		if (s == e)
		{
			ImGuiProse::Replace(ed, c, c, (left + right).c_str());
			ImGuiProse::SetCaret(ed, ImGuiProsePos(c.Line, c.Column + (int)left.size()), false);
			return;
		}
		a = ImGuiProsePos(c.Line, s);
		b = ImGuiProsePos(c.Line, e);
	}
	// The segments of the selection, one per line: the first from its column, the last up to its column
	std::vector<std::string> segments;
	for (int l = a.Line; l <= b.Line; l++)
	{
		const std::string line = line_of(ed, l);
		const int from = l == a.Line ? std::min(a.Column, (int)line.size()) : 0;
		const int to = l == b.Line ? std::min(b.Column, (int)line.size()) : (int)line.size();
		segments.push_back(line.substr((size_t)from, (size_t)std::max(0, to - from)));
	}
	// Already marked (inside the selection, or just around it): the marks are taken off
	bool all_wrapped = true, any = false;
	std::vector<std::string> core(segments.size()), before(segments.size()), after(segments.size());
	for (size_t i = 0; i < segments.size(); i++)
	{
		split_blanks(segments[i], before[i], core[i], after[i]);
		if (core[i].empty())
			continue;
		any = true;
		if (!wrapped(core[i], left, right))
			all_wrapped = false;
	}
	if (!any)
		return;
	if (a.Line == b.Line && !wrapped(core[0], left, right))
	{
		// The marks may be just outside the selection
		const std::string line = line_of(ed, a.Line);
		const int ls = a.Column - (int)left.size(), re = b.Column + (int)right.size();
		if (ls >= 0 && re <= (int)line.size() && line.compare((size_t)ls, left.size(), left) == 0 && line.compare((size_t)b.Column, right.size(), right) == 0)
		{
			const std::string inner = line.substr((size_t)a.Column, (size_t)(b.Column - a.Column));
			ImGuiProse::Replace(ed, ImGuiProsePos(a.Line, ls), ImGuiProsePos(a.Line, re), inner.c_str());
			ImGuiProse::SetSelection(ed, ImGuiProsePos(a.Line, ls), ImGuiProsePos(a.Line, ls + (b.Column - a.Column)));
			return;
		}
	}
	std::string out;
	for (size_t i = 0; i < segments.size(); i++)
	{
		if (i)
			out += "\n";
		if (core[i].empty())
		{
			out += segments[i];
			continue;
		}
		if (all_wrapped)
			out += before[i] + core[i].substr(left.size(), core[i].size() - left.size() - right.size()) + after[i];
		else if (wrapped(core[i], left, right))
			out += segments[i];
		else
			out += before[i] + left + core[i] + right + after[i];
	}
	// The result stays selected
	std::vector<std::string> lines;
	size_t pos = 0;
	for (;;)
	{
		const size_t nl = out.find('\n', pos);
		lines.push_back(out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
		if (nl == std::string::npos)
			break;
		pos = nl + 1;
	}
	const ImGuiProsePos new_end(a.Line + (int)lines.size() - 1, lines.size() == 1 ? a.Column + (int)lines[0].size() : (int)lines.back().size());
	ImGuiProse::Replace(ed, a, b, out.c_str());
	ImGuiProse::SetSelection(ed, a, new_end);
}

// Lines: the selected ones, or the one of the caret (a selection which ends at the start of a line does not take that line)
static void line_range(ImGuiProseEditor* ed, int* first, int* last)
{
	ImGuiProsePos a, b;
	if (ImGuiProse::GetSelection(ed, &a, &b))
	{
		*first = a.Line;
		*last = (b.Column == 0 && b.Line > a.Line) ? b.Line - 1 : b.Line;
	}
	else
		*first = *last = ImGuiProse::GetCaret(ed).Line;
}

// Heading (level 1 to 6; 0 = normal text), list, task list or quote on each line. The same action again takes the marker off.
void App::md_lines(int kind, int level)
{
	ImGuiProseEditor* ed = format_editor();
	if (!ed)
		return;
	int first = 0, last = 0;
	line_range(ed, &first, &last);
	std::vector<std::string> lines;
	for (int l = first; l <= last; l++)
		lines.push_back(line_of(ed, l));
	const Marker::Kind want = kind == 1 ? Marker::Heading : kind == 2 ? Marker::Bullet : kind == 3 ? Marker::Numbered : kind == 4 ? Marker::Quote : Marker::Task;
	// Is the marker already everywhere? Then it is taken off.
	bool all = true, any = false;
	for (const std::string& line : lines)
	{
		if (trim_right(line).empty())
			continue;
		any = true;
		const Marker m = marker_of(line);
		const bool same = m.kind == want && (want != Marker::Heading || m.level == level);
		if (!same)
			all = false;
	}
	if (!any)
	{
		// Empty line: the marker alone, the caret after it
		if (kind == 1 && level == 0)
			return;
		const std::string mark = kind == 1 ? std::string((size_t)level, '#') + " " : kind == 2 ? "- " : kind == 3 ? "1. " : kind == 4 ? "> " : "- [ ] ";
		const ImGuiProsePos c(first, 0);
		ImGuiProse::Replace(ed, c, ImGuiProsePos(first, (int)lines[0].size()), (lines[0] + mark).c_str());
		ImGuiProse::SetCaret(ed, ImGuiProsePos(first, (int)(lines[0].size() + mark.size())), false);
		return;
	}
	std::string out;
	int number = 1;
	for (size_t i = 0; i < lines.size(); i++)
	{
		if (i)
			out += "\n";
		const std::string& line = lines[i];
		if (trim_right(line).empty())
		{
			out += line;
			continue;
		}
		const Marker m = marker_of(line);
		// What is left when the marker of the same family is removed (a heading, a list item and a quote are not mixed with each other)
		std::string rest = m.kind != Marker::None ? line.substr(m.length) : line.substr(m.indent.size());
		const std::string indent = m.indent;
		if (all && kind != 1)
		{
			out += indent + rest;
			continue;
		}
		if (all && kind == 1)
		{
			out += rest;
			continue;
		}
		if (kind == 1)
			out += level == 0 ? rest : std::string((size_t)level, '#') + " " + rest;
		else if (kind == 2)
			out += indent + "- " + rest;
		else if (kind == 3)
			out += indent + std::to_string(number++) + ". " + rest;
		else if (kind == 4)
			out += indent + "> " + rest;
		else
			out += indent + "- [ ] " + rest;
	}
	if (kind == 1 && level == 0)
	{
		// Normal text: every heading marker goes
		out.clear();
		for (size_t i = 0; i < lines.size(); i++)
		{
			if (i)
				out += "\n";
			const Marker m = marker_of(lines[i]);
			out += m.kind == Marker::Heading ? lines[i].substr(m.length) : lines[i];
		}
	}
	const ImGuiProsePos a(first, 0), b(last, (int)lines.back().size());
	int out_lines = 1;
	for (char c : out)
		if (c == '\n')
			out_lines++;
	const size_t last_nl = out.rfind('\n');
	const int last_len = (int)(last_nl == std::string::npos ? out.size() : out.size() - last_nl - 1);
	ImGuiProse::Replace(ed, a, b, out.c_str());
	if (first == last)
		ImGuiProse::SetCaret(ed, ImGuiProsePos(first, last_len), false);
	else
		ImGuiProse::SetSelection(ed, ImGuiProsePos(first, 0), ImGuiProsePos(first + out_lines - 1, last_len));
}

// A link: the selection becomes the text, the caret goes between the parentheses; an address on the selection becomes the target
void App::md_link()
{
	ImGuiProseEditor* ed = format_editor();
	if (!ed)
		return;
	ImGuiProsePos a, b;
	std::string text;
	if (ImGuiProse::GetSelection(ed, &a, &b) && a.Line == b.Line)
		text = line_of(ed, a.Line).substr((size_t)a.Column, (size_t)(b.Column - a.Column));
	else
		a = b = ImGuiProse::GetCaret(ed);
	const bool address = starts_with(text, "http://") || starts_with(text, "https://") || starts_with(text, "mailto:") || starts_with(text, "www.");
	if (address)
	{
		// [](address): the caret goes in the brackets for the text
		ImGuiProse::Replace(ed, a, b, ("[](" + text + ")").c_str());
		ImGuiProse::SetCaret(ed, ImGuiProsePos(a.Line, a.Column + 1), false);
	}
	else
	{
		ImGuiProse::Replace(ed, a, b, ("[" + text + "]()").c_str());
		ImGuiProse::SetCaret(ed, ImGuiProsePos(a.Line, a.Column + (int)text.size() + 3), false);
	}
}

// A fenced code block around the selected lines (or an empty one)
void App::md_code_block()
{
	ImGuiProseEditor* ed = format_editor();
	if (!ed)
		return;
	ImGuiProsePos s, e;
	if (!ImGuiProse::GetSelection(ed, &s, &e))
	{
		const ImGuiProsePos c = ImGuiProse::GetCaret(ed);
		const std::string line = line_of(ed, c.Line);
		const std::string text = (line.empty() ? "" : "\n") + std::string("```\n\n```");
		ImGuiProse::Replace(ed, ImGuiProsePos(c.Line, (int)line.size()), ImGuiProsePos(c.Line, (int)line.size()), text.c_str());
		ImGuiProse::SetCaret(ed, ImGuiProsePos(c.Line + (line.empty() ? 1 : 2), 0), false);
		return;
	}
	int first = 0, last = 0;
	line_range(ed, &first, &last);
	std::string body;
	for (int l = first; l <= last; l++)
		body += (l > first ? "\n" : "") + line_of(ed, l);
	ImGuiProse::Replace(ed, ImGuiProsePos(first, 0), ImGuiProsePos(last, (int)line_of(ed, last).size()), ("```\n" + body + "\n```").c_str());
}

// A horizontal rule after the line of the caret
void App::md_rule()
{
	ImGuiProseEditor* ed = format_editor();
	if (!ed)
		return;
	const int line = ImGuiProse::GetCaret(ed).Line;
	const int len = (int)line_of(ed, line).size();
	ImGuiProse::Replace(ed, ImGuiProsePos(line, len), ImGuiProsePos(line, len), "\n\n---\n");
	ImGuiProse::SetCaret(ed, ImGuiProsePos(line + 3, 0), false);
}

// Width of the row of buttons (for the choice between the toolbar and a row of their own)
float App::format_buttons_width() const
{
	const float w = ImGui::GetFrameHeight() * 1.1f, spacing = ImGui::GetStyle().ItemSpacing.x;
	return 11.0f * w + 0.3f * w + 12.0f * spacing + 2.0f * (ImGui::CalcTextSize("|").x + 2.0f * spacing);
}

// The buttons, in the toolbar before the list of the headings, for the Markdown texts. False when they are not shown.
bool App::draw_format_buttons()
{
	if (!_has_project || !_s.format_bar || !_s.markdown)
		return false;
	ImGui::BeginDisabled(format_editor() == nullptr);
	const float w = ImGui::GetFrameHeight() * 1.1f;
	auto button = [&](const char* label, const char* tip_key, const char* shortcut) -> bool
	{
		const bool clicked = ImGui::Button(label, ImVec2(w, 0.0f));
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			const std::string tip = std::string(TR(tip_key)) + (shortcut && shortcut[0] ? std::string("  (") + shortcut + ")" : std::string());
			ImGui::SetTooltip("%s", tip.c_str());
		}
		return clicked;
	};
	if (button("B", "FMT_BOLD", "Ctrl+B")) md_wrap("**", "**");
	ImGui::SameLine();
	if (button("I", "FMT_ITALIC", "Ctrl+I")) md_wrap("*", "*");
	ImGui::SameLine();
	if (button("S", "FMT_STRIKE", "")) md_wrap("~~", "~~");
	ImGui::SameLine();
	if (button("`", "FMT_CODE", "")) md_wrap("`", "`");
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	if (ImGui::Button("H\xE2\x96\xBE", ImVec2(w * 1.3f, 0.0f)))
		ImGui::OpenPopup("##fmt_heading");
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s", TR("FMT_HEADING"));
	if (ImGui::BeginPopup("##fmt_heading"))
	{
		static const char* const keys[] = { "FMT_H0", "FMT_H1", "FMT_H2", "FMT_H3", "FMT_H4" };
		for (int level = 1; level <= 4; level++)
			if (ImGui::MenuItem(TR(keys[level])))
				md_lines(1, level);
		ImGui::Separator();
		if (ImGui::MenuItem(TR(keys[0])))
			md_lines(1, 0);
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if (button("\xE2\x80\xA2", "FMT_BULLET", "")) md_lines(2, 0);											// bullet
	ImGui::SameLine();
	if (button("1.", "FMT_NUMBERED", "")) md_lines(3, 0);
	ImGui::SameLine();
	if (button("\xE2\x98\x91", "FMT_TASK", "")) md_lines(5, 0);											// ballot box with check
	ImGui::SameLine();
	if (button(">", "FMT_QUOTE", "")) md_lines(4, 0);
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	if (button("\xE2\x86\x97", "FMT_LINK", "Ctrl+K")) md_link();											// north east arrow
	ImGui::SameLine();
	if (button("{ }", "FMT_CODEBLOCK", "")) md_code_block();
	ImGui::SameLine();
	if (button("\xE2\x80\x94", "FMT_RULE", "")) md_rule();													// em dash
	ImGui::EndDisabled();
	return true;
}
