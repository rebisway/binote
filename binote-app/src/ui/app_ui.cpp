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

// Binote - the application: interface

#include "ui/app.h"
#include "common/version.h"
#include "common/i18n.h"
#include <imgui_internal.h>
#include "common/dialog.h"
#include "common/logger.h"
#include <filesystem>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <map>
#include "stb/stb_image.h"

static const char* ACCENTS[] = { "ACCENT_YELLOW", "ACCENT_GREEN", "ACCENT_BLUE", "ACCENT_PINK", "ACCENT_ORANGE", "ACCENT_CUSTOM" };
// light background, light text, dark background, dark text
static const ImU32 ACCENT_COLORS[5][4] = {
	{ IM_COL32(255, 243, 176, 255), IM_COL32(122, 92, 0, 255), IM_COL32(74, 64, 18, 255), IM_COL32(255, 216, 77, 255) },
	{ IM_COL32(214, 245, 214, 255), IM_COL32(27, 107, 27, 255), IM_COL32(31, 74, 37, 255), IM_COL32(126, 224, 138, 255) },
	{ IM_COL32(214, 232, 255, 255), IM_COL32(11, 79, 158, 255), IM_COL32(27, 58, 94, 255), IM_COL32(125, 184, 255, 255) },
	{ IM_COL32(255, 220, 235, 255), IM_COL32(163, 25, 91, 255), IM_COL32(90, 32, 56, 255), IM_COL32(255, 143, 192, 255) },
	{ IM_COL32(255, 226, 194, 255), IM_COL32(160, 74, 0, 255), IM_COL32(90, 52, 16, 255), IM_COL32(255, 176, 102, 255) },
};

// One of the four colours of the accent (see ACCENT_COLORS) of the settings: the ones which are provided, or the ones the user chose
static ImU32 accent_color(const util::Settings& s, int index, int which)
{
	return s.accent >= 5 ? (ImU32)s.accent_custom[which] : ACCENT_COLORS[index][which];
}

static bool toggle_button(const char* label, bool* value, const char* tooltip = nullptr)
{
	const bool on = *value;
	if (on)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}
	const bool clicked = ImGui::Button(label);
	if (on)
		ImGui::PopStyleColor();
	if (clicked)
		*value = !*value;
	if (tooltip && ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", tooltip);
	return clicked;
}

//-----------------------------------------------------------------------------
// Frame
//-----------------------------------------------------------------------------

// The paragraph of a pane which faces the current one: the same number in the active pane, the aligned one in the other
int App::highlight_para(const Pane* pane) const
{
	if (_current_para < 1 || pane == _active)
		return _current_para;
	return (pane == &_dst ? diff::map_src_to_dst(_align, _current_para - 1) : diff::map_dst_to_src(_align, _current_para - 1)) + 1;
}

// One frame of the whole interface: menu, toolbar, body (tree and panes), status bar, then the floating windows.
// The analyses (differences, outline, statistics) are redone here once the text has been quiet for a moment.
void App::frame()
{
	const double now = ImGui::GetTime();
	if (!_theme_set || _applied_dark != _s.dark)
	{
		const bool first = !_theme_set;
		apply_theme();
		if (!first)
			rebuild_spans();								// the colours of the differences depend on the theme
	}
	handle_shortcuts();
	open_pending_link();
	typography::set_quote_style(_s.quote_style);
	typography::set_language(_has_project ? _project.language : "fr");
	draw_menu();
	draw_script();
	draw_script_editor();

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin(TR("##root"), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::PopStyleVar();
	draw_toolbar();
	draw_body();
	draw_status();
	ImGui::End();
	draw_dialogs();
	if (_show_find) draw_find();
	if (_show_charmap) draw_charmap();
	draw_export();
	if (_show_problems) draw_problems();
	draw_about();
	if (_show_names) draw_names();
	draw_notes();
	if (_show_vcs) draw_vcs();
	if (_show_history) draw_history();
	if (_show_toc) draw_toc();
	if (_show_stats) draw_stats();
	{
		static bool style_was_open = false;
		if (_show_style)
			draw_style();
		if (style_was_open && !_show_style)
			rebuild_spans();
		style_was_open = _show_style;
	}

	// Analysis a moment after the last change
	const int sv = ImGuiProse::GetVersion(_src.editor), dv = ImGuiProse::GetVersion(_dst.editor);
	if (sv != _seen_src_version || dv != _seen_dst_version)
	{
		_seen_src_version = sv;
		_seen_dst_version = dv;
		_diff_due = now + 0.35;
		_outline_due = now + 0.35;
		_stats_due = now + 0.35;
	}
	if (_diff_due >= 0.0 && now >= _diff_due)
	{
		_diff_due = -1.0;
		const std::vector<std::string> src_paras = paragraphs(_src.editor), dst_paras = paragraphs(_dst.editor);
		_align = diff::compute(src_paras, dst_paras, locate_pins(src_paras, dst_paras));
		_diff = _s.show_diff ? _align : diff::Result();
		// Paragraphs were added or removed: the destination goes back to the paragraph which faces the one at the top of the source
			_sync_settle = 3;
		if (_s.sync_scroll && !_src.preview && !_dst.preview)
		{
			ImGuiProseScrollAnchor anchor = ImGuiProse::GetScrollAnchor(_src.editor);
			if (anchor.Paragraph > 0)
				anchor.Paragraph = diff::map_src_to_dst(_align, anchor.Paragraph - 1) + 1;
			ImGuiProse::SetScrollAnchor(_dst.editor, anchor);
		}
		if (_show_find)
			update_matches();
		if (_show_style && _style_done)
			run_style();
		rebuild_spans();
	}
	if (_stats_due >= 0.0 && now >= _stats_due)
		update_stats();
	if (_outline_due >= 0.0 && now >= _outline_due)
		refresh_headings();
	if (_s.auto_snapshot && _has_project && dirty() && now > _snapshot_due)
	{
		History::add(current_doc()->dst_path, editor_text(_dst.editor), "auto");
		_snapshot_due = now + 90.0;
	}

	// Window title
	std::string title = "Binote";
	if (_has_project)
		title += " - " + _project.name + (_project.is_folder && current_doc() ? " / " + current_doc()->rel : "") + (dirty() ? " *" : "");
	if (title != _title)
	{
		_title = title;
		glfwSetWindowTitle(_window, title.c_str());
	}
}

//-----------------------------------------------------------------------------
// Shortcuts
//-----------------------------------------------------------------------------

// Keyboard shortcuts, read once per frame (the editors keep the editing keys)
void App::handle_shortcuts()
{
	ImGuiIO& io = ImGui::GetIO();
	if (ImGui::GetTopMostPopupModal() != nullptr)
		return;
	const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
	auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };

	if (ctrl && alt && pressed(ImGuiKey_A) && _has_project)
		pin_here();
	if (ctrl && alt && pressed(ImGuiKey_N))
		add_note_here();
	// Ctrl+F7 spelling, Ctrl+F8 grammar, Ctrl+F9 list of the problems (the keys alone are the typography and the differences)
	if (ctrl && !alt && !shift && pressed(ImGuiKey_F7))
		toggle_spell();
	if (ctrl && !alt && !shift && pressed(ImGuiKey_F8))
		toggle_grammar();
	if (ctrl && !alt && !shift && pressed(ImGuiKey_F9))
		_show_problems = !_show_problems;
	if (ctrl && !alt)
	{
		if (pressed(ImGuiKey_O)) { std::string p; if (shift) { if (dialog::pick_folder_dialog("", p)) open_folder_path(p); } else if (dialog::open_file_dialog("", p, "Texte / Markdown", "*.md;*.markdown;*.txt")) open_file_path(p); }
		else if (pressed(ImGuiKey_S)) save();
		else if (pressed(ImGuiKey_G)) { _open_goto = true; _goto_value = 1; }
		else if (pressed(ImGuiKey_F)) { _show_find = true; _find_focus = true; _find_all_chapters = shift; }
		else if (pressed(ImGuiKey_L)) toggle(_s.show_log);
		else if (pressed(ImGuiKey_H)) { if (Doc* d = current_doc()) { _snapshots = History::list(d->dst_path); _snapshot_selected = -1; _show_history = true; } }
		else if (pressed(ImGuiKey_R)) set_reading(!reading());
		else if (pressed(ImGuiKey_T)) toggle(_s.dark);
		else if (pressed(ImGuiKey_DownArrow) && !shift) move_paragraph(1);
		else if (pressed(ImGuiKey_UpArrow) && !shift) move_paragraph(-1);
		else if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd) || pressed(ImGuiKey_RightBracket)) { _s.font_size = std::min(40.0f, _s.font_size + 1.0f); _s.save(); }
		else if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract) || pressed(ImGuiKey_6)) { _s.font_size = std::max(10.0f, _s.font_size - 1.0f); _s.save(); }
		else if (pressed(ImGuiKey_Space) && shift) insert_text(typography::NBSP);
			else if (pressed(ImGuiKey_U) && shift) _show_charmap = true;
		if (io.MouseWheel != 0.0f)
		{
			_s.font_size = std::max(10.0f, std::min(40.0f, _s.font_size + (io.MouseWheel > 0 ? 1.0f : -1.0f)));
			_s.save();
		}
	}
	if (ctrl && alt && pressed(ImGuiKey_Space))
		insert_text(typography::NNBSP);
	if (!ctrl && alt && pressed(ImGuiKey_Z))
		toggle(_s.wrap);
	if (pressed(ImGuiKey_F3)) find_next(shift ? -1 : 1);
	if (!ctrl && pressed(ImGuiKey_F7)) fix_typography(shift);
	if (!ctrl && pressed(ImGuiKey_F8)) jump_diff(shift ? -1 : 1);
	if (pressed(ImGuiKey_F9)) toggle(_s.show_tree);
}

//-----------------------------------------------------------------------------
// Menu and toolbar
//-----------------------------------------------------------------------------

