// Binote - the application: documents, panes, dialogs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#ifndef _APP_H
#define _APP_H

#include "project.h"
#include "spell.h"
#include "pyscript.h"
#include "search.h"
class TextEditor;
#include "grammar.h"
#include "style.h"
#include "vcs.h"
#include "text.h"
#include "util.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <imgui.h>
#include "imgui_md.h"
#include "imgui_md_diagram.h"
#include "imgui_prose.h"

struct GLFWwindow;

// Counts of a text
struct TextStats
{
	int words = 0;
	int chars = 0;							// characters (code points)
	int chars_no_space = 0;
	int paragraphs = 0;
	int lines = 0;
};

struct Fonts
{
	ImFont* ui = nullptr;					// menus, buttons, dialogs
	ImFont* regular = nullptr;
	ImFont* bold = nullptr;
	ImFont* italic = nullptr;
	ImFont* bold_italic = nullptr;
	ImFont* mono = nullptr;
	ImFont* heading[3] = {};
};

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
	util::Settings _s;
	Fonts _fonts;
	float _applied_font_size = 0.0f;
	float _applied_ui_font_size = 0.0f;
	std::string _applied_fonts;
	std::vector<util::FontFile> _font_files;
	float _pref_ui = 0.0f, _pref_editor = 0.0f;
	bool _applied_dark = true;
	bool _theme_set = false;

	Project _project;
	bool _has_project = false;
	int _cur = -1;							// current document
	Pane _src, _dst;
	Pane* _active = nullptr;
	ImGuiMdDiagramCache* _diagrams = nullptr;
	bool _split_dragging = false;

	// Analysis, redone a moment after the last change
	double _diff_due = -1.0, _outline_due = -1.0, _snapshot_due = 0.0;
	int _seen_src_version = -1, _seen_dst_version = -1;
	diff::Result _align;						// the alignment of the paragraphs, always computed (scroll together)
	diff::Result _diff;
	std::vector<Heading> _headings;
	int _current_para = 0;					// paragraph of the active pane
	int highlight_para(const Pane* pane) const;	// the paragraph of that pane which faces it (they differ when paragraphs were added, removed or pinned)
	int _last_para = 0;
	bool _sync_scroll_pending = false;
	ImGuiProseScrollAnchor _prev_src_anchor, _prev_dst_anchor;		// to see a pane which was scrolled by the program (go to a line, open a chapter)
	int _prev_src_version = -1, _prev_dst_version = -1;
	int _sync_settle = 0;											// frames to wait after the program moved a pane

	// Dialogs and messages
	std::string _status;
	double _status_until = 0.0;
	std::function<void()> _after_confirm;
	std::string _confirm_text;
	bool _open_confirm = false;
	bool _open_about = false;
	void draw_about();
	bool _open_goto = false;
	int _goto_value = 1;
	int _goto_kind = 0;
	bool _open_dest = false;
	std::string _dest_input;
	std::string _dest_folder;
	bool _show_find = false;
	bool _show_charmap = false;
	bool _show_vcs = false;
	vcs::Info _vcs;
	std::vector<vcs::Change> _vcs_changes;
	char _vcs_message[256] = "";
	std::string _vcs_output;
	bool _open_export = false;
	char _export_title[128] = "";
	std::string _export_path, _export_message;
	bool _export_title_page = true, _export_page_break = true, _export_source = false;
	int _charmap_group = 1;
	std::vector<unsigned int> _charmap_recent;
	char _charmap_code[16] = "";
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
	bool _show_history = false;
	std::vector<History::Snapshot> _snapshots;
	int _snapshot_selected = -1;
	std::vector<diff::Line> _snapshot_diff;
	bool _snapshot_only_changes = true;
	bool _show_toc = false;
	std::string _toc_text;
	bool _toc_multi = false;
	bool _quit_requested = false;
	std::string _title;
	Spell _spell;
	bool _show_style = false;
	bool _style_source = false;
	bool _style_done = false;
	style::Report _style_report;
	bool _style_highlight = true;
	struct StyleMark { int line, start, end, obs; };
	std::vector<StyleMark> _style_marks;
	Pane& style_pane()						{ return _style_source ? _src : _dst; }
	std::string style_advice(const style::Observation& o);
	const style::Contribution* style_points(int kind);
	void run_style();
	void draw_style();
	bool _open_spell_menu = false;
	std::string _spell_word;
	int _spell_line = 0, _spell_start = 0, _spell_end = 0;
	std::vector<std::string> _spell_suggestions;
	TextStats _stats_src, _stats_dst;
	double _stats_due = -1.0;
	bool _show_stats = false;
	long long _project_words = -1;
	std::vector<int> _chapter_words;					// words of each chapter (destination), see count_project
	int _goal_chapter = 0, _goal_total = 0;				// objectives in words, 0 = none
	void count_project();
	void load_goals();
	void save_goals();
	long long _sel_key = -1;
	int _sel_words = 0;
	int _sel_chars = 0;
	bool _force_reading = false;
	bool _edit_source = false;				// reading mode: the original can be edited and saved in place
	bool _src_dirty = false;
	bool _open_prefs = false;

	// Log panel (the log file of the Logger, read again when it changed)
	ImGuiTextBuffer _log_buffer;
	ImGuiTextFilter _log_filter;
	ImVector<int> _log_offsets;
	bool _log_scroll = false;
	double _log_last = 0.0;
	long long _log_stamp = 0;

	// ---- documents (app.cpp)
	bool open_file_path(const std::string& path, const std::string& destination = "");
	bool open_folder_path(const std::string& path, const std::string& destination = "");
	void set_project(Project&& p);
	bool open_doc(int index, int line = 0);
	void request_open_doc(int index, int line = 0);
	void guard(std::function<void()> action);	// runs the action, after saving or discarding the changes of the current document
	bool save();
	bool merge_files(const std::string& path);			// all the chapters (destination, else source) in one file
	bool save_as(const std::string& path);
	bool dirty() const;
	void reset_dest_from_source();
	void refresh_headings();
	void set_project_language(const std::string& code);
	bool french() const						{ return !_has_project || _project.language.rfind("fr", 0) == 0; }
	void remember_position();
	void status(const std::string& text);
	Doc* current_doc()						{ return (_has_project && _cur >= 0 && _cur < (int)_project.docs.size()) ? &_project.docs[_cur] : nullptr; }
	std::string editor_text(ImGuiProseEditor* editor);
	std::vector<std::string> editor_lines(ImGuiProseEditor* editor);
	std::vector<std::string> paragraphs(ImGuiProseEditor* editor);
	Pane* outline_pane()					{ return reading() ? &_src : &_dst; }
	bool is_markdown() const				{ return _s.markdown; }

	// ---- editing helpers (app.cpp)
	void fix_typography(bool whole);
	void insert_text(const std::string& s);
	// Python scripts (app_script.cpp)
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
	struct ScriptRange { ImGuiProsePos start, end; bool valid = false; };
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
	void go_to(int kind, int value);
	void move_paragraph(int delta);
	void jump_diff(int dir);
	void update_matches();
	struct Problem { int line = 1, start = 0, end = 0, kind = 0; std::string text, detail, message; };		// kind: 0 spelling, 1 grammar
	std::vector<Problem> _problems;
	int _problems_version = -1;
	double _problems_due = -1.0;
	bool _show_problems = false;
	void toggle_spell();
	void toggle_grammar();
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
	void draw_style_extra();
	style::Report _style_cmp_src, _style_cmp_dst;
	bool _style_cmp_done = false;
	std::vector<float> _style_timeline;
	std::vector<std::string> _style_timeline_labels;
	struct NameCount { std::string name; int count; int chapters; };
	std::vector<NameCount> _names;
	std::vector<std::pair<int, int>> _name_variants;
	bool _show_names = false, _names_scanned = false;
	void scan_names();
	void draw_names();
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
	void add_note_here();
	void draw_notes();
	void draw_vcs();
	void vcs_refresh();
	void reload_current();
	void draw_export();
	void update_spacers();
	void draw_charmap();
	bool prepare_search();
	void select_match(const FindMatch& m);
	void replace_in_editor(bool all);
	void replace_in_project();
	void find_next(int dir);
	void search_all();
	void generate_toc();
	void apply_theme();
	void rebuild_spans();
	static void typed_callback(ImGuiProseEditor* editor, const char* typed, void* user_data);

	// ---- interface (app_ui.cpp)
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
	void draw_dialogs();
	void draw_find();
	void draw_history();
	void draw_toc();
	void configure_pane(Pane& pane);
	void handle_shortcuts();
	void toggle(bool& value)				{ value = !value; _s.save(); }
};

#endif
