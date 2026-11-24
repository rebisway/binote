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

// Binote - the application: documents, panes, dialogs
//
// Index of this file:
// [SECTION] Small types: TextStats, Fonts
// [SECTION] class App, public interface (used by main.cpp and the test bridge)
// [SECTION] App, private: panes, layout and synchronised scrolling
// [SECTION] App, private: documents and project (app.cpp)
// [SECTION] App, private: dialogs, find and replace, history, table of contents
// [SECTION] App, private: spelling, grammar and the list of problems
// [SECTION] App, private: style analysis
// [SECTION] App, private: Python scripts (app_script.cpp)
// [SECTION] App, private: notes, names, alignment pins, version control, export
// [SECTION] App, private: interface (app_ui.cpp)
//
// The implementation is spread over several files, one per feature: app.cpp (documents, editing, test commands),
// app_ui.cpp (menus, panes, dialogs), app_script.cpp, app_problems.cpp, app_notes.cpp, app_names.cpp,
// app_style_extra.cpp, app_charmap.cpp, app_export.cpp, app_vcs.cpp.

#pragma once
#ifndef _APP_H
#define _APP_H

#include "project/project.h"
#include "analysis/spell.h"
#include "script/pyscript.h"
#include "project/search.h"
class TextEditor;
#include "analysis/grammar.h"
#include "analysis/style.h"
#include "vcs/vcs.h"
#include "project/text.h"
#include "common/util.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <imgui.h>
#include "imgui_md.h"
#include "imgui_md_diagram.h"
#include "imgui_prose.h"

struct GLFWwindow;

//-----------------------------------------------------------------------------
// [SECTION] Small types
//-----------------------------------------------------------------------------

// Counts of a text
struct TextStats
{
	int words = 0;
	int chars = 0;							// characters (code points)
	int chars_no_space = 0;
	int paragraphs = 0;
	int lines = 0;
};

// The fonts built by main.cpp; the editor panes use regular/bold/italic/mono, the rest of the interface uses ui
struct Fonts
{
	ImFont* ui = nullptr;					// menus, buttons, dialogs
	ImFont* regular = nullptr;
	ImFont* bold = nullptr;
	ImFont* italic = nullptr;
	ImFont* bold_italic = nullptr;
	ImFont* mono = nullptr;
	ImFont* charmap = nullptr;				// table of characters, null = the font of the interface
	ImFont* heading[3] = {};
};

//-----------------------------------------------------------------------------
// [SECTION] class App, public interface
//-----------------------------------------------------------------------------

// The whole application. The window loop of main.cpp calls frame() once per frame; everything else is internal.
// A document is a pair of text panes: the source (original, read-only unless edited on purpose) and the destination (the rewrite).
class App
{
public:
	App(GLFWwindow* window);
	~App();

	void set_fonts(const Fonts& fonts)		{ _fonts = fonts; }
	util::Settings& settings()				{ return _s; }
	bool font_size_changed();				// true once when the font size setting changed: the host rebuilds the fonts
	float font_size() const					{ return _s.font_size; }
	float ui_font_size() const				{ return _s.ui_font_size; }
	void save_state();						// positions, last chapter, settings: at exit

	void frame();							// builds the whole interface

	// Reading mode: the original only. A file opened by the system can impose it without changing the saved setting.
	bool reading() const					{ return _s.reading_mode || _force_reading; }
	void set_reading(bool on)				{ _force_reading = false; _s.reading_mode = on; if (on) _s.dest_only = false; _s.save(); }
	void set_dest_only(bool on)				{ _s.dest_only = on; if (on) { _force_reading = false; _s.reading_mode = false; } _s.save(); }
	// A file given by the system (double click, Open with): reading, preview or normal editing, according to the preferences
	bool open_from_system(const std::string& path);
	bool quit_requested() const				{ return _quit_requested; }		// the user chose to quit in the confirmation box
	bool can_close();					// false while a confirmation is shown (the window stays open)
	void shutdown();

	// Commands for the test bridge (the generic ones are in bridge.cpp); false if unknown
	bool command(const std::vector<std::string>& args);

private:
	//-------------------------------------------------------------------------
	// [SECTION] App, private: panes, layout and synchronised scrolling
	//-------------------------------------------------------------------------

	// One side of the double view: an editor, and the Markdown preview which can replace it
	struct Pane
	{
		ImGuiProseEditor* editor = nullptr;
		ImGuiProseConfig config;
		ImGuiMdDocument* md = nullptr;
		bool preview = false;
		int md_version = -1;
		double md_due = -1.0;
		ImGuiMdState md_state;
		bool is_source = false;
	};