// Main menu bar
void App::draw_menu()
{
	if (!ImGui::BeginMainMenuBar())
		return;
	std::string path;
	if (ImGui::BeginMenu(TR("MENU_FILE")))
	{
		if (ImGui::MenuItem(TR("MENU_OPEN_FILE"), "Ctrl+O") && dialog::open_file_dialog("", path, "Texte / Markdown", "*.md;*.markdown;*.txt"))
			open_file_path(path);
		if (ImGui::MenuItem(TR("MENU_OPEN_FOLDER"), "Ctrl+Shift+O") && dialog::pick_folder_dialog("", path))
			open_folder_path(path);
		if (ImGui::BeginMenu(TR("MENU_RECENT"), !_s.recents.empty()))
		{
			for (size_t i = 0; i < _s.recents.size(); i++)
			{
				const util::Recent r = _s.recents[i];
				const bool dir = util::is_directory(r.source);
				const std::string label = std::string(dir ? "[dossier] " : "") + util::file_name(r.source) + "   ->   " + util::file_name(r.destination) + "##recent" + std::to_string(i);
				if (ImGui::MenuItem(label.c_str()))
				{
					if (dir) open_folder_path(r.source);
					else open_file_path(r.source, r.destination);
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", (i18n::fmt(TR("TIP_SOURCE"), r.source.c_str()) + "\n" + i18n::fmt(TR("TIP_DESTINATION"), r.destination.c_str())).c_str());
			}
			ImGui::Separator();
			if (ImGui::MenuItem(TR("MENU_CLEAR_RECENT")))
			{
				_s.recents.clear();
				_s.save();
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem(TR("MENU_SAVE"), "Ctrl+S", false, _has_project))
			save();
		if (ImGui::MenuItem(TR("MENU_VERSIONS"), nullptr, _show_vcs, _has_project))
			_show_vcs = !_show_vcs;
		if (ImGui::MenuItem(TR("MENU_EXPORT"), nullptr, false, _has_project))
			_open_export = true;
		if (ImGui::MenuItem(TR("MENU_SAVE_AS"), nullptr, false, _has_project && !_project.is_folder) && dialog::save_file_dialog("", path, "Texte / Markdown", "*.md;*.markdown;*.txt"))
			save_as(path);
		if (ImGui::MenuItem(TR("MENU_MERGE_FILES"), nullptr, false, _has_project && _project.is_folder && _project.docs.size() > 1) && dialog::save_file_dialog("", path, "Texte / Markdown", "*.md;*.markdown;*.txt"))
			merge_files(path);
		if (ImGui::MenuItem(TR("MENU_RESET_FROM_SOURCE"), nullptr, false, _has_project))
			reset_dest_from_source();
		ImGui::Separator();
		if (ImGui::MenuItem(TR("MENU_PREFERENCES")))
			_open_prefs = true;
		if (ImGui::MenuItem(TR("MENU_DEST_FOLDER"), nullptr, false, _has_project))
		{
			_dest_folder.clear();
			_dest_input = _project.dest_root;
			_open_dest = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem(TR("MENU_QUIT")))
			glfwSetWindowShouldClose(_window, GLFW_TRUE);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(TR("MENU_EDIT")))
	{
		if (ImGui::MenuItem(TR("MENU_UNDO"), "Ctrl+Z"))
			ImGuiProse::Undo(_dst.editor);
		if (ImGui::MenuItem(TR("MENU_REDO"), "Ctrl+Y"))
			ImGuiProse::Redo(_dst.editor);
		ImGui::Separator();
		if (ImGui::MenuItem(TR("MENU_FIND"), "Ctrl+F")) { _show_find = true; _find_focus = true; }
		if (ImGui::MenuItem(TR("MENU_FIND_NEXT"), "F3")) find_next(1);
		if (ImGui::MenuItem(TR("MENU_FIND_ALL"), "Ctrl+Shift+F")) { _show_find = true; _find_focus = true; _find_all_chapters = true; }
		if (ImGui::MenuItem(TR("MENU_GOTO"), "Ctrl+G")) { _open_goto = true; _goto_value = 1; }
		if (ImGui::MenuItem(TR("MENU_NEXT_PARAGRAPH"), "Ctrl+Down")) move_paragraph(1);
		if (ImGui::MenuItem(TR("MENU_PREV_PARAGRAPH"), "Ctrl+Up")) move_paragraph(-1);
		if (ImGui::MenuItem(TR("MENU_NEXT_DIFF"), "F8")) jump_diff(1);
		if (ImGui::MenuItem(TR("MENU_PREV_DIFF"), "Shift+F8")) jump_diff(-1);
		ImGui::Separator();
		if (ImGui::MenuItem(TR("MENU_CHARMAP"), "Ctrl+Shift+U")) _show_charmap = true;
		if (ImGui::MenuItem(TR("MENU_PIN_HERE"), "Ctrl+Alt+A", false, _has_project)) pin_here();
		if (ImGui::MenuItem(TR("MENU_PIN_CLEAR"), nullptr, false, _has_project)) clear_pins();
		if (ImGui::MenuItem(TR("MENU_NOTE_HERE"), "Ctrl+Alt+N", false, _has_project)) add_note_here();
		if (ImGui::MenuItem(TR("MENU_NOTES"), nullptr, _show_notes, _has_project)) _show_notes = !_show_notes;
		if (ImGui::MenuItem(TR("MENU_INSERT_NBSP"), "Ctrl+Shift+Space")) insert_text(typography::NBSP);
		if (ImGui::MenuItem(TR("MENU_INSERT_NNBSP"), "Ctrl+Alt+Space")) insert_text(typography::NNBSP);
		if (ImGui::MenuItem(TR("MENU_INSERT_QUOTES")))
		{
			ImGuiProsePos a, b;
			std::string inner;
			if (ImGuiProse::GetSelection(_dst.editor, &a, &b) && a.Line == b.Line)
			{
				int len = 0;
				const char* s = ImGuiProse::GetLine(_dst.editor, a.Line, &len);
				inner.assign(s + a.Column, b.Column - a.Column);
			}
			insert_text(std::string("\xC2\xAB") + typography::NNBSP + inner + typography::NNBSP + "\xC2\xBB");
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(TR("MENU_VIEW")))
	{
		if (ImGui::MenuItem(TR("MENU_WORD_WRAP"), "Alt+Z", _s.wrap)) toggle(_s.wrap);
		if (ImGui::MenuItem(TR("MENU_LINE_NUMBERS"), nullptr, _s.line_numbers)) toggle(_s.line_numbers);
		if (ImGui::MenuItem(TR("MENU_PARAGRAPH_NUMBERS"), nullptr, _s.paragraph_numbers)) toggle(_s.paragraph_numbers);
		if (ImGui::MenuItem(TR("MENU_MARK_DIFF"), nullptr, _s.show_diff)) { toggle(_s.show_diff); _diff_due = ImGui::GetTime(); }
		if (ImGui::MenuItem(TR("MENU_SYNC"), nullptr, _s.sync_scroll)) toggle(_s.sync_scroll);
		if (ImGui::MenuItem(TR("MENU_ALIGN"), nullptr, _s.align_paragraphs)) toggle(_s.align_paragraphs);
		if (ImGui::MenuItem(TR("MENU_SHOW_NBSP"), nullptr, _s.show_nbsp)) toggle(_s.show_nbsp);
		if (ImGui::MenuItem(TR("MENU_SHOW_INVISIBLE"), nullptr, _s.show_invisible)) toggle(_s.show_invisible);
		ImGui::Separator();
		if (ImGui::MenuItem(TR("MENU_PROJECT_TREE"), "F9", _s.show_tree)) toggle(_s.show_tree);
		if (ImGui::MenuItem(TR("MENU_READING_MODE"), "Ctrl+R", reading())) set_reading(!reading());
		if (ImGui::MenuItem(TR("MENU_DEST_ONLY"), nullptr, _s.dest_only && !reading())) set_dest_only(!_s.dest_only);
		if (ImGui::MenuItem(TR("MENU_LOG"), "Ctrl+L", _s.show_log)) toggle(_s.show_log);
		if (ImGui::MenuItem(TR("MENU_OPEN_LOG_FOLDER")))
			dialog::open_folder(util::parent_path(Logger::path()));
		if (ImGui::MenuItem(TR("MENU_DARK"), "Ctrl+T", _s.dark)) toggle(_s.dark);
		if (ImGui::MenuItem(TR("MENU_MONO_FONT"), nullptr, _s.mono_editor)) toggle(_s.mono_editor);
		ImGui::Separator();
		if (ImGui::BeginMenu(TR("MENU_HIGHLIGHT")))
		{
			static const char* modes[] = { "HIGHLIGHT_NONE", "HIGHLIGHT_BACKGROUND", "HIGHLIGHT_TEXT", "HIGHLIGHT_BOTH" };
			for (int i = 0; i < 4; i++)
				if (ImGui::MenuItem(TR(modes[i]), nullptr, _s.highlight_mode == i)) { _s.highlight_mode = i; _s.save(); }
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(TR("MENU_HIGHLIGHT_COLOR")))
		{
			for (int i = 0; i < 6; i++)
			{
				// The colour, then its name
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float h = ImGui::GetTextLineHeight();
				const util::Settings preview = [&] { util::Settings t = _s; t.accent = i; return t; }();
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + h * 1.6f, p.y + h), accent_color(preview, i, _s.dark ? 2 : 0) | IM_COL32(0, 0, 0, 255), 2.0f);
				ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + h * 1.6f, p.y + h), accent_color(preview, i, _s.dark ? 3 : 1), 2.0f);
				ImGui::Dummy(ImVec2(h * 1.6f, h));
				ImGui::SameLine();
				if (ImGui::MenuItem(TR(ACCENTS[i]), nullptr, _s.accent == i)) { _s.accent = i; _s.save(); }
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(TR("MENU_LANGUAGE")))
		{
			for (const auto& lang : i18n::languages())
				if (ImGui::MenuItem(lang.second.c_str(), nullptr, i18n::current() == lang.first)) { i18n::load(lang.first); _s.language = lang.first; _s.save(); }
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem(TR("MENU_ZOOM_IN"), "Ctrl++")) { _s.font_size = std::min(40.0f, _s.font_size + 1.0f); _s.save(); }
		if (ImGui::MenuItem(TR("MENU_ZOOM_OUT"), "Ctrl+-")) { _s.font_size = std::max(10.0f, _s.font_size - 1.0f); _s.save(); }
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(TR("MENU_MODE")))
	{
		if (ImGui::MenuItem(TR("MENU_MODE_TEXT"), nullptr, !_s.markdown)) { _s.markdown = false; refresh_headings(); }
		if (ImGui::MenuItem(TR("MENU_MODE_MARKDOWN"), nullptr, _s.markdown)) { _s.markdown = true; refresh_headings(); }
		ImGui::EndMenu();
	}
	draw_script_menu();
	if (ImGui::BeginMenu(TR("MENU_TOOLS")))
	{
		if (ImGui::MenuItem(TR("MENU_STATS"), nullptr, false, _has_project)) { update_stats(); _show_stats = true; }
		if (ImGui::MenuItem(TR("MENU_STYLE"), nullptr, false, _has_project)) { _show_style = true; run_style(); }
		if (ImGui::MenuItem(TR("MENU_TOC"), nullptr, false, _has_project)) generate_toc();
		if (ImGui::MenuItem(TR("MENU_NAMES"), nullptr, _show_names, _has_project)) _show_names = !_show_names;
		if (ImGui::MenuItem(TR("MENU_HISTORY"), "Ctrl+H", false, _has_project)) { _snapshots = History::list(current_doc()->dst_path); _snapshot_selected = -1; _show_history = true; }
		if (ImGui::MenuItem(TR("MENU_SNAPSHOT"), nullptr, false, _has_project) && History::add(current_doc()->dst_path, editor_text(_dst.editor), "manual"))
			status(TR("MSG_SNAPSHOT_ADDED"));
		ImGui::Separator();
		if (ImGui::BeginMenu(TR("MENU_PROJECT_LANGUAGE"), _has_project))
		{
			draw_project_languages();
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem(TR("MENU_GRAMMAR"), "Ctrl+F8", _s.grammar_check))
			toggle_grammar();
		if (ImGui::MenuItem(TR("MENU_PROBLEMS"), "Ctrl+F9", _show_problems, _has_project))
			_show_problems = !_show_problems;
		if (ImGui::MenuItem(TR("MENU_GRAMMAR_RESET"), nullptr, false, !_s.grammar_ignored.empty()))
		{
			_s.grammar_ignored.clear();
			_s.save();
			rebuild_spans();
		}
		if (ImGui::MenuItem(TR("MENU_SPELL"), "Ctrl+F7", _s.spell_check && _spell.available()))
			toggle_spell();
		if (ImGui::MenuItem(TR("MENU_AUTO_TYPO"), nullptr, _s.auto_typography)) toggle(_s.auto_typography);
		if (ImGui::MenuItem(TR("MENU_TYPO_PARAGRAPH"), "F7", false, _has_project)) fix_typography(false);
		if (ImGui::MenuItem(TR("MENU_TYPO_CHAPTER"), "Shift+F7", false, _has_project)) fix_typography(true);
		if (ImGui::MenuItem(TR("MENU_TYPO_SIMPLIFY_PARAGRAPH"), nullptr, false, _has_project)) fix_typography(false, true);
		if (ImGui::MenuItem(TR("MENU_TYPO_SIMPLIFY_CHAPTER"), nullptr, false, _has_project)) fix_typography(true, true);
		ImGui::Separator();
		if (ImGui::MenuItem(TR("MENU_AUTOSAVE_SWITCH"), nullptr, _s.autosave_on_switch)) toggle(_s.autosave_on_switch);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu(TR("MENU_HELP")))
	{
		if (ImGui::MenuItem(TR("MENU_ABOUT")))
			_open_about = true;
		ImGui::EndMenu();
	}
	ImGui::EndMainMenuBar();
}

// The languages of the text of the project (the dictionaries which were found), as menu items: the menu Tools and the status bar share them
void App::draw_project_languages()
{
	static const char* names[][2] = { { "fr", "Français" }, { "en_US", "English (US)" }, { "en_GB", "English (UK)" }, { "de_DE", "Deutsch" }, { "es_ES", "Español" }, { "it_IT", "Italiano" }, { "pt_BR", "Português (Brasil)" }, { "pt_PT", "Português" }, { "nl_NL", "Nederlands" } };
	for (const std::string& code : Spell::languages())
	{
		std::string name = code;
		for (const auto& n : names)
			if (code == n[0]) name = std::string(n[1]) + "  (" + code + ")";
		if (ImGui::MenuItem(name.c_str(), nullptr, _project.language == code))
			set_project_language(code);
	}
}

// A small button with an icon (a glyph of the font) and the usual text as tooltip
static bool icon_button(const char* icon, const char* tooltip)
{
	const bool clicked = ImGui::Button(icon, ImVec2(ImGui::GetFrameHeight() * 1.2f, 0.0f));
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", tooltip);
	return clicked;
}

// A toolbar button which switches a boolean, drawn pressed when on
static bool icon_toggle(const char* icon, bool* value, const char* tooltip)
{
	if (*value)
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	const bool clicked = ImGui::Button(icon, ImVec2(ImGui::GetFrameHeight() * 1.2f, 0.0f));
	if (*value)
		ImGui::PopStyleColor();
	if (clicked)
		*value = !*value;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", tooltip);
	return clicked;
}

