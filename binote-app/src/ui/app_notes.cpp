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

// Binote - notes on paragraphs (binote.notes in the folder of the project: one line per note, doc <TAB> anchor <TAB> paragraph <TAB> text)

#include "ui/app.h"
#include "common/i18n.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// A note takes one line of the file: the line breaks and the backslashes are escaped
static std::string note_escape(const std::string& s)
{
	std::string o;
	for (char c : s)
	{
		if (c == '\\') o += "\\\\";
		else if (c == '\n') o += "\\n";
		else if (c == '\t') o += "\\t";
		else if (c != '\r') o += c;
	}
	return o;
}

// Reverse of note_escape
static std::string note_unescape(const std::string& s)
{
	std::string o;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] == '\\' && i + 1 < s.size())
		{
			const char e = s[++i];
			o += e == 'n' ? '\n' : (e == 't' ? '\t' : e);
		}
		else
			o += s[i];
	}
	return o;
}

// The start of a paragraph identifies it, even when other paragraphs are added before it
static std::string note_anchor(const std::string& paragraph)
{
	std::string a = paragraph.substr(0, std::min<size_t>(paragraph.size(), 40));
	while (!a.empty() && ((unsigned char)a.back() & 0xC0) == 0x80)
		a.pop_back();
	if (!a.empty() && ((unsigned char)a.back() & 0x80))
		a.pop_back();
	return a;
}

// File of the notes, next to the project
std::string App::notes_path() const
{
	return _project.meta_path("binote.notes");
}

// Reads the notes of the project (binote.notes)
void App::load_notes()
{
	_notes.clear();
	std::string text;
	if (!_has_project || !util::read_text(notes_path(), text))
		return;
	for (const std::string& line : util::split_lines(text))
	{
		std::vector<std::string> f;
		size_t pos = 0;
		for (int k = 0; k < 3; k++)
		{
			const size_t tab = line.find('\t', pos);
			if (tab == std::string::npos)
				break;
			f.push_back(line.substr(pos, tab - pos));
			pos = tab + 1;
		}
		if (f.size() == 3)
			_notes.push_back({ f[0], note_unescape(f[1]), std::atoi(f[2].c_str()), note_unescape(line.substr(pos)) });
	}
}

// Writes the notes of the project (binote.notes)
void App::save_notes()
{
	if (!_has_project)
		return;
	std::string text;
	for (const Note& n : _notes)
		text += n.doc + "\t" + note_escape(n.anchor) + "\t" + std::to_string(n.para) + "\t" + note_escape(n.text) + "\n";
	if (text.empty())
		std::remove(notes_path().c_str());
	else
		util::write_text(notes_path(), text);
}

// The paragraph (1-based) of the current document each note is about: found again by its anchor, else by its number
void App::locate_notes()
{
	_note_paras.clear();
	const Doc* d = current_doc();
	if (!d)
		return;
	const std::vector<std::string> paras = paragraphs(_dst.editor);
	for (size_t i = 0; i < _notes.size(); i++)
	{
		Note& n = _notes[i];
		if (n.doc != d->rel)
			continue;
		int best = -1;
		for (int p = 0; p < (int)paras.size(); p++)
			if (!n.anchor.empty() && paras[(size_t)p].compare(0, n.anchor.size(), n.anchor) == 0 && (best < 0 || std::abs(p + 1 - n.para) < std::abs(best + 1 - n.para)))
				best = p;
		if (best >= 0)
			n.para = best + 1;
		if (n.para >= 1 && n.para <= (int)paras.size())
			_note_paras.push_back({ n.para, (int)i });
	}
}

// Opens the note box for the paragraph of the caret
void App::add_note_here()
{
	const Doc* d = current_doc();
	const int para = ImGuiProse::GetParagraphOfLine(_dst.editor, ImGuiProse::GetCaret(_dst.editor).Line);
	if (!d || para < 1)
		return;
	_note_edit = -1;
	for (size_t i = 0; i < _notes.size(); i++)
		if (_notes[i].doc == d->rel && _notes[i].para == para)
			_note_edit = (int)i;
	_note_target_para = para;
	snprintf(_note_text, sizeof(_note_text), "%s", _note_edit >= 0 ? _notes[(size_t)_note_edit].text.c_str() : "");
	_open_note = true;
}