	GLFWwindow* _window;
	util::Settings _s;						// user preferences (settings.ini)
	Fonts _fonts;
	float _applied_font_size = 0.0f;
	float _applied_ui_font_size = 0.0f;
	std::string _applied_fonts;
	std::vector<util::FontFile> _font_files;
	float _pref_ui = 0.0f, _pref_editor = 0.0f;
	bool _applied_dark = true;
	bool _theme_set = false;

	Pane _src, _dst;
	Pane* _active = nullptr;				// the pane which has the focus
	ImGuiMdDiagramCache* _diagrams = nullptr;
	bool _split_dragging = false;
	bool _force_reading = false;
	bool _edit_source = false;				// reading mode: the original can be edited and saved in place
	bool _src_dirty = false;
	Pane& style_pane()						{ return _style_source ? _src : _dst; }
	Pane* outline_pane()					{ return reading() ? &_src : &_dst; }

	// Analysis, redone a moment after the last change
	double _diff_due = -1.0, _outline_due = -1.0, _snapshot_due = 0.0;
	int _seen_src_version = -1, _seen_dst_version = -1;
	diff::Result _align;						// the alignment of the paragraphs, always computed (scroll together)
	diff::Result _diff;
	std::vector<Heading> _headings;
	int _current_para = 0;					// paragraph of the active pane
	int _last_para = 0;
	int highlight_para(const Pane* pane) const;	// the paragraph of that pane which faces it (they differ when paragraphs were added, removed or pinned)
	bool _sync_scroll_pending = false;
	ImGuiProseScrollAnchor _prev_src_anchor, _prev_dst_anchor;		// to see a pane which was scrolled by the program (go to a line, open a chapter)
	int _prev_src_version = -1, _prev_dst_version = -1;
	int _sync_settle = 0;											// frames to wait after the program moved a pane
	void update_spacers();					// blank lines which keep the paragraphs facing each other
	void configure_pane(Pane& pane);
	void apply_theme();
	void rebuild_spans();					// squiggles (spelling, grammar), style marks, search matches
	static void typed_callback(ImGuiProseEditor* editor, const char* typed, void* user_data);	// French typography while typing

	//-------------------------------------------------------------------------
	// [SECTION] App, private: documents and project (app.cpp)
	//-------------------------------------------------------------------------

	Project _project;
	bool _has_project = false;
	int _cur = -1;							// current document
	std::string _title;
	bool _quit_requested = false;
	std::string _status;					// message of the status bar
	double _status_until = 0.0;

	bool open_file_path(const std::string& path, const std::string& destination = "");
	bool open_folder_path(const std::string& path, const std::string& destination = "");
	void set_project(Project&& p);
	bool open_doc(int index, int line = 0);
	void request_open_doc(int index, int line = 0);
	// Links of the text (app_links.cpp): a click in the preview or a Ctrl+click in the editor
	void queue_link(const std::string& url, const std::vector<std::string>& folders);
	void open_pending_link();
	void follow_link(std::string url, const std::vector<std::string>& folders);
	static std::string link_in_line(const char* line, int length, int column);
	void guard(std::function<void()> action);	// runs the action, after saving or discarding the changes of the current document
	bool save();
	bool merge_files(const std::string& path);			// all the chapters (destination, else source) in one file
	bool save_as(const std::string& path);
	bool dirty() const;
	void reset_dest_from_source();
	void reload_current();
	void refresh_headings();
	void set_project_language(const std::string& code);
	bool french() const						{ return !_has_project || _project.language.rfind("fr", 0) == 0; }
	bool is_markdown() const				{ return _s.markdown; }
	void remember_position();
	void status(const std::string& text);
	Doc* current_doc()						{ return (_has_project && _cur >= 0 && _cur < (int)_project.docs.size()) ? &_project.docs[_cur] : nullptr; }
	std::string editor_text(ImGuiProseEditor* editor);
	std::vector<std::string> editor_lines(ImGuiProseEditor* editor);
	std::vector<std::string> paragraphs(ImGuiProseEditor* editor);

	// Editing helpers
	void fix_typography(bool whole, bool simplify = false);
	void insert_text(const std::string& s);
	void go_to(int kind, int value);
	void move_paragraph(int delta);
	void jump_diff(int dir);

	// Counts: selection, document and project; objectives in words
	TextStats _stats_src, _stats_dst;
	double _stats_due = -1.0;
	bool _show_stats = false;
	long long _project_words = -1;
	std::vector<int> _chapter_words;					// words of each chapter (destination), see count_project
	int _goal_chapter = 0, _goal_total = 0;				// objectives in words, 0 = none
	long long _sel_key = -1;
	int _sel_words = 0;
	int _sel_chars = 0;
	void count_project();
	void load_goals();
	void save_goals();