// Toolbar: view modes (reading, destination only, preview), differences, spelling, grammar, style, search
void App::draw_toolbar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
	std::string path;
	if (icon_button("\xE2\x8F\x8F", TR("BTN_OPEN")) && dialog::open_file_dialog("", path, "Texte / Markdown", "*.md;*.markdown;*.txt"))		// eject
		open_file_path(path);
	ImGui::SameLine();
	if (icon_button("\xE2\x96\xA4", TR("BTN_FOLDER")) && dialog::pick_folder_dialog("", path))												// square with lines
		open_folder_path(path);
	ImGui::SameLine();
	if (icon_button("\xE2\x87\xA9", TR("BTN_SAVE")))																						// down arrow
		save();
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	if (icon_toggle("\xE2\x86\xB5", &_s.wrap, TR("BTN_WRAP"))) _s.save();																	// return
	ImGui::SameLine();
	if (icon_toggle("#", &_s.line_numbers, TR("BTN_LINE_NUMBERS"))) _s.save();
	ImGui::SameLine();
	if (icon_toggle("\xC2\xB6", &_s.paragraph_numbers, TR("BTN_PARAGRAPH_NUMBERS"))) _s.save();
	ImGui::SameLine();
	if (icon_toggle("\xE2\x89\xA0", &_s.show_diff, TR("BTN_DIFF"))) { _s.save(); _diff_due = ImGui::GetTime(); }								// not equal
	ImGui::SameLine();
	if (icon_toggle("\xE2\x87\x85", &_s.sync_scroll, TR("BTN_SYNC"))) _s.save();															// arrows up/down
	ImGui::SameLine();
	if (icon_toggle("\xE2\x98\xB0", &_s.show_tree, TR("BTN_TREE"))) _s.save();																// trigram
	ImGui::SameLine();
	{
		// View mode: both panes, reading (original alone) or destination only
		// Entries: 0 source (reading), 1 correction (destination only), 2 both
		const char* modes[3] = { TR("VIEW_SOURCE"), TR("VIEW_DEST"), TR("BTN_VIEW_NORMAL") };
		const int mode = reading() ? 0 : (_s.dest_only ? 1 : 2);
		float w = 0.0f;
		for (int i = 0; i < 3; i++)
			w = std::max(w, ImGui::CalcTextSize(modes[i]).x);
		ImGui::SetNextItemWidth(w + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f);
		if (ImGui::BeginCombo("##viewmode", modes[mode]))
		{
			for (int i = 0; i < 3; i++)
				if (ImGui::Selectable(modes[i], i == mode) && i != mode)
				{
					if (mode == 0) set_reading(false);
					else if (mode == 1) set_dest_only(false);
					if (i == 0) set_reading(true);
					else if (i == 1) set_dest_only(true);
				}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", TR("BTN_VIEW"));
	}
	ImGui::SameLine();
	if (icon_toggle("\xE2\x97\x90", &_s.dark, TR("BTN_DARK"))) _s.save();																	// half circle
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	// Heading of the caret
	Pane* p = outline_pane();
	const int line = ImGuiProse::GetCaret(p->editor).Line;
	int current = -1;
	for (size_t i = 0; i < _headings.size(); i++)
		if (_headings[i].line <= line)
			current = (int)i;
	const float tail = ImGui::GetFrameHeight() * 1.2f * 3.0f + 24.0f;
	ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - tail));
	if (ImGui::BeginCombo("##heading", current >= 0 ? _headings[current].title.c_str() : "-"))
	{
		for (size_t i = 0; i < _headings.size(); i++)
		{
			const std::string label = std::string((size_t)(_headings[i].level - 1) * 2, ' ') + _headings[i].title + "##h" + std::to_string(i);
			if (ImGui::Selectable(label.c_str(), (int)i == current))
			{
				ImGuiProse::SetCaret(p->editor, ImGuiProsePos(_headings[i].line, 0));
				ImGuiProse::ScrollToLine(p->editor, _headings[i].line, 0.1f);
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", TR("TOOLBAR_CHAPTER"));
	ImGui::SameLine();
	if (icon_button("\xE2\x87\xA5", TR("BTN_GOTO")))																						// arrow to bar
	{
		_open_goto = true;
		_goto_value = 1;
	}
	ImGui::SameLine();
	if (icon_button("\xE2\x89\xA1", TR("BTN_TOC")) && _has_project)																			// identical to
		generate_toc();
	ImGui::SameLine();
	if (icon_button("\xE2\x86\xBB", TR("HISTORY_TITLE")) && _has_project)																		// clockwise arrow
	{
		_snapshots = History::list(current_doc()->dst_path);
		_snapshot_selected = -1;
		_show_history = true;
	}
	ImGui::PopStyleVar(2);
	ImGui::Separator();
}
//-----------------------------------------------------------------------------
// Body: tree, source and destination panes
//-----------------------------------------------------------------------------

// A vertical splitter between two panes; delta receives the horizontal drag in pixels
static bool splitter(const char* id, float height, float* delta)
{
	ImGui::InvisibleButton(id, ImVec2(6.0f, height));
	const bool active = ImGui::IsItemActive();
	if (ImGui::IsItemHovered() || active)
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
	ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x + 2.0f, a.y), ImVec2(a.x + 4.0f, b.y), ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive : (ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator)));
	*delta = active ? ImGui::GetIO().MouseDelta.x : 0.0f;
	return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
}

// Applies the settings (fonts, wrapping, numbers, colours) to the editor of a pane
void App::configure_pane(Pane& pane)
{
	ImGuiProseConfig& c = pane.config;
	const bool dark = _s.dark;
	c.WordWrap = _s.wrap;
	c.ScrollPastEnd = true;
	c.ShowLineNumbers = _s.line_numbers;
	c.ShowParagraphNumbers = _s.paragraph_numbers;
	c.ShowNbsp = _s.show_nbsp;
	c.ShowInvisible = _s.show_invisible;
	c.Font = _s.mono_editor ? _fonts.mono : _fonts.regular;
	c.HighlightCaretParagraph = false;
	c.HighlightBackground = (_s.highlight_mode & 1) != 0;
	c.HighlightText = (_s.highlight_mode & 2) != 0;
	ImGuiProseColors& k = c.Colors;
	k.Background = dark ? IM_COL32(30, 30, 30, 255) : IM_COL32(255, 255, 255, 255);
	k.Text = dark ? IM_COL32(216, 216, 216, 255) : IM_COL32(31, 31, 31, 255);
	k.Gutter = dark ? IM_COL32(255, 255, 255, 14) : IM_COL32(0, 0, 0, 14);
	k.LineNumber = dark ? IM_COL32(133, 133, 133, 255) : IM_COL32(138, 138, 138, 255);
	k.ParagraphNumber = dark ? IM_COL32(201, 154, 60, 255) : IM_COL32(176, 106, 0, 255);
	k.Selection = dark ? IM_COL32(58, 120, 200, 130) : IM_COL32(110, 170, 255, 120);
	k.CurrentBackground = accent_color(_s, _s.accent, dark ? 2 : 0);
	k.CurrentText = accent_color(_s, _s.accent, dark ? 3 : 1);
	ImU32* markers[7] = { &k.Nbsp, &k.NarrowNbsp, &k.ThinSpace, &k.WideSpace, &k.ZeroWidth, &k.SoftHyphen, &k.Control };
	for (int i = 0; i < 7; i++)
		if (_s.invisible_colors[i])
			*markers[i] = _s.invisible_colors[i];
}

// The area under the toolbar: outline tree, source pane, splitter, destination pane. Also the synchronised scrolling of the two panes.
void App::draw_body()
{
	ImGuiIO& io = ImGui::GetIO();
	const float status_h = ImGui::GetFrameHeightWithSpacing() + 4.0f;
	const float log_h = _s.show_log ? _s.log_height + 6.0f : 0.0f;
	const float body_h = std::max(60.0f, ImGui::GetContentRegionAvail().y - status_h - log_h);

	// Current paragraph: the one of the pane which has the focus, in both panes
	if (ImGuiProse::HasFocus(_src.editor))
		_active = &_src;
	else if (ImGuiProse::HasFocus(_dst.editor))
		_active = &_dst;
	if (reading())
		_active = &_src;
	else if (_s.dest_only)
		_active = &_dst;
	const int para = ImGuiProse::GetParagraphOfLine(_active->editor, ImGuiProse::GetCaret(_active->editor).Line);
	if (para > 0)
		_current_para = para;
	configure_pane(_src);
	configure_pane(_dst);
	_src.config.ReadOnly = !(reading() && _edit_source);
	_src.config.HighlightLine = _current_para > 0 ? ImGuiProse::GetLineOfParagraph(_src.editor, highlight_para(&_src)) : 0;
	_dst.config.HighlightLine = _current_para > 0 ? ImGuiProse::GetLineOfParagraph(_dst.editor, highlight_para(&_dst)) : 0;

	float dx = 0.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	if (_s.show_tree)
	{
		draw_tree(_s.tree_width, body_h);
		ImGui::SameLine();
		splitter("##split_tree", body_h, &dx);
		_s.tree_width = std::max(120.0f, std::min(600.0f, _s.tree_width + dx));
		ImGui::SameLine();
	}
	const float remaining = ImGui::GetContentRegionAvail().x;
	if (reading())
		draw_pane(_src, "##src", remaining, body_h);
	else if (_s.dest_only)
		draw_pane(_dst, "##dst", remaining, body_h);
	else
	{
		const float usable = remaining - 6.0f;
		const float w_src = std::max(100.0f, usable * _s.split);
		draw_pane(_src, "##src", w_src, body_h);
		ImGui::SameLine();
		if (splitter("##split_panes", body_h, &dx)) _s.split = 0.5f;
		_s.split = std::max(0.15f, std::min(0.85f, _s.split + dx / std::max(usable, 1.0f)));
		ImGui::SameLine();
		draw_pane(_dst, "##dst", std::max(100.0f, usable - w_src), body_h);

		update_spacers();

		// The panes scroll together: the one moved by the user drives the other
		const bool a = ImGuiProse::ScrolledByUser(_src.editor), b = ImGuiProse::ScrolledByUser(_dst.editor);
		if (_s.sync_scroll && !_src.preview && !_dst.preview)
		{
			auto src_to_dst = [&]() {
				ImGuiProseScrollAnchor anchor = ImGuiProse::GetScrollAnchor(_src.editor);
				if (anchor.Paragraph > 0)
					anchor.Paragraph = diff::map_src_to_dst(_align, anchor.Paragraph - 1) + 1;
				ImGuiProse::SetScrollAnchor(_dst.editor, anchor);
				_sync_settle = 3;
			};
			auto dst_to_src = [&]() {
				ImGuiProseScrollAnchor anchor = ImGuiProse::GetScrollAnchor(_dst.editor);
				if (anchor.Paragraph > 0)
					anchor.Paragraph = diff::map_dst_to_src(_align, anchor.Paragraph - 1) + 1;
				ImGuiProse::SetScrollAnchor(_src.editor, anchor);
				_sync_settle = 3;
			};
			auto same = [](const ImGuiProseScrollAnchor& x, const ImGuiProseScrollAnchor& y) { return x.Paragraph == y.Paragraph && std::fabs(x.Fraction - y.Fraction) < 0.03f; };
			const ImGuiProseScrollAnchor sa = ImGuiProse::GetScrollAnchor(_src.editor), da = ImGuiProse::GetScrollAnchor(_dst.editor);
			const int sv = ImGuiProse::GetVersion(_src.editor), dv = ImGuiProse::GetVersion(_dst.editor);
			// What the user does always wins; the wait only concerns the moves made by the program
			if (a && !b)
				src_to_dst();
			else if (b && !a)
				dst_to_src();
			else if (_sync_settle > 0)
				_sync_settle--;
			else if (sv == _prev_src_version && dv == _prev_dst_version)
			{
				// Moved by the program (open a chapter, go to a line, find): the other pane follows
				if (!same(sa, _prev_src_anchor) && same(da, _prev_dst_anchor))
					src_to_dst();
				else if (!same(da, _prev_dst_anchor) && same(sa, _prev_src_anchor))
					dst_to_src();
			}
			_prev_src_anchor = ImGuiProse::GetScrollAnchor(_src.editor);
			_prev_dst_anchor = ImGuiProse::GetScrollAnchor(_dst.editor);
			_prev_src_version = sv;
			_prev_dst_version = dv;
		}
	}
	ImGui::PopStyleVar();
	IM_UNUSED(io);
	if (_s.show_log)
		draw_log();
}

// Facing paragraphs start at the same height: the pane with the shorter text gets empty space (not editable) above the paragraph
void App::update_spacers()
{
	ImGuiProseEditor* se = _src.editor;
	ImGuiProseEditor* de = _dst.editor;
	const int sn = ImGuiProse::GetParagraphCount(se), dn = ImGuiProse::GetParagraphCount(de);
	if (!_s.align_paragraphs || _src.preview || _dst.preview || reading() || _s.dest_only)
	{
		ImGuiProse::SetParagraphSpacers(se, nullptr, 0);
		ImGuiProse::SetParagraphSpacers(de, nullptr, 0);
		return;
	}
	if (_diff_due >= 0.0)
		return;											// the paragraphs are being compared again: keep what is shown
	// The paragraphs which face each other: the same ones, and the first ones of what differs
	std::vector<std::pair<int, int>> pairs;
	int sp = 0, dp = 0;
	for (const diff::Block& b : _align.blocks)
	{
		for (; sp < b.a0 && dp < b.b0; sp++, dp++)
			pairs.push_back({ sp, dp });
		if (b.a1 > b.a0 && b.b1 > b.b0)
			pairs.push_back({ b.a0, b.b0 });
		sp = b.a1;
		dp = b.b1;
	}
	for (; sp < sn && dp < dn; sp++, dp++)
		pairs.push_back({ sp, dp });
	std::vector<float> src_pad((size_t)sn, 0.0f), dst_pad((size_t)dn, 0.0f);
	float src_extra = 0.0f, dst_extra = 0.0f;			// space added above so far
	for (const auto& pr : pairs)
	{
		if (pr.first >= sn || pr.second >= dn)
			continue;
		float st = 0.0f, dt = 0.0f;
		if (!ImGuiProse::GetParagraphGeometry(se, pr.first + 1, &st, nullptr) || !ImGuiProse::GetParagraphGeometry(de, pr.second + 1, &dt, nullptr))
			continue;
		const float ys = st + src_extra, yd = dt + dst_extra;
		if (ys > yd + 0.5f)
		{
			dst_pad[(size_t)pr.second] = ys - yd;
			dst_extra += ys - yd;
		}
		else if (yd > ys + 0.5f)
		{
			src_pad[(size_t)pr.first] = yd - ys;
			src_extra += yd - ys;
		}
	}
	ImGuiProse::SetParagraphSpacers(se, src_pad.data(), sn);
	ImGuiProse::SetParagraphSpacers(de, dst_pad.data(), dn);
}