// Notes window (list of the notes, with the passage they are about) and the box to edit one
void App::draw_notes()
{
	if (_open_note)
	{
		_open_note = false;
		ImGui::OpenPopup("###note_edit");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
	const std::string title = std::string(TR("NOTE_TITLE")) + "###note_edit";
	if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings))
	{
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::InputTextMultiline("##note_text", _note_text, sizeof(_note_text), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 6.0f));
		if (ImGui::Button(TR("BTN_OK")))
		{
			const Doc* d = current_doc();
			if (d)
			{
				const std::vector<std::string> paras = paragraphs(_dst.editor);
				const std::string text = _note_text;
				if (_note_edit >= 0)
				{
					if (text.empty())
						_notes.erase(_notes.begin() + _note_edit);
					else
						_notes[(size_t)_note_edit].text = text;
				}
				else if (!text.empty() && _note_target_para >= 1 && _note_target_para <= (int)paras.size())
					_notes.push_back({ d->rel, note_anchor(paras[(size_t)_note_target_para - 1]), _note_target_para, text });
				save_notes();
				locate_notes();
				rebuild_spans();
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("BTN_CANCEL")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		if (_note_edit >= 0)
		{
			ImGui::SameLine();
			if (ImGui::Button(TR("NOTE_DELETE")))
			{
				_notes.erase(_notes.begin() + _note_edit);
				save_notes();
				locate_notes();
				rebuild_spans();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}

	if (!_show_notes)
		return;
	ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("NOTES_TITLE"), &_show_notes))
	{
		ImGui::End();
		return;
	}
	ImGui::TextDisabled("%s", i18n::fmt(TR("NOTES_COUNT"), (int)_notes.size()).c_str());
	ImGui::BeginChild("##notes_list", ImVec2(0, 0), true);
	for (size_t i = 0; i < _notes.size(); i++)
	{
		const Note& n = _notes[i];
		std::string text = n.text;
		std::replace(text.begin(), text.end(), '\n', ' ');
		if (text.size() > 140)
			text.resize(140);
		const std::string label = n.doc + "  \xC2\xB6" + std::to_string(n.para) + "   " + text + "##note" + std::to_string(i);
		if (ImGui::Selectable(label.c_str()))
		{
			for (int d = 0; d < (int)_project.docs.size(); d++)
				if (_project.docs[d].rel == n.doc)
				{
					if (d != _cur)
						request_open_doc(d, 0);
					break;
				}
			if (current_doc() && current_doc()->rel == n.doc)
			{
				_active = &_dst;
				go_to(1, n.para);
			}
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

//-----------------------------------------------------------------------------
// [SECTION] Alignment pins (binote.align: doc <TAB> source anchor <TAB> source paragraph <TAB> destination anchor <TAB> destination paragraph)
//-----------------------------------------------------------------------------

// File of the alignment pins, next to the project
std::string App::pins_path() const
{
	return _project.meta_path("binote.align");
}

// Reads the alignment pins of the project (binote.align)
void App::load_pins()
{
	_pins.clear();
	std::string text;
	if (!_has_project || !util::read_text(pins_path(), text))
		return;
	for (const std::string& line : util::split_lines(text))
	{
		std::vector<std::string> f;
		size_t pos = 0;
		for (;;)
		{
			const size_t tab = line.find('\t', pos);
			if (tab == std::string::npos)
				break;
			f.push_back(line.substr(pos, tab - pos));
			pos = tab + 1;
		}
		f.push_back(line.substr(pos));
		if (f.size() == 5)
			_pins.push_back({ f[0], note_unescape(f[1]), note_unescape(f[3]), std::atoi(f[2].c_str()), std::atoi(f[4].c_str()) });
	}
}

// Writes the alignment pins of the project (binote.align)
void App::save_pins()
{
	if (!_has_project)
		return;
	std::string text;
	for (const Pin& p : _pins)
		text += p.doc + "\t" + note_escape(p.src_anchor) + "\t" + std::to_string(p.src_para) + "\t" + note_escape(p.dst_anchor) + "\t" + std::to_string(p.dst_para) + "\n";
	if (text.empty())
		std::remove(pins_path().c_str());
	else
		util::write_text(pins_path(), text);
}

// Paragraph (0-based) which starts with the anchor, the nearest to the stored number (1-based), else the stored number
static int locate_para(const std::vector<std::string>& paras, const std::string& anchor, int stored)
{
	int best = -1;
	for (int p = 0; p < (int)paras.size(); p++)
		if (!anchor.empty() && paras[(size_t)p].compare(0, anchor.size(), anchor) == 0 && (best < 0 || std::abs(p + 1 - stored) < std::abs(best + 1 - stored)))
			best = p;
	if (best < 0 && stored >= 1 && stored <= (int)paras.size())
		best = stored - 1;
	return best;
}

// Pins of the current document as (source paragraph, destination paragraph), found again by the text of the paragraphs
std::vector<std::pair<int, int>> App::locate_pins(const std::vector<std::string>& src, const std::vector<std::string>& dst)
{
	std::vector<std::pair<int, int>> out;
	const Doc* d = current_doc();
	if (!d)
		return out;
	for (const Pin& p : _pins)
	{
		if (p.doc != d->rel)
			continue;
		const int a = locate_para(src, p.src_anchor, p.src_para), b = locate_para(dst, p.dst_anchor, p.dst_para);
		if (a >= 0 && b >= 0)
			out.push_back({ a, b });
	}
	std::sort(out.begin(), out.end());
	return out;
}

// The paragraph under the caret of the active pane faces a paragraph of the other pane, asked in a dialog (proposed: the one the comparison found)
void App::pin_here()
{
	const Pane* pane = _active == &_src ? &_src : &_dst;
	const int para = ImGuiProse::GetParagraphOfLine(pane->editor, ImGuiProse::GetCaret(pane->editor).Line);
	if (!current_doc() || para < 1)
		return;
	_pin_from_src = pane == &_src;
	_pin_para = para;
	_pin_value = (_pin_from_src ? diff::map_src_to_dst(_align, para - 1) : diff::map_dst_to_src(_align, para - 1)) + 1;
	_open_pin = true;
}

// Declares that source paragraph a faces destination paragraph b (replaces the pins which use either)
void App::apply_pin(int a, int b)
{
	const Doc* d = current_doc();
	const std::vector<std::string> src = paragraphs(_src.editor), dst = paragraphs(_dst.editor);
	if (!d || a < 1 || b < 1 || a > (int)src.size() || b > (int)dst.size())
		return;
	for (size_t i = _pins.size(); i-- > 0;)
		if (_pins[i].doc == d->rel && (locate_para(src, _pins[i].src_anchor, _pins[i].src_para) == a - 1 || locate_para(dst, _pins[i].dst_anchor, _pins[i].dst_para) == b - 1))
			_pins.erase(_pins.begin() + (long)i);
	_pins.push_back({ d->rel, note_anchor(src[(size_t)a - 1]), note_anchor(dst[(size_t)b - 1]), a, b });
	save_pins();
	_diff_due = ImGui::GetTime();
	status(i18n::fmt(TR("MSG_PINNED"), a, b));
}

// Removes the pins of the current document
void App::clear_pins()
{
	const Doc* d = current_doc();
	if (!d)
		return;
	for (size_t i = _pins.size(); i-- > 0;)
		if (_pins[i].doc == d->rel)
			_pins.erase(_pins.begin() + (long)i);
	save_pins();
	_diff_due = ImGui::GetTime();
	status(TR("MSG_PINS_CLEARED"));
}