	//-------------------------------------------------------------------------
	// [SECTION] App, private: dialogs, find and replace, history, table of contents
	//-------------------------------------------------------------------------

	// Confirmation box, about, go to, destination folder, preferences
	std::function<void()> _after_confirm;
	std::string _link_url;						// a link which was clicked, followed at the start of the next frame
	std::vector<std::string> _link_folders;		// where its relative address is looked for
	std::string _confirm_text;
	bool _open_confirm = false;
	bool _open_about = false;
	bool _open_goto = false;
	int _goto_value = 1;
	int _goto_kind = 0;
	bool _open_dest = false;
	std::string _dest_input;
	std::string _dest_folder;
	bool _open_prefs = false;
	void draw_about();

	// Find and replace (search.*): the engine works on UTF-8 lines, the matches are kept for the squiggles
	bool _show_find = false;
	bool _find_focus = false;
	char _find_text[256] = "";
	char _replace_text[256] = "";
	bool _find_case = false;
	bool _find_regex = false;
	bool _find_word = false;
	int _find_field = 0;						// the box which was edited last: 0 find, 1 replace (the special characters go there)
	std::string _find_error;
	search::Engine _engine;
	bool _find_all_chapters = false;
	struct Hit { int doc; int line; std::string text; };
	std::vector<Hit> _hits;
	std::string _hits_for;
	struct FindMatch { int line = 1, column = 0, end_line = 1, end_column = 0; };	// in the active editor (bytes)
	std::vector<FindMatch> _matches;
	void update_matches();
	bool prepare_search();
	void select_match(const FindMatch& m);
	void replace_in_editor(bool all);
	void replace_in_project();
	void find_next(int dir);
	void search_all();

	// Table of characters
	bool _show_charmap = false;
	int _charmap_group = 1;
	std::vector<unsigned int> _charmap_recent;
	char _charmap_code[16] = "";
	void draw_charmap();

	// History (snapshots of the destination) and table of contents
	bool _show_history = false;
	std::vector<History::Snapshot> _snapshots;
	int _snapshot_selected = -1;
	std::vector<diff::Line> _snapshot_diff;
	bool _snapshot_only_changes = true;
	bool _show_toc = false;
	std::string _toc_text;
	bool _toc_multi = false;
	void generate_toc();

	// Log panel (the log file of the Logger, read again when it changed)
	ImGuiTextBuffer _log_buffer;
	ImGuiTextFilter _log_filter;
	ImVector<int> _log_offsets;
	bool _log_scroll = false;
	double _log_last = 0.0;
	long long _log_stamp = 0;

	//-------------------------------------------------------------------------
	// [SECTION] App, private: spelling, grammar and the list of problems
	//-------------------------------------------------------------------------

	Spell _spell;
	bool _open_spell_menu = false;			// context menu on a misspelled word
	std::string _spell_word;
	int _spell_line = 0, _spell_start = 0, _spell_end = 0;
	std::vector<std::string> _spell_suggestions;

	// Problems of the destination (app_problems.cpp): one row per misspelled word or grammar issue, with its line
	struct Problem { int line = 1, start = 0, end = 0, kind = 0; std::string text, detail, message; };		// kind: 0 spelling, 1 grammar
	std::vector<Problem> _problems;
	int _problems_version = -1;
	double _problems_due = -1.0;
	bool _show_problems = false;
	void toggle_spell();
	void toggle_grammar();
	void draw_project_languages();
	void collect_problems();
	void draw_problems();

	// Grammar (grammar.*): the issues of the lines of the destination, kept by text so only the lines which changed are checked again
	grammar::Engine _grammar;
	lexicon::Lexicon _lexicon_fr;
	bool _lexicon_fr_tried = false;
	std::string _grammar_key;						// language and ignored rules the cache was made for
	std::unordered_map<std::string, std::vector<grammar::Issue>> _grammar_cache;
	std::vector<grammar::Issue> grammar_issues(const std::string& line);
	bool _open_grammar_menu = false;
	grammar::Issue _gram_issue;
	int _gram_line = 0;
	std::string _gram_message;
	void apply_grammar_menu_choice();

	//-------------------------------------------------------------------------
	// [SECTION] App, private: style analysis
	//-------------------------------------------------------------------------

	bool _show_style = false;
	bool _style_source = false;				// analyse the source instead of the destination
	bool _style_done = false;
	style::Report _style_report;
	bool _style_highlight = true;
	struct StyleMark { int line, start, end, obs; };	// a passage coloured in the editor, with the observation it belongs to
	std::vector<StyleMark> _style_marks;
	std::string style_advice(const style::Observation& o);
	const style::Contribution* style_points(int kind);
	void run_style();
	void draw_style();
	// Comparison source / destination and evolution of the index (app_style_extra.cpp)
	style::Report _style_cmp_src, _style_cmp_dst;
	bool _style_cmp_done = false;
	std::vector<float> _style_timeline;
	std::vector<std::string> _style_timeline_labels;
	void draw_style_extra();