// Outline of the project: chapters and their headings; a click goes there
void App::draw_tree(float width, float height)
{
	ImGui::BeginChild("##tree", ImVec2(width, height), true);
	if (!_has_project)
	{
		ImGui::TextDisabled(TR("TREE_NO_PROJECT"));
		ImGui::TextWrapped(TR("TREE_HINT"));
		ImGui::EndChild();
		return;
	}
	{ const std::string head = _project.is_folder ? i18n::fmt(TR("TREE_PROJECT"), _project.name.c_str()) : std::string(TR("TREE_FILE")); ImGui::TextUnformatted(head.c_str()); }
	if (_project.is_folder)
	{
		ImGui::SameLine(width - 74.0f);
		if (ImGui::SmallButton("\xE2\x96\xB2") && _cur > 0)
		{
			_project.move(_cur, -1);
			_cur--;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("\xE2\x96\xBC") && _cur + 1 < (int)_project.docs.size())
		{
			_project.move(_cur, 1);
			_cur++;
		}
	}
	ImGui::Separator();
	for (int i = 0; i < (int)_project.docs.size(); i++)
	{
		const Doc& d = _project.docs[i];
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (d.headings.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;
		if (i == _cur)
			flags |= ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_DefaultOpen;
		const std::string label = std::string(d.dirty ? "\xE2\x97\x8F " : "") + d.rel;
		const bool open = ImGui::TreeNodeEx((void*)(intptr_t)(i + 1), flags, "%s", label.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			request_open_doc(i);
		if (open)
		{
			for (size_t h = 0; h < d.headings.size(); h++)
			{
				const Heading& hd = d.headings[h];
				const std::string t = std::string((size_t)(hd.level - 1) * 2, ' ') + hd.title + "##" + std::to_string(i) + "_" + std::to_string(h);
				if (ImGui::Selectable(t.c_str()))
					request_open_doc(i, hd.line);
			}
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();
}

// Images of the preview: loaded on demand, relative to the folder of the pane's file (then of the other one), kept as textures
static std::vector<std::string> g_image_dirs;
static App* g_link_app = nullptr;				// the application, for the callback of the links of the preview

// Image callback of the Markdown preview: loads a file or a data URL next to the document, kept in a cache
static ImTextureID preview_image(const char* url, ImVec2* out_size, void*)
{
	struct Tex { GLuint id; int w, h; };
	static std::map<std::string, Tex> cache;
	std::string key = url;
	for (const std::string& dir : g_image_dirs)
		key += "|" + dir;
	auto it = cache.find(key);
	if (it == cache.end())
	{
		Tex tex = { 0, 0, 0 };
		if (strncmp(url, "http", 4) != 0)
			for (const std::string& dir : g_image_dirs)
			{
				std::string path = std::filesystem::u8path(url).is_absolute() ? std::string(url) : util::combine(dir, url);
				int n;
				unsigned char* data = stbi_load(path.c_str(), &tex.w, &tex.h, &n, 4);
				if (!data)
					continue;
				glGenTextures(1, &tex.id);
				glBindTexture(GL_TEXTURE_2D, tex.id);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.w, tex.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
				stbi_image_free(data);
				break;
			}
		it = cache.insert(std::make_pair(key, tex)).first;
	}
	if (it->second.id == 0)
		return 0;
	*out_size = ImVec2((float)it->second.w, (float)it->second.h);
	return (ImTextureID)(intptr_t)it->second.id;
}
// One pane: its editor or its Markdown preview, with the context menus and the selection counts
void App::draw_pane(Pane& pane, const char* id, float width, float height)
{
	ImGui::BeginGroup();
	const Doc* d = current_doc();
	std::string title = pane.is_source ? TR("PANE_SOURCE") : TR("PANE_DESTINATION");
	if (d)
		title = i18n::fmt(pane.is_source ? TR("PANE_TITLE_SOURCE") : TR("PANE_TITLE_DESTINATION"), d->rel.c_str()) + (!pane.is_source && d->dirty ? " *" : "");
	// Title bar with a colour code: blue for the original, green for the work
	const float header_h = ImGui::GetFrameHeight();
	const ImVec2 bar_min = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(bar_min, ImVec2(bar_min.x + width, bar_min.y + header_h),
		pane.is_source ? IM_COL32(40, 85, 150, 255) : IM_COL32(40, 120, 70, 255));
	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 0.0f));
	const float pad_y = (header_h - ImGui::GetTextLineHeight()) * 0.5f;
	ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 6.0f, ImGui::GetCursorPosY() + pad_y));
	ImGui::TextUnformatted(title.c_str());
	if (_s.markdown)
	{
		ImGui::SameLine(0.0f, 12.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
		ImGui::PushID(id);
		if (ImGui::Checkbox(TR("CHK_PREVIEW"), &pane.preview) && !_force_reading)
		{
			(pane.is_source ? _s.preview_source : _s.preview_destination) = pane.preview;
			_s.save();
		}
		ImGui::PopID();
	}
	if (pane.is_source && reading())
	{
		ImGui::SameLine(0.0f, 12.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
		if (ImGui::Checkbox(TR("CHK_EDIT"), &_edit_source) && _edit_source)
			pane.preview = false;
		if (_edit_source && _src_dirty)
		{
			ImGui::SameLine();
			ImGui::TextUnformatted("*");
		}
	}
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	ImGui::SetCursorScreenPos(ImVec2(bar_min.x, bar_min.y + header_h));
	ImGui::Dummy(ImVec2(0.0f, 0.0f));
	ImGui::Dummy(ImVec2(0.0f, 0.0f));

	if (pane.preview && _s.markdown)
	{
		const double now = ImGui::GetTime();
		const int version = ImGuiProse::GetVersion(pane.editor);
		if (version != pane.md_version)
		{
			if (pane.md_due < 0.0 || pane.md_version < 0)
				pane.md_due = (pane.md_version < 0) ? now : now + 0.4;
			if (now >= pane.md_due)
			{
				ImGuiMd::SetText(pane.md, editor_text(pane.editor).c_str());
				pane.md_version = version;
				pane.md_due = -1.0;
			}
		}
		ImGuiMdConfig md;
		md.Fonts.Regular = _fonts.regular;
		md.Fonts.Bold = _fonts.bold;
		md.Fonts.Italic = _fonts.italic;
		md.Fonts.BoldItalic = _fonts.bold_italic;
		md.Fonts.Code = _fonts.mono;
		for (int i = 0; i < 3; i++)
			md.Fonts.Heading[i] = _fonts.heading[i];
		md.Colors.HighlightBg = (accent_color(_s, _s.accent, _s.dark ? 2 : 0) & 0x00FFFFFF) | IM_COL32(0, 0, 0, 140);
		md.CodeBlockCallback = [](const char* lang, const char* text, int len, float w, bool draw, ImVec2 pos, void* user) -> float {
			return ImGuiMdDiagrams::CodeBlock((ImGuiMdDiagramCache*)user, lang, text, len, w, draw, pos);
		};
		md.UserData = _diagrams;
		md.ImageCallback = preview_image;
		g_link_app = this;
		md.LinkCallback = [](const char* url, void*) { if (g_link_app) g_link_app->queue_link(url, g_image_dirs); };		// followed at the start of the next frame
		g_image_dirs.clear();
		if (d)
		{
			const std::string s = util::parent_path(d->src_path), t = util::parent_path(d->dst_path);
			g_image_dirs = pane.is_source ? std::vector<std::string>{ s, t } : std::vector<std::string>{ t, s };
		}
		pane.md_state.HighlightLine = _current_para > 0 ? ImGuiProse::GetLineOfParagraph(pane.editor, highlight_para(&pane)) : 0;
		if (_current_para != _last_para && _active != &pane)
			pane.md_state.ScrollToLine = pane.md_state.HighlightLine;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, pane.config.Colors.Background);
		ImGui::BeginChild(id, ImVec2(width, height - header_h), false);
		ImGuiMd::Render(pane.md, md, &pane.md_state);
		ImGui::EndChild();
		ImGui::PopStyleColor();
		if (pane.md_state.ClickedLine > 0)
		{
			_active = &pane;
			ImGuiProse::SetCaret(pane.editor, ImGuiProsePos(pane.md_state.ClickedLine, 0), false);
		}
	}
	else
	{
		const bool changed = ImGuiProse::Render(pane.editor, id, pane.config, ImVec2(width, height - header_h));
		// Ctrl+click on a link of the Markdown text follows it (the click has just put the caret there)
		if (_s.markdown && ImGui::GetIO().KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()))
		{
			const ImGuiProsePos where = ImGuiProse::GetCaret(pane.editor);
			int length = 0;
			const char* text = ImGuiProse::GetLine(pane.editor, where.Line, &length);
			const std::string url = text ? link_in_line(text, length, where.Column) : std::string();
			if (!url.empty() && d)
			{
				const std::string s = util::parent_path(d->src_path), t = util::parent_path(d->dst_path);
				queue_link(url, pane.is_source ? std::vector<std::string>{ s, t } : std::vector<std::string>{ t, s });
			}
		}
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()))
			spell_menu_at(pane, ImGui::GetIO().MousePos);
		// A colored passage of the style analysis: what it is and what to do
		if (_show_style && _style_done && _style_highlight && &pane == &style_pane() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)
			&& ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()))
		{
			ImGuiProsePos mp;
			if (ImGuiProse::GetPositionFromScreen(pane.editor, ImGui::GetIO().MousePos, &mp))
				for (const StyleMark& mk : _style_marks)
					if (mk.line == mp.Line && mp.Column >= mk.start && mp.Column <= mk.end)
					{
						const style::Observation& o = _style_report.observations[mk.obs];
						static const char* const keys[] = { "OBS_CONNECTOR", "OBS_STOCK_PHRASE", "OBS_CLOSE_REPETITION", "OBS_REPEATED_PHRASE", "OBS_SAME_OPENING", "OBS_LONG_SENTENCE", "OBS_ENUMERATION", "OBS_REDUNDANT_PARAGRAPHS", "OBS_CONSTRUCTION", "OBS_PARALLEL_STRUCTURE", "OBS_STYLE_BREAK" };
						ImGui::BeginTooltip();
						ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
						ImGui::TextUnformatted(i18n::fmt(TR(keys[std::min(o.kind, 10)]), o.text.c_str(), o.count).c_str());
						if (const style::Contribution* c = style_points(o.kind))
							ImGui::TextDisabled("%s", i18n::fmt(TR("OBS_POINTS"), c->points, c->max_points).c_str());
						ImGui::Separator();
						ImGui::TextWrapped("%s", style_advice(o).c_str());
						ImGui::PopTextWrapPos();
						ImGui::EndTooltip();
						break;
					}
		}
		if (changed && !pane.is_source)
			if (Doc* doc = current_doc())
				doc->dirty = true;
		if (changed && pane.is_source && !pane.config.ReadOnly)
			_src_dirty = true;
	}
	ImGui::EndGroup();
	if (&pane == &_dst || reading())
		_last_para = _current_para;
}

