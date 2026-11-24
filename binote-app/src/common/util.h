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

// Binote - paths, files, settings

#pragma once
#ifndef _UTIL_H
#define _UTIL_H

#include <string>
#include <vector>

namespace util
{
	// Folder of the executable (resources are beside it) and of the user data (settings, history)
	std::string application_path();
	std::string data_path();
	std::string combine(const std::string& left, const std::string& right);
	std::string file_name(const std::string& path);
	std::string parent_path(const std::string& path);
	std::string extension(const std::string& path);			// lower case, with the dot
	bool exists(const std::string& path);
	bool is_directory(const std::string& path);

	// A text file as UTF-8 with LF line breaks. UTF-8 (BOM or not) or Windows-1252 are read. crlf tells the original line breaks.
	bool read_text(const std::string& path, std::string& text, bool* crlf = nullptr);
	// UTF-8 without BOM, line breaks as asked. The folders are created.
	bool write_text(const std::string& path, const std::string& text, bool crlf = false);

	// "chapter 2" before "chapter 10"
	bool natural_less(const std::string& a, const std::string& b);

	std::vector<std::string> split_lines(const std::string& text);

	// Fonts which can be chosen: the ones beside the executable, then the ones of the system
	struct FontFile { std::string name; std::string path; };
	std::vector<FontFile> list_fonts();

	// Full path of the executable
	std::string application_full_path();
	// Let the system open .md, .markdown and .txt files with Binote (Windows: the Open with list, in the registry of the user; Linux: a desktop entry).
	// message: what was done, or the error.
	bool register_file_associations(std::string& message);

	// Recently opened projects
	struct Recent
	{
		std::string source;							// a file or a folder
		std::string destination;
		std::string last_doc;						// chapter open when the project was closed
		int last_line = 0;
		std::string language;						// language of the text of a single file project							// caret line of the destination in it
	};

	// Settings, saved in the data folder (settings.ini)
	struct Settings
	{
		bool dark = true;
		bool wrap = true;
		bool markdown = true;
		bool line_numbers = true;
		bool paragraph_numbers = true;
		bool show_diff = true;
		bool align_paragraphs = true;				// empty (not editable) space in the shorter pane so facing paragraphs start at the same height
		bool sync_scroll = true;
		bool show_tree = true;
		bool show_log = false;
		bool spell_check = true;
		bool grammar_check = true;
		std::string grammar_ignored;				// rules which are switched off, separated by commas
		bool python_seen = false;					// the notice about Python was shown at the first start
		bool python_never = false;					// the user does not want to be reminded that Python is missing
		bool reopen_last = true;					// open the last project at start
		int export_format = 0;						// PDF, Word, EPUB, HTML, Markdown
		std::string export_author;
		int quote_style = 0;						// quotation marks typed with the key ": 0 guillemets, 1 straight, 2 curly
		int system_open_mode = 1;					// file opened by the system: 0 reading (text), 1 reading (preview), 2 normal (source + destination)
		float log_height = 200.0f;
		bool reading_mode = false;
	bool dest_only = false;
		bool autosave_on_switch = true;
		bool auto_snapshot = false;
		bool show_nbsp = true;
		bool show_invisible = false;
		bool auto_typography = true;
		bool preview_source = false;
		bool preview_destination = false;
		int highlight_mode = 3;						// 0 none, 1 background, 2 text, 3 both
		int accent = 0;								// 0-4 the colours which are provided, 5 the colours of accent_custom
		unsigned accent_custom[4] = { 0xFFB0F3FFu, 0xFF005C7Au, 0xFF12404Au, 0xFF4DD8FFu };		// light background, light text, dark background, dark text
		unsigned invisible_colors[7] = { 0, 0, 0, 0, 0, 0, 0 };		// markers of the invisible characters, 0 = the default colour
		float font_size = 17.0f;					// text of the editors and of the preview
		float ui_font_size = 16.0f;
		std::string cjk_font;						// fallback font for Chinese, Japanese, Korean: empty = the ones of the system, "none" = off, else a file
		std::string charmap_font;						// font of the table of characters: a file of the fonts folder, or a full path; empty = the font of the interface
		std::string ui_font = "Roboto-Medium.ttf";			// a file of the fonts folder, or a full path
		std::string editor_font = "DejaVuSans.ttf";
		std::string mono_font = "DejaVuSansMono.ttf";					// menus, buttons, dialogs
		float split = 0.5f;							// share of the source pane
		int window_x = -32000, window_y = -32000;
		bool maximized = false;
		bool find_case = false;
		bool history_only_changes = true;
		int goto_kind = 0;
		std::string language;						// code of a file of the lang folder, empty = language of the system
		bool mono_editor = false;					// monospaced font in the editors (plain text)
		float tree_width = 260.0f;
		int window_w = 1400, window_h = 850;
		std::vector<Recent> recents;

		void load();
		void save() const;
		void add_recent(const std::string& source, const std::string& destination);
		Recent* find_recent(const std::string& source);
	};
}

#endif