	//-------------------------------------------------------------------------
	// [SECTION] App, private: Python scripts (app_script.cpp)
	//-------------------------------------------------------------------------

	void draw_script_menu();
	void draw_script();
	void draw_script_editor();
	void start_script(const std::string& path);
	void run_script_now();
	void apply_script(const pyscript::Result& result);
	std::string script_context();
	void load_script_in_editor(const std::string& path);
	void free_script_editor();
	bool save_script_in_editor();
	std::vector<pyscript::Info> _scripts;
	bool _scripts_loaded = false;
	pyscript::Session _script;
	bool _script_active = false;					// the dialog box of a script is open
	bool _script_ok = false;						// bridge: validate the dialog box
	std::string _script_path;
	struct ScriptRange { ImGuiProsePos start, end; bool valid = false; };	// what a script can work on, from the smallest to the whole file
	ScriptRange _script_selection, _script_paragraph, _script_section, _script_file;
	bool _show_script_editor = false;
	TextEditor* _script_text = nullptr;
	std::string _script_edit_path;
	bool _script_edit_user = false, _script_edit_dirty = false, _script_edit_dark = true;
	int _script_name_mode = 0;						// 1 new, 2 copy: name being typed
	char _script_name[64] = "";
	std::string _script_message;
	std::vector<diff::Line> _script_preview;			// what the script would change (diff of the lines)
	int _script_preview_changes = -1;

	//-------------------------------------------------------------------------
	// [SECTION] App, private: notes, names, alignment pins, version control, export
	//-------------------------------------------------------------------------

	// Proper names and their variants (app_names.cpp)
	struct NameCount { std::string name; int count; int chapters; };
	std::vector<NameCount> _names;
	std::vector<std::pair<int, int>> _name_variants;
	bool _show_names = false, _names_scanned = false;
	void scan_names();
	void draw_names();

	// Notes on paragraphs (app_notes.cpp, file binote.notes): anchored by the text of the paragraph, so they follow it when it moves
	struct Note { std::string doc, anchor; int para; std::string text; };
	std::vector<Note> _notes;
	std::vector<std::pair<int, int>> _note_paras;		// (paragraph, index of the note) in the current document
	bool _show_notes = false, _open_note = false;
	int _note_edit = -1, _note_target_para = 0;
	char _note_text[1024] = "";
	std::string notes_path() const;
	void load_notes();
	void save_notes();
	void locate_notes();
	void add_note_here();
	void draw_notes();

	// Alignment pins (binote.align): paragraphs of the source and of the destination declared to face each other
	struct Pin { std::string doc, src_anchor, dst_anchor; int src_para, dst_para; };
	std::vector<Pin> _pins;
	std::string pins_path() const;
	void load_pins();
	void save_pins();
	std::vector<std::pair<int, int>> locate_pins(const std::vector<std::string>& src, const std::vector<std::string>& dst);
	void pin_here();
	void apply_pin(int src_para, int dst_para);
	bool _open_pin = false, _pin_from_src = false;
	int _pin_para = 1, _pin_value = 1;
	void clear_pins();

	// Git / Subversion (app_vcs.cpp)
	bool _show_vcs = false;
	vcs::Info _vcs;
	std::vector<vcs::Change> _vcs_changes;
	char _vcs_message[256] = "";
	std::string _vcs_output;
	void draw_vcs();
	void vcs_refresh();

	// Export (app_export.cpp)
	bool _open_export = false;
	char _export_title[128] = "";
	std::string _export_path, _export_message;
	bool _export_title_page = true, _export_page_break = true, _export_source = false;
	void draw_export();

	//-------------------------------------------------------------------------
	// [SECTION] App, private: interface (app_ui.cpp)
	//-------------------------------------------------------------------------

	void draw_menu();
	void draw_toolbar();
	void draw_body();
	void draw_tree(float width, float height);
	void draw_pane(Pane& pane, const char* id, float width, float height);
	void draw_status();
	void draw_log();
	void draw_stats();
	void update_stats();
	void spell_menu_at(Pane& pane, ImVec2 screen_pos);
	int selection_words();
	void draw_prefs();
	void draw_prefs_colors();
	void draw_dialogs();
	void draw_find();
	void draw_history();
	void draw_toc();
	void handle_shortcuts();
	void toggle(bool& value)				{ value = !value; _s.save(); }
};

#endif