// Status bar: position, words and characters (of the selection, else the document), objective, differences; on the right the message or the saved state
void App::draw_status()
{
	const ImGuiProsePos p = ImGuiProse::GetCaret(_active->editor);
	const int para = ImGuiProse::GetParagraphOfLine(_active->editor, p.Line);
	ImGui::Separator();
	ImGui::Text(TR("STATUS_POSITION"), _active->is_source ? TR("PANE_SOURCE_SHORT") : TR("PANE_DESTINATION_SHORT"), p.Line, ImGuiProse::GetLineCount(_active->editor),
		para > 0 ? std::to_string(para).c_str() : "-", ImGuiProse::GetParagraphCount(_active->editor), p.Column + 1);
	if (_has_project)
	{
		// Words of the text of the pane, and of the selection if there is one
		const TextStats& st = _active->is_source ? _stats_src : _stats_dst;
		const int sel = selection_words();
		(void)0;
		ImGui::SameLine();
		if (sel > 0 || _sel_chars > 0)
			ImGui::Text("  |  %s", i18n::fmt(TR("STATUS_WORDS_SELECTION"), sel, _sel_chars, st.words).c_str());
		else
			ImGui::Text("  |  %s", i18n::fmt(TR("STATUS_WORDS"), st.words).c_str());
		if (_goal_chapter > 0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("  |  %s", i18n::fmt(TR("GOAL_STATUS"), st.words, _goal_chapter, st.words * 100 / _goal_chapter).c_str());
		}
	}
	if (_s.show_diff && _has_project)
	{
		ImGui::SameLine();
		ImGui::Text("  |  %s", i18n::fmt(TR("STATUS_DIFF"), _diff.changed, _diff.added, _diff.removed).c_str());
	}
	const float left_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

	// At the far right: the language of the text (a click changes it), and the switches of the spelling (red, like its underline) and of the grammar (blue)
	std::string language = _project.language;
	for (char& ch : language)
		ch = ch == '_' ? '-' : (char)toupper((unsigned char)ch);
	const ImVec2 pad(6.0f, 1.0f);
	auto width = [&](const char* text) { return ImGui::CalcTextSize(text).x + pad.x * 2.0f; };
	const float cluster = (_has_project ? width(language.c_str()) + 4.0f : 0.0f) + width("Ab") + 4.0f + width("Gr") + 10.0f;
	const float cluster_x = ImGui::GetWindowWidth() - cluster;
	ImGui::SameLine(cluster_x);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, pad);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
	// A flat button: tinted when on, the colour of a disabled text when off
	auto flat = [&](const char* label, bool on, ImVec4 color, const std::string& tip)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, on ? 0.25f : 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.40f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 0.55f));
		ImGui::PushStyleColor(ImGuiCol_Text, on ? color : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		const bool clicked = ImGui::Button(label);
		ImGui::PopStyleColor(4);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tip.c_str());
		return clicked;
	};
	if (_has_project)
	{
		if (flat(language.c_str(), true, ImGui::GetStyleColorVec4(ImGuiCol_Text), TR("MENU_PROJECT_LANGUAGE")))
			ImGui::OpenPopup("##textlanguage");
		if (ImGui::BeginPopup("##textlanguage"))
		{
			draw_project_languages();
			ImGui::EndPopup();
		}
		ImGui::SameLine(0.0f, 4.0f);
	}
	const ImVec4 red = _s.dark ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.78f, 0.12f, 0.12f, 1.0f);
	const ImVec4 blue = _s.dark ? ImVec4(0.45f, 0.65f, 1.0f, 1.0f) : ImVec4(0.12f, 0.38f, 0.85f, 1.0f);
	if (flat("Ab", _s.spell_check && _spell.available(), red, std::string(TR("MENU_SPELL")) + "  (Ctrl+F7)"))
		toggle_spell();
	ImGui::SameLine(0.0f, 4.0f);
	if (flat("Gr", _s.grammar_check, blue, std::string(TR("MENU_GRAMMAR")) + "  (Ctrl+F8)"))
		toggle_grammar();
	ImGui::PopStyleVar(2);
	std::string right = ImGui::GetTime() < _status_until ? _status : std::string(dirty() ? TR("STATUS_MODIFIED") : (_has_project ? TR("STATUS_SAVED") : ""));
	// Right aligned, shortened at its start if it does not fit
	const float room = cluster_x - left_end - 40.0f;
	while (right.size() > 4 && ImGui::CalcTextSize(right.c_str()).x > room)
	{
		size_t cut = 1;
		while (cut < right.size() && (right[cut] & 0xC0) == 0x80)
			cut++;
		right.erase(0, cut);
	}
	ImGui::SameLine(std::max(left_end + 20.0f, cluster_x - ImGui::CalcTextSize(right.c_str()).x - 12.0f));
	ImGui::TextUnformatted(right.c_str());}

//-----------------------------------------------------------------------------
// Dialogs
//-----------------------------------------------------------------------------

// Context menus (spelling, grammar) and modal boxes: confirmation, go to, destination folder, alignment pin
void App::draw_dialogs()
{
	draw_prefs();
	if (_open_spell_menu)
	{
		ImGui::OpenPopup("##spell_menu");
		_open_spell_menu = false;
	}
	if (ImGui::BeginPopup("##spell_menu"))
	{
		ImGui::TextDisabled("%s", _spell_word.c_str());
		ImGui::Separator();
		if (_spell_suggestions.empty())
			ImGui::TextDisabled("%s", TR("SPELL_NO_SUGGESTION"));
		for (const std::string& s : _spell_suggestions)
			if (ImGui::MenuItem(s.c_str()) && _active)
			{
				if (!_active->is_source)
					ImGuiProse::Replace(_active->editor, ImGuiProsePos(_spell_line, _spell_start), ImGuiProsePos(_spell_line, _spell_end), s.c_str());
			}
		ImGui::Separator();
		if (ImGui::MenuItem(TR("SPELL_IGNORE")))
		{
			_spell.ignore(_spell_word);
			rebuild_spans();
		}
		if (ImGui::MenuItem(TR("SPELL_ADD")))
		{
			_spell.add(_spell_word);
			rebuild_spans();
		}
		ImGui::EndPopup();
	}
	if (_open_grammar_menu)
	{
		ImGui::OpenPopup("##grammar_menu");
		_open_grammar_menu = false;
	}
	if (ImGui::BeginPopup("##grammar_menu"))
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
		ImGui::TextWrapped("%s", _gram_message.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Separator();
		for (const std::string& s : _gram_issue.suggestions)
		{
			const std::string label = s.empty() ? std::string(TR("GRAM_DELETE")) : s;
			if (ImGui::MenuItem(label.c_str()) && _active && !_active->is_source)
				ImGuiProse::Replace(_active->editor, ImGuiProsePos(_gram_line, _gram_issue.start), ImGuiProsePos(_gram_line, _gram_issue.end), s.c_str());
		}
		ImGui::Separator();
		if (ImGui::MenuItem(TR("GRAM_IGNORE_RULE")))
		{
			_s.grammar_ignored += (_s.grammar_ignored.empty() ? "" : ",") + _gram_issue.rule;
			_s.save();
			rebuild_spans();
		}
		ImGui::EndPopup();
	}
	if (_open_confirm) { ImGui::OpenPopup(TR("DLG_CONFIRM_TITLE")); _open_confirm = false; }
	if (_open_goto) { ImGui::OpenPopup(TR("DLG_GOTO_TITLE")); _open_goto = false; }
	if (_open_dest) { ImGui::OpenPopup(TR("DLG_DEST_TITLE")); _open_dest = false; }

	const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	if (_open_pin) { ImGui::OpenPopup(TR("DLG_PIN_TITLE")); _open_pin = false; }
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal(TR("DLG_PIN_TITLE"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGuiProseEditor* other = _pin_from_src ? _dst.editor : _src.editor;
		ImGui::Text(TR(_pin_from_src ? "PIN_FROM_SRC" : "PIN_FROM_DST"), _pin_para);
		ImGui::Text(TR("GOTO_RANGE"), ImGuiProse::GetParagraphCount(other));
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::InputInt("##pin", &_pin_value, 1, 10);
		const bool enter = ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
		if (enter || ImGui::Button(TR("OK")))
		{
			apply_pin(_pin_from_src ? _pin_para : _pin_value, _pin_from_src ? _pin_value : _pin_para);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("BTN_CANCEL")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal(TR("DLG_CONFIRM_TITLE"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(_confirm_text.c_str());
		ImGui::Spacing();
		if (ImGui::Button(TR("BTN_SAVE")))
		{
			ImGui::CloseCurrentPopup();
			if (save() && _after_confirm)
				_after_confirm();
			_after_confirm = nullptr;
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("BTN_DONT_SAVE")))
		{
			ImGui::CloseCurrentPopup();
			if (Doc* d = current_doc())
				d->dirty = false;
			if (_after_confirm)
				_after_confirm();
			_after_confirm = nullptr;
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("BTN_CANCEL")))
		{
			ImGui::CloseCurrentPopup();
			_after_confirm = nullptr;
		}
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal(TR("DLG_GOTO_TITLE"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGuiProseEditor* ed = _active->editor;
		ImGui::RadioButton(TR("GOTO_LINE"), &_goto_kind, 0);
		_s.goto_kind = _goto_kind;
		ImGui::SameLine();
		ImGui::RadioButton(TR("GOTO_PARAGRAPH"), &_goto_kind, 1);
		ImGui::Text(TR("GOTO_RANGE"), _goto_kind == 0 ? ImGuiProse::GetLineCount(ed) : ImGuiProse::GetParagraphCount(ed));
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		// The value follows what is typed (with EnterReturnsTrue it was only taken at Enter, so a click on OK used the old one)
		ImGui::InputInt("##goto", &_goto_value, 1, 10);
		const bool enter = ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
		if (enter || ImGui::Button(TR("OK")))
		{
			go_to(_goto_kind, _goto_value);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("BTN_CANCEL")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f));
	if (ImGui::BeginPopupModal(TR("DLG_DEST_TITLE"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped(TR("DLG_DEST_TEXT"));
		char buf[1024];
		snprintf(buf, sizeof(buf), "%s", _dest_input.c_str());
		ImGui::SetNextItemWidth(480.0f);
		if (ImGui::InputText("##dest", buf, sizeof(buf)))
			_dest_input = buf;
		ImGui::SameLine();
		std::string picked;
		if (ImGui::Button(TR("BTN_BROWSE")) && dialog::pick_folder_dialog(_dest_input, picked))
			_dest_input = picked;
		ImGui::Spacing();
		if (ImGui::Button(TR("OK")) && !_dest_input.empty())
		{
			ImGui::CloseCurrentPopup();
			if (!_dest_folder.empty())
			{
				const std::string folder = _dest_folder, dest = _dest_input;
				_dest_folder.clear();
				open_folder_path(folder, dest);
			}
			else if (_has_project)
			{
				const std::string dest = _dest_input;
				guard([this, dest]() {
					const std::string rel = current_doc() ? current_doc()->rel : "";
					_project.set_destination(dest);
					for (Doc& d : _project.docs)
					{
						std::string text;
						if (!(util::exists(d.dst_path) && util::read_text(d.dst_path, text)))
							util::read_text(d.src_path, text);
						d.headings = parse_headings(util::split_lines(text), d.is_markdown());
					}
					int idx = 0;
					for (int i = 0; i < (int)_project.docs.size(); i++)
						if (_project.docs[i].rel == rel)
							idx = i;
					_cur = -1;
					open_doc(idx);
				});
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("BTN_CANCEL")))
		{
			_dest_folder.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// Find and replace window (text, regular expressions, special characters, all chapters)
void App::draw_find()
{
	ImGui::SetNextWindowSize(ImVec2(680.0f, 460.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x + 340.0f, ImGui::GetMainViewport()->WorkPos.y + 90.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("FIND_TITLE"), &_show_find))
	{
		ImGui::End();
		return;
	}
	bool refresh = false;
	const float field_w = ImGui::GetContentRegionAvail().x - 250.0f;

	// Search: the text, then the options
	if (_find_focus)
	{
		ImGui::SetKeyboardFocusHere();
		_find_focus = false;
	}
	ImGui::SetNextItemWidth(field_w);
	const std::string before = _find_text;
	const bool enter = ImGui::InputTextWithHint("##find", TR("FIND_HINT"), _find_text, sizeof(_find_text), ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::IsItemActive())
		_find_field = 0;
	if (before != _find_text)
		refresh = true;
	ImGui::SameLine();
	if (ImGui::Checkbox(TR("FIND_CASE"), &_find_case))
	{
		_s.find_case = _find_case;
		_s.save();
		refresh = true;
	}
	ImGui::SameLine();
	if (ImGui::Checkbox(TR("FIND_REGEX"), &_find_regex))
		refresh = true;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", TR("FIND_REGEX_HELP"));
	ImGui::SameLine();
	ImGui::BeginDisabled(_find_regex);
	if (ImGui::Checkbox(TR("FIND_WORD"), &_find_word))
		refresh = true;
	ImGui::EndDisabled();

	// Replacement, and the special characters (written as escapes: \t \n     ...)
	ImGui::SetNextItemWidth(field_w);
	ImGui::InputTextWithHint("##replace", TR("FIND_REPLACE_HINT"), _replace_text, sizeof(_replace_text));
	if (ImGui::IsItemActive())
		_find_field = 1;
	ImGui::SameLine();
	if (ImGui::Button(TR("FIND_SPECIAL")))
		ImGui::OpenPopup("##special");
	if (ImGui::BeginPopup("##special"))
	{
		static const struct { const char* escape; const char* key; } specials[] = {
			{ "\\t", "CHAR_TAB" }, { "\\n", "CHAR_NEWLINE" }, { "\\u00a0", "CHAR_NBSP" }, { "\\u202f", "CHAR_NNBSP" }, { "\\u2009", "CHAR_THIN" }, { "\\u200a", "CHAR_HAIR" },
			{ "\\u200b", "CHAR_ZWSP" }, { "\\u00ad", "CHAR_SHY" }, { "\\u2011", "CHAR_NBHYPHEN" } };
		for (const auto& s : specials)
		{
			const std::string label = std::string(s.escape) + "   " + TR(s.key);
			if (ImGui::MenuItem(label.c_str()))
			{
				char* field = _find_field == 0 ? _find_text : _replace_text;
				const size_t size = _find_field == 0 ? sizeof(_find_text) : sizeof(_replace_text);
				if (strlen(field) + strlen(s.escape) < size)
					strcat(field, s.escape);
				refresh = true;
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem(TR("MENU_CHARMAP")))
			_show_charmap = true;
		ImGui::EndPopup();
	}
	if (refresh)
	{
		update_matches();
		rebuild_spans();
	}
	if (enter)
		find_next(1);

	const bool can_replace = _active == &_dst && !_engine.empty() && _find_error.empty();
	if (ImGui::Button(TR("BTN_PREVIOUS"))) find_next(-1);
	ImGui::SameLine();
	if (ImGui::Button(TR("BTN_NEXT"))) find_next(1);
	ImGui::SameLine();
	ImGui::BeginDisabled(!can_replace);
	if (ImGui::Button(TR("BTN_REPLACE"))) replace_in_editor(false);
	ImGui::SameLine();
	if (ImGui::Button(TR("BTN_REPLACE_ALL"))) replace_in_editor(true);
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button(TR("FIND_ALL_CHAPTERS")))
		search_all();
	ImGui::SameLine();
	ImGui::BeginDisabled(!_has_project || _engine.empty() || !_find_error.empty());
	if (ImGui::Button(TR("BTN_REPLACE_PROJECT")))
		replace_in_project();
	ImGui::EndDisabled();
	if (!_find_error.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", _find_error.c_str());
	else
		ImGui::Text(TR("FIND_MATCH_COUNT"), (int)_matches.size(), _active->is_source ? TR("FIND_IN_SOURCE") : TR("FIND_IN_DESTINATION"));
	if (_find_all_chapters && _hits_for != _find_text)
	{
		_find_all_chapters = false;
		search_all();
	}
	if (!_hits.empty())
	{
		ImGui::Separator();
		ImGui::Text(TR("FIND_PROJECT_RESULTS"), (int)_hits.size(), _hits_for.c_str());
		ImGui::BeginChild("##hits", ImVec2(0, 0), true);
		for (size_t i = 0; i < _hits.size(); i++)
		{
			std::string text = _hits[i].text;
			if (text.size() > 160)
				text.resize(160);
			const std::string label = _project.docs[_hits[i].doc].rel + "  l." + std::to_string(_hits[i].line) + "   " + text + "##hit" + std::to_string(i);
			if (ImGui::Selectable(label.c_str()))
				request_open_doc(_hits[i].doc, _hits[i].line);
		}
		ImGui::EndChild();
	}
	ImGui::End();
	if (!_show_find)
	{
		_matches.clear();
		rebuild_spans();
	}
}

// History window: the versions kept at each save, with the differences against the current text
void App::draw_history()
{
	ImGui::SetNextWindowSize(ImVec2(1000.0f, 560.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("HISTORY_TITLE"), &_show_history))
	{
		ImGui::End();
		return;
	}
	ImGui::BeginChild("##snapshots", ImVec2(330.0f, 0.0f), true);
	if (_snapshots.empty())
		ImGui::TextWrapped(TR("HISTORY_EMPTY"));
	for (size_t i = 0; i < _snapshots.size(); i++)
		if (ImGui::Selectable((_snapshots[i].label + "##s" + std::to_string(i)).c_str(), (int)i == _snapshot_selected))
		{
			_snapshot_selected = (int)i;
			_snapshot_diff = diff::lines(History::read(_snapshots[i]), editor_text(_dst.editor), _snapshot_only_changes);
		}
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::TextWrapped(TR("HISTORY_LEGEND"));
	if (ImGui::Checkbox(TR("HISTORY_ONLY_CHANGES"), &_snapshot_only_changes))
		_s.history_only_changes = _snapshot_only_changes;
	if (ImGui::IsItemDeactivatedAfterEdit() && _snapshot_selected >= 0)
		_snapshot_diff = diff::lines(History::read(_snapshots[_snapshot_selected]), editor_text(_dst.editor), _snapshot_only_changes);
	ImGui::SameLine();
	if (ImGui::Button(TR("HISTORY_RESTORE")) && _snapshot_selected >= 0)
	{
		const std::string text = History::read(_snapshots[_snapshot_selected]);
		const int last = ImGuiProse::GetLineCount(_dst.editor);
		int len = 0;
		ImGuiProse::GetLine(_dst.editor, last, &len);
		ImGuiProse::Replace(_dst.editor, ImGuiProsePos(1, 0), ImGuiProsePos(last, len), text.c_str());
		status(TR("MSG_VERSION_RESTORED"));
		_show_history = false;
	}
	ImGui::SameLine();
	if (ImGui::Button(TR("BTN_DELETE")) && _snapshot_selected >= 0)
	{
		History::remove(_snapshots[_snapshot_selected]);
		_snapshots.erase(_snapshots.begin() + _snapshot_selected);
		_snapshot_selected = -1;
		_snapshot_diff.clear();
	}
	ImGui::BeginChild("##diff", ImVec2(0, 0), true);
	for (const diff::Line& l : _snapshot_diff)
	{
		const ImU32 col = l.sign == '+' ? IM_COL32(60, 200, 100, 255) : (l.sign == '-' ? IM_COL32(230, 90, 90, 255) : ImGui::GetColorU32(ImGuiCol_Text));
		ImGui::PushStyleColor(ImGuiCol_Text, col);
		ImGui::TextWrapped("%c %s", l.sign, l.text.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();
	ImGui::EndGroup();
	ImGui::End();
}

// Table of contents window, to copy or insert
void App::draw_toc()
{
	ImGui::SetNextWindowSize(ImVec2(700.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("TOC_TITLE"), &_show_toc))
	{
		ImGui::End();
		return;
	}
	if (ImGui::Button(TR("BTN_COPY")))
		ImGui::SetClipboardText(_toc_text.c_str());
	ImGui::SameLine();
	if (ImGui::Button(TR("TOC_INSERT")))
	{
		insert_text(_toc_text + "\n");
		_show_toc = false;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!_toc_multi);
	if (ImGui::Button(TR("TOC_WRITE_FILE")))
	{
		util::write_text(util::combine(_project.dest_root, "SOMMAIRE.md"), _toc_text);
		status(TR("MSG_TOC_WRITTEN"));
		_show_toc = false;
	}
	ImGui::EndDisabled();
	ImGui::BeginChild("##toctext", ImVec2(0, 0), true);
	if (_fonts.mono)
		ImGui::PushFont(_fonts.mono);
	ImGui::TextUnformatted(_toc_text.c_str());
	if (_fonts.mono)
		ImGui::PopFont();
	ImGui::EndChild();
	ImGui::End();
}

//-----------------------------------------------------------------------------
// Log panel at the bottom: the content of the log file, with a filter
//-----------------------------------------------------------------------------

// Log panel: the log file of the application, filtered
void App::draw_log()
{
	// Read the file again when it changed, at most once per second
	const double now = ImGui::GetTime();
	if (now - _log_last > 1.0)
	{
		_log_last = now;
		std::error_code ec;
		const auto time = std::filesystem::last_write_time(std::filesystem::u8path(Logger::path()), ec);
		const long long stamp = ec ? 0 : (long long)time.time_since_epoch().count();
		if (stamp != _log_stamp)
		{
			_log_stamp = stamp;
			_log_buffer.clear();
			_log_offsets.clear();
			std::string text;
			util::read_text(Logger::path(), text);
			std::vector<std::string> lines = util::split_lines(text);
			if (lines.size() > 3000)
				lines.erase(lines.begin(), lines.end() - 3000);
			for (size_t i = 0; i < lines.size(); i++)
			{
				if (i > 0)
					_log_offsets.push_back(_log_buffer.size());
				_log_buffer.append(lines[i].c_str());
				_log_buffer.append("\n");
			}
			_log_scroll = true;
		}
	}

	// The top edge resizes the panel
	ImGui::InvisibleButton("##log_split", ImVec2(ImGui::GetContentRegionAvail().x, 6.0f));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	if (ImGui::IsItemActive())
		_s.log_height = std::max(80.0f, std::min(ImGui::GetMainViewport()->Size.y * 0.8f, _s.log_height - ImGui::GetIO().MouseDelta.y));

	ImGui::BeginChild("##log", ImVec2(0.0f, _s.log_height), true);
	if (ImGui::Button(TR("LOG_CLEAR")))
	{
		Logger::clear();
		_log_buffer.clear();
		_log_offsets.clear();
		_log_last = 0.0;
		_log_stamp = -1;
	}
	ImGui::SameLine();
	_log_filter.Draw(TR("LOG_FILTER"), -ImGui::CalcTextSize(TR("BTN_CLOSE")).x - ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x - 60.0f);
	ImGui::SameLine();
	if (ImGui::Button(TR("BTN_CLOSE")))
		toggle(_s.show_log);
	ImGui::Separator();

	ImGui::BeginChild("##log_text");
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f));
	if (_fonts.mono)
		ImGui::PushFont(_fonts.mono);
	if (_log_filter.IsActive())
	{
		const char* begin = _log_buffer.begin();
		const char* line = begin;
		for (int n = 0; line != nullptr; n++)
		{
			const char* end = (n < _log_offsets.Size) ? begin + _log_offsets[n] : nullptr;
			if (_log_filter.PassFilter(line, end))
				ImGui::TextUnformatted(line, end);
			line = end && end[1] ? end + 1 : nullptr;
		}
	}
	else
		ImGui::TextUnformatted(_log_buffer.begin());
	if (_log_scroll)
		ImGui::SetScrollHereY(1.0f);
	_log_scroll = false;
	if (_fonts.mono)
		ImGui::PopFont();
	ImGui::PopStyleVar();
	ImGui::EndChild();
	ImGui::EndChild();
}
//-----------------------------------------------------------------------------
// Preferences
//-----------------------------------------------------------------------------

// Colours tab of the preferences: the highlight of the current paragraph and the markers of the invisible characters (which is also their key)
void App::draw_prefs_colors()
{
	const float h = ImGui::GetTextLineHeight();
	ImGui::TextUnformatted(TR("PREF_HIGHLIGHT_COLOR"));
	for (int i = 0; i < 6; i++)
	{
		util::Settings t = _s;
		t.accent = i;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + h * 1.6f, p.y + h), accent_color(t, i, _s.dark ? 2 : 0) | IM_COL32(0, 0, 0, 255), 2.0f);
		ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + h * 1.6f, p.y + h), accent_color(t, i, _s.dark ? 3 : 1), 2.0f);
		ImGui::Dummy(ImVec2(h * 1.6f, h));
		ImGui::SameLine();
		if (ImGui::RadioButton(TR(ACCENTS[i]), _s.accent == i)) { _s.accent = i; _s.save(); }
	}
	if (_s.accent >= 5)
	{
		static const char* names[4] = { "PREF_COLOR_LIGHT_BG", "PREF_COLOR_LIGHT_TEXT", "PREF_COLOR_DARK_BG", "PREF_COLOR_DARK_TEXT" };
		ImGui::Indent();
		for (int k = 0; k < 4; k++)
		{
			ImVec4 c = ImGui::ColorConvertU32ToFloat4(_s.accent_custom[k]);
			if (ImGui::ColorEdit3(TR(names[k]), &c.x, ImGuiColorEditFlags_NoInputs))
				_s.accent_custom[k] = ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, 1.0f));
			if (ImGui::IsItemDeactivatedAfterEdit())
				_s.save();
		}
		ImGui::Unindent();
	}
	ImGui::Separator();
	ImGui::TextUnformatted(TR("PREF_INVISIBLE_COLORS"));
	const ImGuiProseColors defaults;
	const ImU32 standard[7] = { defaults.Nbsp, defaults.NarrowNbsp, defaults.ThinSpace, defaults.WideSpace, defaults.ZeroWidth, defaults.SoftHyphen, defaults.Control };
	static const char* kinds[7] = { "INV_NBSP", "INV_NNBSP", "INV_THIN", "INV_WIDE", "INV_ZERO", "INV_SOFT", "INV_CONTROL" };
	for (int i = 0; i < 7; i++)
	{
		ImVec4 c = ImGui::ColorConvertU32ToFloat4(_s.invisible_colors[i] ? _s.invisible_colors[i] : standard[i]);
		ImGui::PushID(i);
		if (ImGui::ColorEdit4(TR(kinds[i]), &c.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
			_s.invisible_colors[i] = ImGui::ColorConvertFloat4ToU32(c);
		if (ImGui::IsItemDeactivatedAfterEdit())
			_s.save();
		ImGui::PopID();
	}
	ImGui::Spacing();
	if (ImGui::Button(TR("PREF_COLORS_RESET")))
	{
		const util::Settings fresh;
		for (int i = 0; i < 4; i++) _s.accent_custom[i] = fresh.accent_custom[i];
		for (int i = 0; i < 7; i++) _s.invisible_colors[i] = 0;
		_s.save();
	}
}

// Preferences window: fonts, sizes, typography, saving, display
void App::draw_prefs()
{
	if (_open_prefs)
	{
		ImGui::OpenPopup(TR("DLG_PREFS_TITLE"));
		_open_prefs = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(TR("DLG_PREFS_TITLE"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	if (ImGui::BeginTabBar("##prefs_tabs"))
	{
	if (ImGui::BeginTabItem(TR("PREF_TAB_GENERAL")))
	{
	if (ImGui::Checkbox(TR("PREF_REOPEN_LAST"), &_s.reopen_last))
		_s.save();
	ImGui::Separator();
	ImGui::TextUnformatted(TR("PREF_HISTORY_SECTION"));
	if (ImGui::Checkbox(TR("PREF_AUTO_SNAPSHOT"), &_s.auto_snapshot))
		_s.save();
	ImGui::Separator();
	ImGui::TextUnformatted(TR("PREF_QUOTE_STYLE"));
	int quotes = _s.quote_style;
	if (ImGui::RadioButton(TR("PREF_QUOTE_FRENCH"), &quotes, 0)) { _s.quote_style = quotes; _s.save(); }
	if (ImGui::RadioButton(TR("PREF_QUOTE_STRAIGHT"), &quotes, 1)) { _s.quote_style = quotes; _s.save(); }
	if (ImGui::RadioButton(TR("PREF_QUOTE_CURLY"), &quotes, 2)) { _s.quote_style = quotes; _s.save(); }
	ImGui::Separator();
	ImGui::TextUnformatted(TR("PREF_TYPO_PROFILE"));
	{
		const std::string name = typography::profile_name();
		if (!typography::has_profile())
			ImGui::TextDisabled("%s", TR("PREF_TYPO_NONE"));
		else
		{
			ImGui::Text("%s%s", name.c_str(), typography::profile_is_user() ? TR("PREF_TYPO_USER") : "");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", typography::profile_path().c_str());
		}
		if (typography::has_profile() && !typography::profile_is_user() && ImGui::Button(TR("PREF_TYPO_COPY")))
		{
			if (typography::create_user_copy())
				status(TR("MSG_TYPO_COPIED"));
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("PREF_TYPO_FOLDER")))
		{
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::u8path(typography::user_folder()), ec);
			dialog::open_folder(typography::user_folder());
		}
		ImGui::SameLine();
		if (ImGui::Button(TR("PREF_TYPO_RELOAD")))
		{
			typography::reload();
			status(TR("MSG_TYPO_RELOADED"));
		}
	}
	ImGui::Separator();
	ImGui::TextUnformatted(TR("PREF_SYSTEM_OPEN"));
	int mode = _s.system_open_mode;
	if (ImGui::RadioButton(TR("PREF_OPEN_READING"), &mode, 0)) { _s.system_open_mode = mode; _s.save(); }
	if (ImGui::RadioButton(TR("PREF_OPEN_PREVIEW"), &mode, 1)) { _s.system_open_mode = mode; _s.save(); }
	if (ImGui::RadioButton(TR("PREF_OPEN_EDIT"), &mode, 2)) { _s.system_open_mode = mode; _s.save(); }
	ImGui::Spacing();
	ImGui::Separator();
	if (ImGui::IsWindowAppearing())
	{
		_pref_ui = _s.ui_font_size;
		_pref_editor = _s.font_size;
	}
	ImGui::SetNextItemWidth(220.0f);
	ImGui::SliderFloat(TR("PREF_UI_FONT"), &_pref_ui, 11.0f, 22.0f, "%.0f");
	if (ImGui::IsItemDeactivatedAfterEdit()) { _s.ui_font_size = _pref_ui; _s.save(); }
	ImGui::SetNextItemWidth(220.0f);
	ImGui::SliderFloat(TR("PREF_EDITOR_FONT"), &_pref_editor, 11.0f, 36.0f, "%.0f");
	if (ImGui::IsItemDeactivatedAfterEdit()) { _s.font_size = _pref_editor; _s.save(); }
	// Fonts: the ones beside the executable, then the ones of the system
	if (_font_files.empty())
		_font_files = util::list_fonts();
	auto font_combo = [&](const char* label, std::string& value)
	{
		std::string shown = util::file_name(value);
		const size_t dot = shown.find_last_of('.');
		if (dot != std::string::npos)
			shown.resize(dot);
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo(label, shown.c_str()))
		{
			for (const util::FontFile& f : _font_files)
				if (ImGui::Selectable(f.name.c_str(), f.path == value || f.name == shown))
				{
					value = f.path;
					_s.save();
				}
			ImGui::EndCombo();
		}
	};
	font_combo(TR("PREF_FONT_UI"), _s.ui_font);
	font_combo(TR("PREF_FONT_EDITOR"), _s.editor_font);
	font_combo(TR("PREF_FONT_MONO"), _s.mono_font);
	{
		// Chinese, Japanese, Korean: automatic (fonts of the system), none, or a font
		std::string shown = _s.cjk_font.empty() ? std::string(TR("FONT_CJK_AUTO")) : _s.cjk_font == "none" ? std::string(TR("FONT_CJK_NONE")) : util::file_name(_s.cjk_font);
		if (!_s.cjk_font.empty() && _s.cjk_font != "none" && shown.find_last_of('.') != std::string::npos)
			shown.resize(shown.find_last_of('.'));
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo(TR("PREF_FONT_CJK"), shown.c_str()))
		{
			if (ImGui::Selectable(TR("FONT_CJK_AUTO"), _s.cjk_font.empty())) { _s.cjk_font.clear(); _s.save(); }
			if (ImGui::Selectable(TR("FONT_CJK_NONE"), _s.cjk_font == "none")) { _s.cjk_font = "none"; _s.save(); }
			for (const util::FontFile& f : _font_files)
				if (ImGui::Selectable(f.name.c_str(), f.path == _s.cjk_font)) { _s.cjk_font = f.path; _s.save(); }
			ImGui::EndCombo();
		}
	}
	ImGui::Separator();
	ImGui::Spacing();
	if (ImGui::Button(TR("PREF_REGISTER")))
	{
		std::string message;
		if (util::register_file_associations(message))
			status(i18n::fmt(TR("MSG_REGISTERED"), message.c_str()));
		else
			status(i18n::fmt(TR("MSG_REGISTER_FAILED"), message.c_str()));
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", TR("PREF_REGISTER_HINT"));
	ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem(TR("PREF_TAB_COLORS")))
	{
		draw_prefs_colors();
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
	}
	ImGui::Spacing();
	ImGui::Separator();
	if (ImGui::Button(TR("BTN_CLOSE")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
//-----------------------------------------------------------------------------
// Statistics window
//-----------------------------------------------------------------------------

// Statistics window: counts of the selection, the document and the project, objectives by chapter
void App::draw_stats()
{
	ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("STATS_TITLE"), &_show_stats, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::End();
		return;
	}
	auto minutes = [](int words) { return std::max(1, (words + 229) / 230); };
	if (ImGui::BeginTable("##stats", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
	{
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 270.0f);
		ImGui::TableSetupColumn(TR("PANE_SOURCE_SHORT"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn(TR("PANE_DESTINATION_SHORT"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn(TR("STATS_DIFFERENCE"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableHeadersRow();
		auto row = [&](const char* label, int a, int b)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
			ImGui::TableNextColumn(); ImGui::Text("%d", a);
			ImGui::TableNextColumn(); ImGui::Text("%d", b);
			ImGui::TableNextColumn(); ImGui::Text("%+d", b - a);
		};
		row(TR("STATS_WORDS"), _stats_src.words, _stats_dst.words);
		row(TR("STATS_CHARS"), _stats_src.chars, _stats_dst.chars);
		row(TR("STATS_CHARS_NO_SPACE"), _stats_src.chars_no_space, _stats_dst.chars_no_space);
		row(TR("STATS_PARAGRAPHS"), _stats_src.paragraphs, _stats_dst.paragraphs);
		row(TR("STATS_LINES"), _stats_src.lines, _stats_dst.lines);
		row(TR("STATS_READING_MINUTES"), minutes(_stats_src.words), minutes(_stats_dst.words));
		ImGui::EndTable();
	}
	// Objectives and progress, then the words of each chapter
	const bool multi = _project.is_folder && _project.docs.size() > 1;
	if (ImGui::IsWindowAppearing() && multi)
		count_project();
	ImGui::Separator();
	ImGui::TextUnformatted(TR("GOAL_TITLE"));
	ImGui::SetNextItemWidth(150.0f);
	if (ImGui::InputInt(TR("GOAL_CHAPTER"), &_goal_chapter, 100, 1000))
	{
		_goal_chapter = std::max(0, _goal_chapter);
		save_goals();
	}
	if (multi)
	{
		ImGui::SetNextItemWidth(150.0f);
		if (ImGui::InputInt(TR("GOAL_PROJECT"), &_goal_total, 1000, 10000))
		{
			_goal_total = std::max(0, _goal_total);
			save_goals();
		}
	}
	auto bar = [](const char* label, long long words, int goal)
	{
		const float frac = goal > 0 ? (float)((double)words / goal) : 0.0f;
		char text[96];
		snprintf(text, sizeof(text), goal > 0 ? "%lld / %d  (%.0f %%)" : "%lld", words, goal, frac * 100.0f);
		ImGui::TextUnformatted(label);
		ImGui::ProgressBar(std::min(frac, 1.0f), ImVec2(-1.0f, 0.0f), text);
	};
	bar(TR("GOAL_CURRENT"), _stats_dst.words, _goal_chapter);
	if (multi)
	{
		if (ImGui::Button(TR("STATS_PROJECT_WORDS")))
			count_project();
		if (_project_words >= 0)
			bar(TR("GOAL_WHOLE_PROJECT"), _project_words, _goal_total);
		if (_chapter_words.size() == _project.docs.size() && ImGui::BeginChild("##chapter_words", ImVec2(0.0f, 150.0f), true))
		{
			for (int i = 0; i < (int)_project.docs.size(); i++)
			{
				const float frac = _goal_chapter > 0 ? std::min(1.0f, (float)_chapter_words[i] / _goal_chapter) : 0.0f;
				char text[64];
				snprintf(text, sizeof(text), "%d", i == _cur ? _stats_dst.words : _chapter_words[i]);
				ImGui::TextUnformatted(_project.docs[i].rel.c_str());
				ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::ProgressBar(_goal_chapter > 0 ? frac : 0.0f, ImVec2(-1.0f, ImGui::GetTextLineHeight()), text);
			}
		}
		if (multi && _chapter_words.size() == _project.docs.size())
			ImGui::EndChild();
		else if (multi)
		{
		}
	}
	ImGui::End();
}
//-----------------------------------------------------------------------------
// Writing style analysis window
//-----------------------------------------------------------------------------

// Style analysis window: index, contributions, observations with advice, highlighted passages
void App::draw_style()
{
	ImGui::SetNextWindowSize(ImVec2(760.0f, 700.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(TR("STYLE_TITLE"), &_show_style))
	{
		ImGui::End();
		return;
	}
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextWrapped("%s", TR("STYLE_DISCLAIMER"));
	ImGui::PopStyleColor();
	ImGui::Spacing();
	int source = _style_source ? 1 : 0;
	bool again = ImGui::RadioButton(TR("PANE_DESTINATION_SHORT"), &source, 0);
	ImGui::SameLine();
	again |= ImGui::RadioButton(TR("PANE_SOURCE_SHORT"), &source, 1);
	_style_source = source == 1;
	ImGui::SameLine();
	if (ImGui::Button(TR("STYLE_ANALYZE")) || again)
	{
		run_style();
		rebuild_spans();
	}
	ImGui::SameLine();
	if (ImGui::Checkbox(TR("STYLE_HIGHLIGHT"), &_style_highlight))
		rebuild_spans();
	ImGui::Separator();
	if (!_style_done)
	{
		ImGui::End();
		return;
	}

	// Indication of the characteristics associated with generated texts (not a verdict)
	{
		const style::Score& sc = _style_report.score;
		const ImVec4 col = sc.value < 25.0 ? ImVec4(0.35f, 0.8f, 0.4f, 1.0f) : sc.value < 50.0 ? ImVec4(0.95f, 0.8f, 0.3f, 1.0f) : sc.value < 75.0 ? ImVec4(0.95f, 0.6f, 0.25f, 1.0f) : ImVec4(0.9f, 0.35f, 0.3f, 1.0f);
		ImGui::TextUnformatted(TR("SCORE_TITLE"));
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, col);
		ImGui::Text("%.0f / 100", sc.value);
		ImGui::SameLine();
		ImGui::TextUnformatted(TR(sc.value < 25.0 ? "SCORE_LEVEL_FEW" : sc.value < 50.0 ? "SCORE_LEVEL_SOME" : sc.value < 75.0 ? "SCORE_LEVEL_MANY" : "SCORE_LEVEL_VERY_MANY"));
		ImGui::PopStyleColor();
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
		ImGui::ProgressBar((float)(sc.value / 100.0), ImVec2(-1.0f, 8.0f), "");
		ImGui::PopStyleColor();
		if (sc.reliability < 2)
			ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1.0f), "%s", TR(sc.reliability == 0 ? "SCORE_RELIABILITY_0" : "SCORE_RELIABILITY_1"));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", TR("SCORE_NOTE"));
		ImGui::PopStyleColor();
		if (ImGui::TreeNode(TR("SCORE_DETAIL")))
		{
			if (ImGui::BeginTable("##score_detail", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
			{
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 380.0f);
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90.0f);
				for (const style::Contribution& c : sc.contributions)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(TR(c.key.c_str()));
					ImGui::TableNextColumn(); ImGui::Text("%.2f", c.value);
					ImGui::TableNextColumn(); ImGui::Text("%.1f / %.0f", c.points, c.max_points);
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
		ImGui::Separator();
	}
	// Colors of the passages in the editor
	{
		static const char* const names[] = { "OBS_KIND_CONNECTOR", "OBS_KIND_STOCK_PHRASE", "OBS_KIND_CLOSE_REPETITION", "OBS_KIND_REPEATED_PHRASE", "OBS_KIND_SAME_OPENING", "OBS_KIND_LONG_SENTENCE", "", "OBS_KIND_REDUNDANT", "OBS_KIND_CONSTRUCTION", "OBS_KIND_PARALLEL", "OBS_KIND_STYLE_BREAK" };
		static const ImVec4 colors[] = { ImVec4(0.27f, 0.51f, 0.9f, 1), ImVec4(0.9f, 0.27f, 0.27f, 1), ImVec4(0.92f, 0.59f, 0.2f, 1), ImVec4(0.65f, 0.35f, 0.86f, 1), ImVec4(0.16f, 0.7f, 0.67f, 1),
			ImVec4(0.88f, 0.78f, 0.2f, 1), ImVec4(0.6f, 0.6f, 0.6f, 1), ImVec4(0.9f, 0.39f, 0.67f, 1), ImVec4(0.94f, 0.35f, 0.24f, 1), ImVec4(0.47f, 0.78f, 0.35f, 1), ImVec4(0.35f, 0.35f, 0.9f, 1) };
		ImGui::TextUnformatted(TR("STYLE_LEGEND"));
		for (int i = 0; i < 11; i++)
		{
			if (names[i][0] == 0)
				continue;
			// Wrap the legend when the next entry does not fit
			if (ImGui::GetItemRectMax().x + ImGui::CalcTextSize(TR(names[i])).x + 40.0f < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
				ImGui::SameLine();
			ImGui::TextColored(colors[i], "\xE2\x96\xA0");
			ImGui::SameLine(0.0f, 2.0f);
			ImGui::TextUnformatted(TR(names[i]));
		}
		ImGui::TextDisabled("%s", TR("STYLE_LEGEND_NOTE"));
	}

	// Advice: the characteristics which weigh most in the indication
	{
		bool any = false;
		for (const style::Contribution& c : _style_report.score.contributions)
			if (c.max_points > 0.0 && c.points >= 0.3 * c.max_points)
			{
				if (!any)
				{
					ImGui::Separator();
					ImGui::TextUnformatted(TR("STYLE_ADVICE"));
					any = true;
				}
				ImGui::Bullet();
				ImGui::TextWrapped("%s", TR((std::string("ADVICE_") + c.key).c_str()));
			}
		if (any)
			ImGui::Separator();
	}
	const style::Metrics& m = _style_report.metrics;
	// A level is a reading help, not a judgment
	auto level = [](double v, double low, double high, bool higher_is_more) -> const char*
	{
		const bool is_low = higher_is_more ? v < low : v > high, is_high = higher_is_more ? v > high : v < low;
		return TR(is_high ? "STYLE_LEVEL_HIGH" : (is_low ? "STYLE_LEVEL_LOW" : "STYLE_LEVEL_MEDIUM"));
	};
	ImGui::TextUnformatted(TR("STYLE_MEASURES"));
	if (ImGui::BeginTable("##style_measures", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
	{
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 440.0f);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110.0f);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		auto row = [&](const char* label, const std::string& value, const char* tag)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
			ImGui::TableNextColumn(); ImGui::TextUnformatted(value.c_str());
			ImGui::TableNextColumn(); ImGui::TextUnformatted(tag);
		};
		char b[64];
		row(TR("STATS_WORDS"), std::to_string(m.words), "");
		row(TR("STYLE_SENTENCES"), std::to_string(m.sentences), "");
		snprintf(b, sizeof(b), "%.1f", m.mean_sentence);
		row(TR("STYLE_MEAN_SENTENCE"), b, "");
		snprintf(b, sizeof(b), "%.2f", m.sentence_variation);
		row(TR("STYLE_SENTENCE_REGULARITY"), b, m.sentences >= 20 ? level(m.sentence_variation, 0.35, 0.55, false) : "");
		snprintf(b, sizeof(b), "%.2f", m.paragraph_variation);
		row(TR("STYLE_PARAGRAPH_REGULARITY"), b, m.paragraphs >= 10 ? level(m.paragraph_variation, 0.35, 0.55, false) : "");
		snprintf(b, sizeof(b), "%.0f %%", m.lexical_diversity * 100.0);
		row(TR("STYLE_LEXICAL_DIVERSITY"), b, m.lexical_diversity > 0.0 ? level(m.lexical_diversity, 0.55, 0.68, true) : "");
		snprintf(b, sizeof(b), "%.1f", m.connectors_per_1000);
		row(TR("STYLE_CONNECTORS"), b, level(m.connectors_per_1000, 4.0, 9.0, true));
		snprintf(b, sizeof(b), "%.1f", m.stock_phrases_per_1000);
		row(TR("STYLE_STOCK_PHRASES"), b, level(m.stock_phrases_per_1000, 0.8, 2.0, true));
		row(TR("STYLE_LONG_SENTENCES"), std::to_string(m.long_sentences), "");
		row(TR("STYLE_ENUMERATIONS"), std::to_string(m.enumerations), "");
		snprintf(b, sizeof(b), "%.2f", m.type_token_ratio);
		row(TR("STYLE_TTR"), b, "");
		snprintf(b, sizeof(b), "%.0f %%", m.hapax_ratio * 100.0);
		row(TR("STYLE_HAPAX"), b, "");
		snprintf(b, sizeof(b), "%.0f", m.mtld);
		row(TR("STYLE_MTLD"), b, "");
		snprintf(b, sizeof(b), "%.2f", m.word_entropy);
		row(TR("STYLE_WORD_ENTROPY"), b, "");
		snprintf(b, sizeof(b), "%.2f", m.opening_entropy);
		row(TR("STYLE_OPENING_ENTROPY"), b, "");
		snprintf(b, sizeof(b), "%.2f", m.zipf_slope);
		row(TR("STYLE_ZIPF"), b, "");
		snprintf(b, sizeof(b), "%.2f (%.2f)", m.mean_word_length, m.word_length_variation);
		row(TR("STYLE_WORD_LENGTH"), b, "");
		snprintf(b, sizeof(b), "%.1f  %.1f  %.1f  %.1f  %.1f", m.colons, m.semicolons, m.parentheses, m.dashes, m.exclamations);
		row(TR("STYLE_PUNCTUATION"), b, "");
		snprintf(b, sizeof(b), "%.0f %%", m.transition_paragraphs * 100.0);
		row(TR("STYLE_TRANSITION_PARAGRAPHS"), b, "");
		row(TR("STYLE_REDUNDANT_PAIRS"), std::to_string(m.redundant_pairs), "");
			row(TR("STYLE_CONSTRUCTIONS"), std::to_string(m.constructions), "");
		snprintf(b, sizeof(b), "%.2f", m.function_word_overdispersion);
		row(TR("STYLE_FUNCTION_WORD_SPREAD"), b, m.function_word_overdispersion > 0.0 ? level(m.function_word_overdispersion, 1.15, 1.6, true) : "");
		snprintf(b, sizeof(b), "%.0f  %.0f  %.0f  %.0f  %.0f", m.determiners, m.prepositions, m.conjunctions, m.auxiliaries, m.pronouns);
		row(TR("STYLE_FUNCTION_CATEGORIES"), b, "");
		snprintf(b, sizeof(b), "%.0f", m.perplexity);
		row(TR("STYLE_PERPLEXITY"), b, "");
		snprintf(b, sizeof(b), "%.0f %%", m.predictable_share * 100.0);
		row(TR("STYLE_PREDICTABLE"), b, level(m.predictable_share, 0.15, 0.3, true));
		snprintf(b, sizeof(b), "%.2f", m.compression_ratio);
		row(TR("STYLE_COMPRESSION"), b, "");
		if (m.burstiness_words >= 8)
		{
			snprintf(b, sizeof(b), "%+.2f", m.word_burstiness);
			row(TR("STYLE_BURSTINESS"), b, "");
		}
		snprintf(b, sizeof(b), "%.0f %%", m.parallel_share * 100.0);
		row(TR("STYLE_PARALLEL_SHARE"), b, "");
		snprintf(b, sizeof(b), "%.0f  /  %.1f", m.flesch, m.fog);
		row(TR("STYLE_READABILITY"), b, "");
		snprintf(b, sizeof(b), "%.1f", m.readability_variation);
		row(TR("STYLE_READABILITY_SPREAD"), b, "");
		snprintf(b, sizeof(b), "%.2f", m.rhythm_entropy);
		row(TR("STYLE_RHYTHM"), b, "");
		snprintf(b, sizeof(b), "%.0f %%", m.passive_share * 100.0);
		row(TR("STYLE_PASSIVE"), b, "");
		ImGui::EndTable();
	}

	ImGui::Spacing();
	draw_style_extra();
	ImGui::TextUnformatted(TR("STYLE_OBSERVATIONS"));
	ImGui::BeginChild("##style_obs", ImVec2(0, 0), true);
	if (_style_report.observations.empty())
		ImGui::TextDisabled("%s", TR("STYLE_NONE"));
	Pane& pane = _style_source ? _src : _dst;
	int id = 0;
	for (const style::Observation& o : _style_report.observations)
	{
		const char* key = o.kind == style::Connector ? "OBS_CONNECTOR" : o.kind == style::StockPhrase ? "OBS_STOCK_PHRASE" : o.kind == style::CloseRepetition ? "OBS_CLOSE_REPETITION"
			: o.kind == style::RepeatedPhrase ? "OBS_REPEATED_PHRASE" : o.kind == style::SameOpening ? "OBS_SAME_OPENING" : o.kind == style::LongSentence ? "OBS_LONG_SENTENCE" : o.kind == style::RedundantParagraphs ? "OBS_REDUNDANT_PARAGRAPHS" : o.kind == style::ConstructionFamily ? "OBS_CONSTRUCTION" : o.kind == style::ParallelStructure ? "OBS_PARALLEL_STRUCTURE" : o.kind == style::StyleBreak ? "OBS_STYLE_BREAK" : "OBS_ENUMERATION";
		const std::string title = i18n::fmt(TR(key), o.text.c_str(), o.count);
		const std::string obs_id = "###obs" + std::to_string(id++);
		static const ImVec4 kind_colors[] = { ImVec4(0.27f, 0.51f, 0.9f, 1), ImVec4(0.9f, 0.27f, 0.27f, 1), ImVec4(0.92f, 0.59f, 0.2f, 1), ImVec4(0.65f, 0.35f, 0.86f, 1), ImVec4(0.16f, 0.7f, 0.67f, 1),
			ImVec4(0.88f, 0.78f, 0.2f, 1), ImVec4(0.6f, 0.6f, 0.6f, 1), ImVec4(0.9f, 0.39f, 0.67f, 1), ImVec4(0.94f, 0.35f, 0.24f, 1), ImVec4(0.47f, 0.78f, 0.35f, 1), ImVec4(0.35f, 0.35f, 0.9f, 1) };
		ImGui::TextColored(kind_colors[std::min(o.kind, 10)], "\xE2\x96\xA0");
		ImGui::SameLine(0.0f, 4.0f);
		const style::Contribution* contribution = style_points(o.kind);
		const std::string tag = contribution ? i18n::fmt(TR("OBS_POINTS"), contribution->points, contribution->max_points) : std::string();
		if (ImGui::TreeNode((title + "  " + tag + obs_id).c_str()))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", style_advice(o).c_str());
			ImGui::PopStyleColor();
			for (size_t i = 0; i < o.passages.size(); i++)
		
			{
				const style::Passage& p = o.passages[i];
				int len = 0;
				const char* s = ImGuiProse::GetLine(pane.editor, p.line, &len);
				// A few words around the passage
				int from = std::max(0, p.start - 25), to = std::min(len, p.end + 25);
				while (from > 0 && from < len && ((unsigned char)s[from] & 0xC0) == 0x80) from--;
				while (to < len && ((unsigned char)s[to] & 0xC0) == 0x80) to++;
				const std::string text = std::string(from > 0 ? "..." : "") + std::string(s + from, to - from) + (to < len ? "..." : "");
				const std::string item = "l." + std::to_string(p.line) + "  " + text + "##p" + std::to_string(id) + "_" + std::to_string(i);
				if (ImGui::Selectable(item.c_str()))
				{
					_active = &pane;
					ImGuiProse::SetSelection(pane.editor, ImGuiProsePos(p.line, p.start), ImGuiProsePos(p.line, std::min(p.end, len)));
					ImGuiProse::SetCaret(pane.editor, ImGuiProsePos(p.line, std::min(p.end, len)));
					ImGuiProse::ScrollToLine(pane.editor, p.line);
				}
			}
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}