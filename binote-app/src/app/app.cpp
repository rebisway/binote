// Binote - the application: documents, panes, dialogs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app.h"
#include "search.h"
#include "i18n.h"
#include "../common/logger.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

static const char* ACCENT_NAMES[] = { "Jaune", "Vert", "Bleu", "Rose", "Orange" };

//-----------------------------------------------------------------------------
// Construction
//-----------------------------------------------------------------------------

App::App(GLFWwindow* window) : _window(window)
{
	_s.load();
	i18n::load(_s.language);
	_spell.load();
	_applied_font_size = _s.font_size;
	_applied_ui_font_size = _s.ui_font_size;
	_applied_fonts = _s.ui_font + "|" + _s.editor_font + "|" + _s.mono_font;
	_src.is_source = true;
	_src.preview = _s.preview_source;
	_dst.preview = _s.preview_destination;
	_find_case = _s.find_case;
	_goto_kind = _s.goto_kind;
	_snapshot_only_changes = _s.history_only_changes;
	_src.editor = ImGuiProse::CreateEditor();
	_dst.editor = ImGuiProse::CreateEditor();
	_src.md = ImGuiMd::CreateDocument();
	_dst.md = ImGuiMd::CreateDocument();
	_src.config.ReadOnly = true;
	_dst.config.TypedCallback = App::typed_callback;
	_dst.config.UserData = this;
	_diagrams = ImGuiMdDiagrams::CreateCache();
	_active = &_dst;
}

App::~App()
{
	free_script_editor();
	pyscript::finalize();
	ImGuiProse::DestroyEditor(_src.editor);
	ImGuiProse::DestroyEditor(_dst.editor);
	ImGuiMd::DestroyDocument(_src.md);
	ImGuiMd::DestroyDocument(_dst.md);
	ImGuiMdDiagrams::DestroyCache(_diagrams);
}

bool App::font_size_changed()
{
	const std::string fonts = _s.ui_font + "|" + _s.editor_font + "|" + _s.mono_font;
	if (_applied_font_size == _s.font_size && _applied_ui_font_size == _s.ui_font_size && _applied_fonts == fonts)
		return false;
	_applied_fonts = fonts;
	_applied_font_size = _s.font_size;
	_applied_ui_font_size = _s.ui_font_size;
	return true;
}

void App::status(const std::string& text)
{
	_status = text;
	_status_until = ImGui::GetTime() + 6.0;
	Logger::log(text);
}

bool App::dirty() const
{
	return _has_project && _cur >= 0 && _cur < (int)_project.docs.size() && _project.docs[_cur].dirty;
}

void App::shutdown()
{
	save_state();
}

// Where the person was in the project: reopened at the next start
void App::remember_position()
{
	Doc* d = current_doc();
	if (!d || !_has_project)
		return;
	if (util::Recent* r = _s.find_recent(_project.is_folder ? _project.source_root : _project.docs[0].src_path))
	{
		r->last_doc = d->rel;
		r->last_line = ImGuiProse::GetCaret(outline_pane()->editor).Line;
	}
}

void App::save_state()
{
	remember_position();
	_s.save();
}

//-----------------------------------------------------------------------------
// Text helpers
//-----------------------------------------------------------------------------

std::string App::editor_text(ImGuiProseEditor* editor)
{
	ImVector<char> buf;
	ImGuiProse::GetText(editor, &buf);
	return std::string(buf.Data, buf.Size > 0 ? buf.Size - 1 : 0);
}

std::vector<std::string> App::editor_lines(ImGuiProseEditor* editor)
{
	std::vector<std::string> lines;
	const int n = ImGuiProse::GetLineCount(editor);
	lines.reserve(n);
	for (int i = 1; i <= n; i++)
	{
		int len = 0;
		const char* s = ImGuiProse::GetLine(editor, i, &len);
		lines.emplace_back(s, len);
	}
	return lines;
}

std::vector<std::string> App::paragraphs(ImGuiProseEditor* editor)
{
	std::vector<std::string> list;
	const int n = ImGuiProse::GetParagraphCount(editor);
	for (int p = 1; p <= n; p++)
	{
		int len = 0;
		const char* s = ImGuiProse::GetLine(editor, ImGuiProse::GetLineOfParagraph(editor, p), &len);
		while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
			len--;
		list.emplace_back(s, len);
	}
	return list;
}

//-----------------------------------------------------------------------------
// Projects and documents
//-----------------------------------------------------------------------------

void App::guard(std::function<void()> action)
{
	if (!dirty())
	{
		action();
		return;
	}
	if (_s.autosave_on_switch)
	{
		if (save())
			action();
		return;
	}
	_after_confirm = action;
	{ char b[512]; snprintf(b, sizeof(b), TR("CONFIRM_SAVE_CHANGED"), current_doc()->rel.c_str()); _confirm_text = b; }
	_open_confirm = true;
}

bool App::open_file_path(const std::string& path, const std::string& destination)
{
	std::string dest = destination;
	if (dest.empty())
		for (const util::Recent& r : _s.recents)
			if (r.source == path)
				dest = r.destination;
	guard([this, path, dest]() {
		Project p;
		if (!Project::open_file(path, dest, p))
		{
			status(i18n::fmt(TR("MSG_FILE_NOT_FOUND"), path.c_str()));
			return;
		}
		set_project(std::move(p));
	});
	return true;
}

bool App::open_folder_path(const std::string& path, const std::string& destination)
{
	if (!util::is_directory(path))
	{
		status(i18n::fmt(TR("MSG_FOLDER_NOT_FOUND"), path.c_str()));
		return false;
	}
	if (destination.empty() && !Project::config_exists(path))
	{
		// New project: ask for the destination folder first
		_dest_folder = path;
		_dest_input = util::combine(path, "dst");
		_open_dest = true;
		return true;
	}
	guard([this, path, destination]() {
		Project p;
		if (!Project::open_folder(path, destination, p) || p.docs.empty())
		{
			status(TR("MSG_NO_FILES"));
			return;
		}
		if (!destination.empty())
			p.save_config();
		set_project(std::move(p));
	});
	return true;
}

void App::set_project(Project&& p)
{
	_force_reading = false;
	_project = std::move(p);
	_has_project = true;
	_project_words = -1;
	_chapter_words.clear();
	load_goals();
	load_notes();
	load_pins();
	_cur = -1;
	for (Doc& d : _project.docs)
	{
		std::string text;
		if (!(util::exists(d.dst_path) && util::read_text(d.dst_path, text)))
			util::read_text(d.src_path, text);
		d.headings = parse_headings(util::split_lines(text), d.is_markdown());
	}
	// Reopen at the chapter and the line where the project was left
	const std::string key = _project.is_folder ? _project.source_root : _project.docs[0].src_path;
	std::string last_doc;
	int last_line = 0;
	if (const util::Recent* old = _s.find_recent(key))
	{
		last_doc = old->last_doc;
		last_line = old->last_line;
	}
	_s.add_recent(key, _project.is_folder ? _project.dest_root : _project.docs[0].dst_path);
	if (!_project.is_folder)
		if (const util::Recent* r = _s.find_recent(key); r && !r->language.empty())
			_project.language = r->language;
	if (_spell.code() != _project.language)
	{
		_spell.load(_project.language);
		_diff_due = ImGui::GetTime();
	}
	int index = 0;
	bool found = false;
	for (int i = 0; i < (int)_project.docs.size(); i++)
		if (!last_doc.empty() && _project.docs[i].rel == last_doc)
		{
			index = i;
			found = true;
		}
	open_doc(index, found ? last_line : 0);
}

void App::request_open_doc(int index, int line)
{
	if (index == _cur)
	{
		if (line > 0)
		{
			Pane* p = outline_pane();
			ImGuiProse::SetCaret(p->editor, ImGuiProsePos(line, 0));
			ImGuiProse::ScrollToLine(p->editor, line, 0.1f);
		}
		return;
	}
	guard([this, index, line]() { open_doc(index, line); });
}

bool App::open_doc(int index, int line)
{
	if (!_has_project || index < 0 || index >= (int)_project.docs.size())
		return false;
	remember_position();
	Doc& d = _project.docs[index];
	std::string src_text, dst_text;
	bool crlf = false;
	util::read_text(d.src_path, src_text, &crlf);
	const bool existed = util::exists(d.dst_path);
	if (existed)
		util::read_text(d.dst_path, dst_text);
	else
		dst_text = src_text;
	d.crlf = crlf;
	d.dirty = false;
	_cur = index;
	ImGuiProse::SetText(_src.editor, src_text.c_str());
	ImGuiProse::SetText(_dst.editor, dst_text.c_str());
	_src.md_version = _dst.md_version = -1;
	_s.markdown = d.is_markdown();
	_current_para = _last_para = 0;
	refresh_headings();
	_diff_due = ImGui::GetTime();
	_seen_src_version = _seen_dst_version = -1;
	if (line > 0)
	{
		Pane* p = outline_pane();
		ImGuiProse::SetCaret(p->editor, ImGuiProsePos(line, 0));
		ImGuiProse::ScrollToLine(p->editor, line, 0.1f);
		// Same line at the top of the other pane (the texts are the same at this point): the panes stay aligned
		if (!reading() && _s.sync_scroll)
			ImGuiProse::ScrollToLine((p == &_dst ? _src : _dst).editor, line, 0.1f);
	}
	status(d.rel + (existed ? std::string() : std::string("  ") + TR("STATUS_COPY_NOT_SAVED")));
	return true;
}

bool App::merge_files(const std::string& path)
{
	std::string all;
	for (int i = 0; i < (int)_project.docs.size(); i++)
	{
		std::string text;
		if (i == _cur)
			text = editor_text(_dst.editor);
		else if (!(util::exists(_project.docs[i].dst_path) && util::read_text(_project.docs[i].dst_path, text)))
			util::read_text(_project.docs[i].src_path, text);
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			text.pop_back();
		if (!all.empty())
			all += "\n\n";
		all += text;
	}
	all += "\n";
	if (!util::write_text(path, all, false))
	{
		status(i18n::fmt(TR("MSG_SAVE_FAILED"), path.c_str()));
		return false;
	}
	status(i18n::fmt(TR("MSG_SAVED"), path.c_str()));
	return true;
}

bool App::save()
{
	Doc* d = current_doc();
	if (!d)
		return true;
	if (reading() && _edit_source)
	{
		if (!util::write_text(d->src_path, editor_text(_src.editor), d->crlf))
		{
			status(i18n::fmt(TR("MSG_SAVE_FAILED"), d->src_path.c_str()));
			return false;
		}
		_src_dirty = false;
		status(i18n::fmt(TR("MSG_SAVED"), d->src_path.c_str()));
		return true;
	}
	const std::string text = editor_text(_dst.editor);
	if (!util::write_text(d->dst_path, text, d->crlf))
	{
		status(i18n::fmt(TR("MSG_SAVE_FAILED"), d->dst_path.c_str()));
		return false;
	}
	History::add(d->dst_path, text, "save");
	d->dirty = false;
	status(i18n::fmt(TR("MSG_SAVED"), d->dst_path.c_str()));
	return true;
}

bool App::save_as(const std::string& path)
{
	Doc* d = current_doc();
	if (!d || _project.is_folder)
		return false;
	d->dst_path = path;
	return save();
}

void App::reset_dest_from_source()
{
	if (!current_doc())
		return;
	const std::string text = editor_text(_src.editor);
	const int last = ImGuiProse::GetLineCount(_dst.editor);
	int len = 0;
	ImGuiProse::GetLine(_dst.editor, last, &len);
	ImGuiProse::Replace(_dst.editor, ImGuiProsePos(1, 0), ImGuiProsePos(last, len), text.c_str());
}

void App::refresh_headings()
{
	Pane* p = outline_pane();
	_headings = parse_headings(editor_lines(p->editor), _s.markdown);
	if (Doc* d = current_doc())
		d->headings = _headings;
	_outline_due = -1.0;
}

bool App::can_close()
{
	if (!dirty() || _quit_requested)
		return true;
	_after_confirm = [this]() { _quit_requested = true; };
	{ char b[512]; snprintf(b, sizeof(b), TR("CONFIRM_SAVE_BEFORE_QUIT"), current_doc()->rel.c_str()); _confirm_text = b; }
	_open_confirm = true;
	return false;
}

//-----------------------------------------------------------------------------
// Typography
//-----------------------------------------------------------------------------

void App::typed_callback(ImGuiProseEditor* editor, const char* typed, void* user_data)
{
	App* app = (App*)user_data;
	// Typography is French; only the curly quotes can be chosen for another language
	if (!app->_s.auto_typography || (!app->french() && !(app->_s.quote_style == 2 && typed[0] == '"' && typed[1] == 0)))
		return;
	const ImGuiProsePos caret = ImGuiProse::GetCaret(editor);
	int len = 0;
	const char* line = ImGuiProse::GetLine(editor, caret.Line, &len);
	const std::string before(line, std::min(caret.Column, len));
	if (app->_s.markdown)
	{
		// Not in a code block or a code span
		bool fence = false;
		for (int i = 1; i < caret.Line; i++)
		{
			int l = 0;
			const char* s = ImGuiProse::GetLine(editor, i, &l);
			int a = 0;
			while (a < l && (s[a] == ' ' || s[a] == '\t'))
				a++;
			if (l - a >= 3 && (strncmp(s + a, "```", 3) == 0 || strncmp(s + a, "~~~", 3) == 0))
				fence = !fence;
		}
		if (fence || std::count(before.begin(), before.end(), '`') % 2 == 1)
			return;
	}
	const typography::Typed t = typography::on_typed(typed, before);
	if (!t.valid)
		return;
	ImGuiProse::Replace(editor, ImGuiProsePos(caret.Line, t.start), ImGuiProsePos(caret.Line, t.start + t.length), t.text.c_str());
	ImGuiProse::SetCaret(editor, ImGuiProsePos(caret.Line, t.start + (int)t.text.size() + t.caret_extra), false);
}

void App::fix_typography(bool whole)
{
	if (!french())
	{
		status(TR("MSG_TYPO_FRENCH_ONLY"));
		return;
	}
	ImGuiProseEditor* ed = _dst.editor;
	const ImGuiProsePos caret = ImGuiProse::GetCaret(ed);
	if (whole)
	{
		const std::string text = editor_text(ed);
		const std::string fixed = typography::fix_text(text, _s.markdown);
		if (fixed == text)
		{
			status(TR("MSG_TYPO_NOTHING"));
			return;
		}
		const int last = ImGuiProse::GetLineCount(ed);
		int len = 0;
		ImGuiProse::GetLine(ed, last, &len);
		ImGuiProse::Replace(ed, ImGuiProsePos(1, 0), ImGuiProsePos(last, len), fixed.c_str());
		ImGuiProse::SetCaret(ed, caret, false);
		status(TR("MSG_TYPO_FIXED"));
	}
	else
	{
		int len = 0;
		const char* s = ImGuiProse::GetLine(ed, caret.Line, &len);
		const std::string line(s, len), fixed = typography::fix_line(line, _s.markdown);
		if (fixed != line)
		{
			ImGuiProse::Replace(ed, ImGuiProsePos(caret.Line, 0), ImGuiProsePos(caret.Line, len), fixed.c_str());
			ImGuiProse::SetCaret(ed, ImGuiProsePos(caret.Line, std::min(caret.Column, (int)fixed.size())), false);
		}
	}
}

void App::insert_text(const std::string& s)
{
	ImGuiProseEditor* ed = _dst.editor;
	ImGuiProsePos a, b;
	if (ImGuiProse::GetSelection(ed, &a, &b))
		ImGuiProse::Replace(ed, a, b, s.c_str());
	else
		ImGuiProse::Replace(ed, ImGuiProse::GetCaret(ed), ImGuiProse::GetCaret(ed), s.c_str());
}

//-----------------------------------------------------------------------------
// Navigation, search, table of contents
//-----------------------------------------------------------------------------

void App::go_to(int kind, int value)
{
	ImGuiProseEditor* ed = _active->editor;
	int line = value;
	if (kind == 1)
		line = ImGuiProse::GetLineOfParagraph(ed, std::max(1, std::min(value, ImGuiProse::GetParagraphCount(ed))));
	line = std::max(1, std::min(line, ImGuiProse::GetLineCount(ed)));
	ImGuiProse::SetCaret(ed, ImGuiProsePos(line, 0));
	ImGuiProse::ScrollToLine(ed, line);
}

void App::move_paragraph(int delta)
{
	ImGuiProseEditor* ed = _active->editor;
	int line = ImGuiProse::GetCaret(ed).Line;
	const int count = ImGuiProse::GetLineCount(ed);
	for (line += delta; line >= 1 && line <= count; line += delta)
		if (ImGuiProse::GetParagraphOfLine(ed, line) > 0)
		{
			ImGuiProse::SetCaret(ed, ImGuiProsePos(line, 0));
			return;
		}
}

void App::jump_diff(int dir)
{
	if (_diff.dst_marks.empty())
		return;
	std::vector<int> paras;
	for (const diff::Mark& m : _diff.dst_marks)
		paras.push_back(m.para + 1);
	std::sort(paras.begin(), paras.end());
	paras.erase(std::unique(paras.begin(), paras.end()), paras.end());
	const int cur = ImGuiProse::GetParagraphOfLine(_dst.editor, ImGuiProse::GetCaret(_dst.editor).Line);
	int target = 0;
	if (dir > 0)
	{
		auto it = std::upper_bound(paras.begin(), paras.end(), cur);
		target = it == paras.end() ? paras.front() : *it;
	}
	else
	{
		auto it = std::lower_bound(paras.begin(), paras.end(), cur);
		target = it == paras.begin() ? paras.back() : *(it - 1);
	}
	const int line = ImGuiProse::GetLineOfParagraph(_dst.editor, target);
	if (line > 0)
	{
		ImGuiProse::SetCaret(_dst.editor, ImGuiProsePos(line, 0));
		ImGuiProse::ScrollToLine(_dst.editor, line);
	}
}

static std::string lower_ascii(std::string s)
{
	for (char& c : s)
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
	return s;
}

// Position (line, byte column) of a byte offset of the text of an editor (lines joined by \n)
static void position_of(const std::vector<int>& line_starts, int offset, int* line, int* column)
{
	const int n = (int)(std::upper_bound(line_starts.begin(), line_starts.end(), offset) - line_starts.begin());
	*line = std::max(1, n);
	*column = offset - line_starts[*line - 1];
}

static std::vector<int> starts_of_lines(const std::string& text)
{
	std::vector<int> starts(1, 0);
	for (size_t i = 0; i < text.size(); i++)
		if (text[i] == '\n')
			starts.push_back((int)i + 1);
	return starts;
}

// The search engine with the options of the find box. False if the expression is wrong (the error is shown in the box).
bool App::prepare_search()
{
	search::Options o;
	o.regex = _find_regex;
	o.match_case = _find_case;
	o.whole_word = _find_word;
	_engine.set(_find_text, o);
	_find_error = _engine.error();
	return _find_error.empty() && !_engine.empty();
}

void App::update_matches()
{
	_matches.clear();
	if (!prepare_search())
		return;
	const std::string text = editor_text(_active->editor);
	const std::vector<int> starts = starts_of_lines(text);
	std::vector<search::Match> found;
	_engine.find_all(text, found, 5000);
	for (const search::Match& m : found)
	{
		FindMatch f;
		position_of(starts, m.start, &f.line, &f.column);
		position_of(starts, m.end, &f.end_line, &f.end_column);
		_matches.push_back(f);
	}
}

void App::select_match(const FindMatch& m)
{
	ImGuiProse::SetSelection(_active->editor, ImGuiProsePos(m.line, m.column), ImGuiProsePos(m.end_line, m.end_column));
	ImGuiProse::SetCaret(_active->editor, ImGuiProsePos(m.end_line, m.end_column));
	ImGuiProse::ScrollToLine(_active->editor, m.line);
}

void App::find_next(int dir)
{
	update_matches();
	if (_matches.empty())
		return;
	const ImGuiProsePos caret = ImGuiProse::GetCaret(_active->editor);
	int pick = -1;
	if (dir > 0)
	{
		for (size_t i = 0; i < _matches.size(); i++)
			if (_matches[i].line > caret.Line || (_matches[i].line == caret.Line && _matches[i].column >= caret.Column))
			{
				ImGuiProsePos a, b;
				if (ImGuiProse::GetSelection(_active->editor, &a, &b) && _matches[i].line == a.Line && _matches[i].column == a.Column)
					continue;
				pick = (int)i;
				break;
			}
		if (pick < 0)
			pick = 0;
	}
	else
	{
		for (size_t i = _matches.size(); i-- > 0;)
			if (_matches[i].line < caret.Line || (_matches[i].line == caret.Line && _matches[i].column < caret.Column))
			{
				pick = (int)i;
				break;
			}
		if (pick < 0)
			pick = (int)_matches.size() - 1;
	}
	select_match(_matches[pick]);
}

void App::search_all()
{
	_hits.clear();
	_hits_for = _find_text;
	if (!_has_project || !prepare_search())
		return;
	for (int i = 0; i < (int)_project.docs.size() && _hits.size() < 2000; i++)
	{
		std::string text;
		if (i == _cur)
			text = editor_text(_dst.editor);
		else if (!(util::exists(_project.docs[i].dst_path) && util::read_text(_project.docs[i].dst_path, text)))
			util::read_text(_project.docs[i].src_path, text);
		const std::vector<int> starts = starts_of_lines(text);
		std::vector<search::Match> found;
		_engine.find_all(text, found, 2000);
		int last_line = 0;
		for (const search::Match& m : found)
		{
			int line, column;
			position_of(starts, m.start, &line, &column);
			if (line == last_line)
				continue;
			last_line = line;
			const int end = line < (int)starts.size() ? starts[line] - 1 : (int)text.size();
			_hits.push_back({ i, line, text.substr((size_t)starts[line - 1], (size_t)std::max(0, end - starts[line - 1])) });
			if (_hits.size() >= 2000)
				break;
		}
	}
}

// The matches are replaced in the destination, in one undoable step
void App::replace_in_editor(bool all)
{
	if (!prepare_search() || _active != &_dst)
		return;
	const std::string repl = _replace_text;
	ImGuiProseEditor* ed = _dst.editor;
	const std::string text = editor_text(ed);
	const std::vector<int> starts = starts_of_lines(text);
	std::vector<search::Match> found;
	_engine.find_all(text, found, (size_t)-1 / 4, &repl);
	if (found.empty())
	{
		status(TR("MSG_FIND_NOTHING"));
		return;
	}
	if (!all)
	{
		// The match which is selected, else the next one from the caret
		const ImGuiProsePos caret = ImGuiProse::GetCaret(ed);
		ImGuiProsePos a, b;
		const bool selected = ImGuiProse::GetSelection(ed, &a, &b);
		const search::Match* pick = nullptr;
		for (const search::Match& m : found)
		{
			int line, column;
			position_of(starts, m.start, &line, &column);
			if (selected ? (line == a.Line && column == a.Column) : (line > caret.Line || (line == caret.Line && column >= caret.Column)))
			{
				pick = &m;
				break;
			}
		}
		if (!pick)
		{
			find_next(1);										// nothing selected which matches: go to the next match
			return;
		}
		int l1, c1, l2, c2;
		position_of(starts, pick->start, &l1, &c1);
		position_of(starts, pick->end, &l2, &c2);
		ImGuiProse::Replace(ed, ImGuiProsePos(l1, c1), ImGuiProsePos(l2, c2), pick->replacement.c_str());
		if (Doc* d = current_doc())
			d->dirty = true;
		update_matches();
		find_next(1);
		return;
	}
	int count = 0;
	const std::string result = _engine.replace_all(text, repl, count);
	const ImGuiProsePos caret = ImGuiProse::GetCaret(ed);
	const int last = ImGuiProse::GetLineCount(ed);
	int len = 0;
	ImGuiProse::GetLine(ed, last, &len);
	ImGuiProse::Replace(ed, ImGuiProsePos(1, 0), ImGuiProsePos(last, len), result.c_str());
	ImGuiProse::SetCaret(ed, ImGuiProsePos(std::min(caret.Line, ImGuiProse::GetLineCount(ed)), 0), false);
	if (Doc* d = current_doc())
		d->dirty = true;
	status(i18n::fmt(TR("MSG_FIND_REPLACED"), count));
	update_matches();
}

// Every chapter of the project: the files of the destination are rewritten (a version is kept in the history)
void App::replace_in_project()
{
	if (!_has_project || !prepare_search())
		return;
	const std::string repl = _replace_text;
	int total = 0, files = 0;
	for (int i = 0; i < (int)_project.docs.size(); i++)
	{
		if (i == _cur)
		{
			const std::string before = editor_text(_dst.editor);
			int count = 0;
			const std::string result = _engine.replace_all(before, repl, count);
			if (count > 0)
			{
				const int last = ImGuiProse::GetLineCount(_dst.editor);
				int len = 0;
				ImGuiProse::GetLine(_dst.editor, last, &len);
				ImGuiProse::Replace(_dst.editor, ImGuiProsePos(1, 0), ImGuiProsePos(last, len), result.c_str());
				if (Doc* d = current_doc())
					d->dirty = true;
				total += count;
				files++;
			}
			continue;
		}
		Doc& d = _project.docs[i];
		std::string text;
		if (!(util::exists(d.dst_path) && util::read_text(d.dst_path, text)))
			util::read_text(d.src_path, text);
		int count = 0;
		const std::string result = _engine.replace_all(text, repl, count);
		if (count > 0 && util::write_text(d.dst_path, result, d.crlf))
		{
			History::add(d.dst_path, result, "replace");
			total += count;
			files++;
		}
	}
	status(i18n::fmt(TR("MSG_FIND_REPLACED_PROJECT"), total, files));
	update_matches();
}

void App::generate_toc()
{
	if (!_has_project)
		return;
	refresh_headings();
	std::vector<toc::Chapter> chapters;
	for (const Doc& d : _project.docs)
		chapters.push_back({ d.rel, d.headings });
	_toc_multi = _project.is_folder && _project.docs.size() > 1;
	_toc_text = toc::generate(chapters, _s.markdown, _toc_multi);
	_show_toc = true;
}

//-----------------------------------------------------------------------------
// Theme, spans
//-----------------------------------------------------------------------------

void App::apply_theme()
{
	if (_s.dark)
		ImGui::StyleColorsDark();
	else
		ImGui::StyleColorsLight();
	ImGuiStyle& st = ImGui::GetStyle();
	st.WindowRounding = 4.0f;
	st.FrameRounding = 4.0f;
	st.PopupRounding = 4.0f;
	st.ScrollbarRounding = 6.0f;
	st.GrabRounding = 4.0f;
	st.TabRounding = 4.0f;
	st.FramePadding = ImVec2(8.0f, 4.0f);
	st.ItemSpacing = ImVec2(8.0f, 5.0f);
	st.ScrollbarSize = 14.0f;
	_applied_dark = _s.dark;
	_theme_set = true;
}

void App::rebuild_spans()
{
	locate_notes();
	const bool dark = _s.dark;
	const ImU32 changed = dark ? IM_COL32(110, 90, 25, 90) : IM_COL32(255, 235, 160, 150);
	const ImU32 added = dark ? IM_COL32(30, 90, 50, 110) : IM_COL32(200, 240, 205, 170);
	const ImU32 removed = dark ? IM_COL32(120, 40, 40, 110) : IM_COL32(250, 205, 205, 170);
	const ImU32 word_added = dark ? IM_COL32(50, 140, 80, 150) : IM_COL32(140, 220, 155, 200);
	const ImU32 word_removed = dark ? IM_COL32(170, 60, 60, 150) : IM_COL32(240, 150, 150, 200);

	auto build = [&](Pane& pane, const std::vector<diff::Mark>& marks, const std::vector<diff::Word>& words)
	{
		std::vector<ImGuiProseSpan> spans;
		if (_s.show_diff)
		{
			for (const diff::Mark& m : marks)
			{
				const int line = ImGuiProse::GetLineOfParagraph(pane.editor, m.para + 1);
				if (line > 0)
					spans.push_back(ImGuiProseSpan(line, -1, -1, m.kind == diff::Added ? added : (m.kind == diff::Removed ? removed : changed)));
			}
			for (const diff::Word& w : words)
			{
				const int line = ImGuiProse::GetLineOfParagraph(pane.editor, w.para + 1);
				if (line > 0)
					spans.push_back(ImGuiProseSpan(line, w.start, w.start + w.length, w.kind == diff::WordAdded ? word_added : word_removed));
			}
		}
		// Notes: the paragraphs which have one
		if (!pane.is_source)
			for (const auto& np : _note_paras)
			{
				const int line = ImGuiProse::GetLineOfParagraph(pane.editor, np.first);
				if (line > 0)
					spans.push_back(ImGuiProseSpan(line, -1, -1, dark ? IM_COL32(150, 90, 200, 60) : IM_COL32(200, 160, 235, 110)));
			}
		// Style analysis: the passages are colored by kind, stronger when the kind weighs more in the indication
		if (_show_style && _style_done && _style_highlight && &pane == &style_pane())
			for (const StyleMark& mk : _style_marks)
			{
				static const unsigned char rgb[][3] = { { 70, 130, 230 }, { 230, 70, 70 }, { 235, 150, 50 }, { 165, 90, 220 }, { 40, 180, 170 }, { 225, 200, 50 }, { 150, 150, 150 }, { 230, 100, 170 }, { 240, 90, 60 }, { 120, 200, 90 }, { 90, 90, 230 } };
				const style::Observation& o = _style_report.observations[mk.obs];
				const style::Contribution* c = style_points(o.kind);
				const double w = c && c->max_points > 0.0 ? c->points / c->max_points : 0.3;
				const int alpha = (int)((dark ? 70 : 90) + 130 * w);
				const unsigned char* k = rgb[std::min<size_t>(o.kind, 10)];
				spans.push_back(ImGuiProseSpan(mk.line, mk.start, mk.end, IM_COL32(k[0], k[1], k[2], alpha)));
			}
		// Spelling: wavy underlines under the words the dictionary does not know (not in code blocks)
		if (_s.spell_check && _spell.available())
		{
			const int lines = ImGuiProse::GetLineCount(pane.editor);
			bool fence = false;
			std::vector<std::pair<int, int>> errors;
			for (int i = 1; i <= lines; i++)
			{
				int len = 0;
				const char* s = ImGuiProse::GetLine(pane.editor, i, &len);
				if (_s.markdown)
				{
					int a = 0;
					while (a < len && (s[a] == ' ' || s[a] == '\t')) a++;
					if (len - a >= 3 && (strncmp(s + a, "```", 3) == 0 || strncmp(s + a, "~~~", 3) == 0))
					{
						fence = !fence;
						continue;
					}
				}
				if (fence)
					continue;
				_spell.find_errors(s, len, _s.markdown, errors);
				for (const auto& e : errors)
					spans.push_back(ImGuiProseSpan(i, e.first, e.second, IM_COL32(230, 60, 60, 255), ImGuiProseSpanKind_Squiggle));
			}
		}
		// Grammar: blue wavy underlines (the destination only)
		if (_s.grammar_check && !pane.is_source)
		{
			const int lines = ImGuiProse::GetLineCount(pane.editor);
			bool fence = false;
			for (int i = 1; i <= lines; i++)
			{
				int len = 0;
				const char* s = ImGuiProse::GetLine(pane.editor, i, &len);
				if (_s.markdown)
				{
					int a = 0;
					while (a < len && (s[a] == ' ' || s[a] == '\t')) a++;
					if (len - a >= 3 && (strncmp(s + a, "```", 3) == 0 || strncmp(s + a, "~~~", 3) == 0))
					{
						fence = !fence;
						continue;
					}
					if (a < len && s[a] == '#')
						continue;
				}
				if (fence)
					continue;
				for (const grammar::Issue& is : grammar_issues(std::string(s, (size_t)len)))
					spans.push_back(ImGuiProseSpan(i, is.start, is.end, IM_COL32(60, 140, 240, 255), ImGuiProseSpanKind_Squiggle));
			}
		}
		if (_show_find && _active == &pane)
			for (const FindMatch& m : _matches)
				for (int l = m.line; l <= m.end_line; l++)
					spans.push_back(ImGuiProseSpan(l, l == m.line ? m.column : 0, l == m.end_line ? m.end_column : -1, dark ? IM_COL32(90, 130, 220, 140) : IM_COL32(120, 170, 255, 150)));
		ImGuiProse::SetSpans(pane.editor, spans.data(), (int)spans.size());
	};
	build(_src, _diff.src_marks, _diff.src_words);
	build(_dst, _diff.dst_marks, _diff.dst_words);
}

//-----------------------------------------------------------------------------
// Test bridge commands
//-----------------------------------------------------------------------------

bool App::command(const std::vector<std::string>& a)
{
	if (a.empty())
		return false;
	const std::string& c = a[0];
	if (c == "open" && a.size() > 1) return open_file_path(a[1], a.size() > 2 ? a[2] : "");
	if (c == "openfolder" && a.size() > 1) return open_folder_path(a[1], a.size() > 2 ? a[2] : "");
	if (c == "save") return save();
	if (c == "lang" && a.size() > 1) { i18n::load(a[1]); return true; }
	if (c == "doc" && a.size() > 1) { request_open_doc(std::atoi(a[1].c_str())); return true; }
	if (c == "typo") { fix_typography(a.size() > 1 && a[1] == "all"); return true; }
	if (c == "toc") { generate_toc(); return true; }
	if (c == "theme" && a.size() > 1) { _s.dark = a[1] == "dark"; return true; }
	if (c == "reading" && a.size() > 1) { set_reading(a[1] == "on"); return true; }
	if (c == "savestate") { save_state(); return true; }
	if (c == "font" && a.size() > 2) { (a[1] == "ui" ? _s.ui_font : a[1] == "mono" ? _s.mono_font : _s.editor_font) = a[2]; return true; }
	if (c == "stats") { update_stats(); _show_stats = true; return true; }
	if (c == "spell" && a.size() > 1) { _s.spell_check = a[1] == "on"; rebuild_spans(); return true; }
	if (c == "plang" && a.size() > 1) { set_project_language(a[1]); return true; }
	if (c == "langs") { for (const auto& l : i18n::languages()) printf("%s = %s\n", l.first.c_str(), l.second.c_str()); return true; }
	if (c == "script" && a.size() > 1)
	{
		// script <name>: starts the script (the dialog box, if it has one, stays open)
		for (const pyscript::Info& s : pyscript::list(i18n::current()))
			if (s.name == a[1])
			{
				start_script(s.path);
				return true;
			}
		printf("script not found: %s\n", a[1].c_str());
		return true;
	}
	// findopts <regex 0|1> <case 0|1> <word 0|1> <find> [replace]; replace one | all | project
	if (c == "findopts" && a.size() > 4)
	{
		_find_regex = a[1] == "1"; _find_case = a[2] == "1"; _find_word = a[3] == "1";
		snprintf(_find_text, sizeof(_find_text), "%s", a[4].c_str());
		snprintf(_replace_text, sizeof(_replace_text), "%s", a.size() > 5 ? a[5].c_str() : "");
		_show_find = true;
		_active = &_dst;
		update_matches();
		printf("matches=%d error=%s\n", (int)_matches.size(), _find_error.c_str());
		return true;
	}
	if (c == "replace" && a.size() > 1) { if (a[1] == "project") replace_in_project(); else replace_in_editor(a[1] == "all"); return true; }
	if (c == "anchors") { const auto s = ImGuiProse::GetScrollAnchor(_src.editor), d = ImGuiProse::GetScrollAnchor(_dst.editor); printf("src %d %.3f %.3f  dst %d %.3f %.3f settle=%d\n", s.Paragraph, s.Fraction, ImGuiProse::GetScrollFraction(_src.editor), d.Paragraph, d.Fraction, ImGuiProse::GetScrollFraction(_dst.editor), _sync_settle); return true; }
	if (c == "vcs") { _show_vcs = true; vcs_refresh(); return true; }
	if (c == "vcscommit" && a.size() > 1) { std::string out; if (dirty()) save(); const bool ok = vcs::commit(_vcs, _project.source_root, a[1], out); vcs_refresh(); printf("commit %d: %s\n", (int)ok, out.c_str()); return true; }
	if (c == "names") { _show_names = true; return true; }
	if (c == "note" && a.size() > 1) { add_note_here(); snprintf(_note_text, sizeof(_note_text), "%s", a[1].c_str()); const Doc* d = current_doc(); const auto paras = paragraphs(_dst.editor); if (d && _note_target_para >= 1 && _note_target_para <= (int)paras.size()) { _open_note = false; _notes.push_back({ d->rel, paras[(size_t)_note_target_para - 1].substr(0, 30), _note_target_para, a[1] }); save_notes(); rebuild_spans(); } return true; }
	if (c == "shownotes") { _show_notes = true; return true; }
	if (c == "grammar" && a.size() > 1) { for (const grammar::Issue& is : grammar_issues(a[1])) printf("%s [%d,%d] %s -> %s\n", is.rule.c_str(), is.start, is.end, a[1].substr((size_t)is.start, (size_t)(is.end - is.start)).c_str(), is.suggestions.empty() ? "" : is.suggestions[0].c_str()); return true; }
	if (c == "grammarfile" && a.size() > 1)
	{
		// every paragraph of a file: the number of issues by rule, then a sample with its context
		std::string text;
		util::read_text(a[1], text);
		std::map<std::string, int> counts;
		int shown = 0, paragraphs = 0;
		const int limit = a.size() > 2 ? std::atoi(a[2].c_str()) : 40;
		for (const std::string& line : util::split_lines(text))
		{
			if (line.find_first_not_of(" \t") == std::string::npos)
				continue;
			paragraphs++;
			for (const grammar::Issue& is : grammar_issues(line))
			{
				counts[is.rule]++;
				if (shown++ < limit)
				{
					const int from = std::max(0, is.start - 25);
					printf("%-18s %s [[%s]] %s  -> %s\n", is.rule.c_str(), line.substr((size_t)from, (size_t)(is.start - from)).c_str(), line.substr((size_t)is.start, (size_t)(is.end - is.start)).c_str(), line.substr((size_t)is.end, 20).c_str(), is.suggestions.empty() ? "" : is.suggestions[0].c_str());
				}
			}
		}
		printf("paragraphs=%d\n", paragraphs);
		for (const auto& kv : counts)
			printf("%-20s %d\n", kv.first.c_str(), kv.second);
		return true;
	}
	if (c == "problems") { _show_problems = true; return true; }
	if (c == "charmap") { _show_charmap = true; return true; }
	if (c == "about") { _open_about = true; return true; }
	if (c == "scriptok") { if (_script_active) _script_ok = true; return true; }
	if (c == "scripteditor") { _show_script_editor = true; return true; }
	if (c == "style") { _show_style = true; run_style(); const auto& m = _style_report.metrics; printf("words=%d sentences=%d mean=%.1f cv=%.2f lex=%.2f conn=%.1f stock=%.1f long=%d enum=%d obs=%d\n", m.words, m.sentences, m.mean_sentence, m.sentence_variation, m.lexical_diversity, m.connectors_per_1000, m.stock_phrases_per_1000, m.long_sentences, m.enumerations, (int)_style_report.observations.size()); for (const auto& o : _style_report.observations) printf("  kind=%d count=%d text=%s\n", o.kind, o.count, o.text.c_str()); _show_style = true; return true; }
	if (c == "stylehl" && a.size() > 1) { _style_highlight = a[1] == "on"; rebuild_spans(); return true; }
	if (c == "prefs") { _open_prefs = true; return true; }
	if (c == "sysopen" && a.size() > 1) return open_from_system(a[1]);
	if (c == "sysmode" && a.size() > 1) { _s.system_open_mode = std::atoi(a[1].c_str()); return true; }
	if (c == "tree" && a.size() > 1) { _s.show_tree = a[1] == "on"; return true; }
	if (c == "preview" && a.size() > 2) { (a[1] == "src" ? _src : _dst).preview = a[2] == "on"; return true; }
	if (c == "goto" && a.size() > 1) { go_to(a.size() > 2 && a[2] == "p" ? 1 : 0, std::atoi(a[1].c_str())); return true; }
	if (c == "find" && a.size() > 1) { snprintf(_find_text, sizeof(_find_text), "%s", a[1].c_str()); _show_find = true; find_next(1); return true; }
	if (c == "history") { if (Doc* d = current_doc()) { _snapshots = History::list(d->dst_path); _snapshot_selected = -1; _show_history = true; } return true; }
	if (c == "set" && a.size() > 2)
	{
		const bool on = a[2] == "on";
		if (a[1] == "wrap") _s.wrap = on; else if (a[1] == "diff") _s.show_diff = on; else if (a[1] == "sync") _s.sync_scroll = on;
		else if (a[1] == "nbsp") _s.show_nbsp = on; else if (a[1] == "lines") _s.line_numbers = on; else if (a[1] == "paras") _s.paragraph_numbers = on;
		else if (a[1] == "autotypo") _s.auto_typography = on; else if (a[1] == "mono") _s.mono_editor = on; else if (a[1] == "markdown") _s.markdown = on;
		else if (a[1] == "log") _s.show_log = on;
		else if (a[1] == "fontsize") _s.font_size = (float)std::atof(a[2].c_str());
		return true;
	}
	if (c == "status")
	{
		Doc* d = current_doc();
		const ImGuiProsePos p = ImGuiProse::GetCaret(_dst.editor);
		printf("font=%.0f ui=%.0f ",_s.font_size,_s.ui_font_size); printf("doc=%d dirty=%d paras=%d/%d caret=%d:%d diff=+%d ~%d -%d headings=%d\n", _cur, d ? d->dirty : 0, ImGuiProse::GetParagraphCount(_src.editor),
			ImGuiProse::GetParagraphCount(_dst.editor), p.Line, p.Column, _diff.added, _diff.changed, _diff.removed, (int)_headings.size());
		return true;
	}
	if (c == "dump" && a.size() > 1)
	{
		util::write_text(a[1], editor_text(_dst.editor));
		return true;
	}
	return false;
}

bool App::open_from_system(const std::string& path)
{
	if (util::is_directory(path))
		return open_folder_path(path);
	if (!open_file_path(path))
		return false;
	if (_s.system_open_mode == 2 || !_has_project)
		return true;
	_force_reading = true;
	_src.preview = (_s.system_open_mode == 1) && _s.markdown;
	return true;
}
//-----------------------------------------------------------------------------
// Statistics: words, characters, paragraphs
//-----------------------------------------------------------------------------

static bool stat_is_space(unsigned int c)
{
	return c == ' ' || c == '\t' || c == 0xA0 || c == 0x202F || (c >= 0x2000 && c <= 0x200A) || c == 0x3000;
}

static bool stat_is_word_char(unsigned int c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7)
		|| (c >= 0x370 && c <= 0x1FFF) || c >= 0x3040;
}

// Words of a piece of a line: tokens between spaces which hold a letter or a digit (so "##", "---" and lone dashes are not words)
static void stat_count(const char* s, int len, TextStats& st, bool& in_token, bool& token_has_word)
{
	for (int i = 0; i < len;)
	{
		unsigned char b = (unsigned char)s[i];
		int n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
		unsigned int c = n == 1 ? b : (b & (0xFF >> (n + 1)));
		for (int k = 1; k < n && i + k < len; k++)
			c = (c << 6) | ((unsigned char)s[i + k] & 0x3F);
		i += n;
		st.chars++;
		if (stat_is_space(c))
		{
			if (in_token && token_has_word)
				st.words++;
			in_token = token_has_word = false;
			continue;
		}
		st.chars_no_space++;
		in_token = true;
		if (stat_is_word_char(c))
			token_has_word = true;
	}
}

static TextStats stat_editor(ImGuiProseEditor* ed)
{
	TextStats st;
	st.lines = ImGuiProse::GetLineCount(ed);
	st.paragraphs = ImGuiProse::GetParagraphCount(ed);
	for (int i = 1; i <= st.lines; i++)
	{
		int len = 0;
		const char* s = ImGuiProse::GetLine(ed, i, &len);
		bool in_token = false, has_word = false;
		stat_count(s, len, st, in_token, has_word);
		if (in_token && has_word)
			st.words++;
	}
	return st;
}

// Words of every chapter: the current one from the editor, the others from their files
void App::count_project()
{
	_chapter_words.assign(_project.docs.size(), 0);
	long long total = 0;
	for (int i = 0; i < (int)_project.docs.size(); i++)
	{
		int words = 0;
		if (i == _cur)
			words = _stats_dst.words;
		else
		{
			std::string text;
			if (!(util::exists(_project.docs[i].dst_path) && util::read_text(_project.docs[i].dst_path, text)))
				util::read_text(_project.docs[i].src_path, text);
			TextStats st;
			for (const std::string& line : util::split_lines(text))
			{
				bool in_token = false, has_word = false;
				stat_count(line.data(), (int)line.size(), st, in_token, has_word);
				if (in_token && has_word)
					st.words++;
			}
			words = st.words;
		}
		_chapter_words[i] = words;
		total += words;
	}
	_project_words = total;
}

// Objectives are kept apart from the project (goals.ini in the data folder), so the project file stays as it is
static std::string goals_key(const Project& p)
{
	return p.is_folder ? p.source_root : (p.docs.empty() ? p.source_root : p.docs[0].src_path);
}

void App::load_goals()
{
	_goal_chapter = _goal_total = 0;
	std::string text;
	if (!util::read_text(util::combine(util::data_path(), "goals.ini"), text))
		return;
	const std::string key = goals_key(_project) + "=";
	for (const std::string& line : util::split_lines(text))
		if (line.compare(0, key.size(), key) == 0)
			sscanf(line.c_str() + key.size(), "%d|%d", &_goal_chapter, &_goal_total);
}

void App::save_goals()
{
	const std::string path = util::combine(util::data_path(), "goals.ini");
	std::string text, out;
	util::read_text(path, text);
	const std::string key = goals_key(_project) + "=";
	for (const std::string& line : util::split_lines(text))
		if (!line.empty() && line.compare(0, key.size(), key) != 0)
			out += line + "\n";
	if (_goal_chapter > 0 || _goal_total > 0)
		out += key + std::to_string(_goal_chapter) + "|" + std::to_string(_goal_total) + "\n";
	util::write_text(path, out);
}

void App::update_stats()
{
	_stats_src = stat_editor(_src.editor);
	_stats_dst = stat_editor(_dst.editor);
	_stats_due = -1.0;
}

// Words of the selection (computed again only when the selection or the text changes)
int App::selection_words()
{
	ImGuiProsePos a, b;
	ImGuiProseEditor* ed = _active->editor;
	if (!ImGuiProse::GetSelection(ed, &a, &b))
	{
		_sel_chars = 0;
		return 0;
	}
	const long long key = ((long long)a.Line * 1000003 + a.Column) * 1000003 + (long long)b.Line * 1009 + b.Column + (long long)ImGuiProse::GetVersion(ed) * 7919 + (_active == &_src ? 1 : 0);
	if (key == _sel_key)
		return _sel_words;
	TextStats st;
	for (int l = a.Line; l <= b.Line; l++)
	{
		int len = 0;
		const char* s = ImGuiProse::GetLine(ed, l, &len);
		const int from = (l == a.Line) ? a.Column : 0, to = (l == b.Line) ? b.Column : len;
		bool in_token = false, has_word = false;
		stat_count(s + from, std::max(0, to - from), st, in_token, has_word);
		if (in_token && has_word)
			st.words++;
	}
	_sel_key = key;
	_sel_words = st.words;
	_sel_chars = st.chars;
	return _sel_words;
}
// Right click on a word of a pane: the menu of the spelling (suggestions, ignore, add to the dictionary)
// The issues of a paragraph: the cache is made again when the language or the ignored rules change
std::vector<grammar::Issue> App::grammar_issues(const std::string& line)
{
	const std::string language = _has_project ? _project.language : "fr";
	const std::string key = language + "|" + _s.grammar_ignored;
	if (key != _grammar_key || _grammar_cache.size() > 20000)
	{
		_grammar_key = key;
		_grammar_cache.clear();
		_grammar.set_language(language);
		std::set<std::string> ignored;
		size_t pos = 0;
		while (pos < _s.grammar_ignored.size())
		{
			size_t comma = _s.grammar_ignored.find(',', pos);
			if (comma == std::string::npos)
				comma = _s.grammar_ignored.size();
			ignored.insert(_s.grammar_ignored.substr(pos, comma - pos));
			pos = comma + 1;
		}
		_grammar.set_ignored(ignored);
		if (!_lexicon_fr_tried && language.rfind("fr", 0) == 0)
		{
			_lexicon_fr_tried = true;
			const std::string app = util::application_path();
			for (const std::string& d : { util::combine(app, "lexicon"), util::combine(app, "../lexicon"), util::combine(app, "../../binote-app/lexicon"), util::combine(app, "../binote-app/lexicon") })
				if (util::exists(util::combine(d, "fr.lex")) && _lexicon_fr.load(util::combine(d, "fr.lex")))
				{
					Logger::log("Lexicon: " + util::combine(d, "fr.lex"));
					break;
				}
		}
		_grammar.set_lexicon(language.rfind("fr", 0) == 0 ? &_lexicon_fr : nullptr);
		_grammar.set_dictionary([this](const std::string& w) { return _spell.available() && _spell.check(w); });
	}
	auto it = _grammar_cache.find(line);
	if (it == _grammar_cache.end())
		it = _grammar_cache.emplace(line, _grammar.check_paragraph(line)).first;
	return it->second;
}

void App::spell_menu_at(Pane& pane, ImVec2 screen_pos)
{
	ImGuiProsePos pos;
	if (!ImGuiProse::GetPositionFromScreen(pane.editor, screen_pos, &pos))
		return;
	// A grammar issue under the mouse comes first
	if (_s.grammar_check && !pane.is_source)
	{
		int glen = 0;
		const char* gs = ImGuiProse::GetLine(pane.editor, pos.Line, &glen);
		for (const grammar::Issue& is : grammar_issues(std::string(gs, (size_t)glen)))
			if (pos.Column >= is.start && pos.Column <= is.end)
			{
				_active = &pane;
				ImGuiProse::SetCaret(pane.editor, pos, false);
				_gram_issue = is;
				_gram_line = pos.Line;
				_gram_message = i18n::fmt(TR(grammar::message_key(is.rule).c_str()), is.arg.c_str());
				_open_grammar_menu = true;
				return;
			}
	}
	if (!_s.spell_check || !_spell.available())
		return;
	_active = &pane;
	int len = 0;
	const char* s = ImGuiProse::GetLine(pane.editor, pos.Line, &len);
	std::vector<std::pair<int, int>> errors;
	_spell.find_errors(s, len, _s.markdown, errors);
	for (const auto& e : errors)
		if (pos.Column >= e.first && pos.Column <= e.second)
		{
			ImGuiProse::SetCaret(pane.editor, pos, false);
			_spell_word.assign(s + e.first, e.second - e.first);
			_spell_line = pos.Line;
			_spell_start = e.first;
			_spell_end = e.second;
			_spell_suggestions = _spell.suggest(_spell_word);
			_open_spell_menu = true;
			return;
		}
}
// Language of the text of the project (dictionary of the spelling, typography); not the language of the interface
void App::set_project_language(const std::string& code)
{
	if (!_has_project)
		return;
	_project.language = code;
	if (_project.is_folder)
		_project.save_config();
	else if (util::Recent* r = _s.find_recent(_project.docs[0].src_path))
		r->language = code;
	_s.save();
	if (!_spell.load(code))
		status(i18n::fmt(TR("MSG_SPELL_MISSING_FOR"), code.c_str()));
	else
		status(i18n::fmt(TR("MSG_PROJECT_LANGUAGE"), code.c_str()));
	rebuild_spans();
}
void App::run_style()
{
	Pane& pane = _style_source ? _src : _dst;
	_style_report = style::analyze(editor_lines(pane.editor), _s.markdown, _has_project ? _project.language : "fr");
	_style_done = true;
	_style_marks.clear();
	for (size_t i = 0; i < _style_report.observations.size(); i++)
		for (const style::Passage& p : _style_report.observations[i].passages)
			_style_marks.push_back({ p.line, p.start, p.end, (int)i });
	rebuild_spans();
}

// Score contribution which an observation feeds
const style::Contribution* App::style_points(int kind)
{
	const char* key = kind == style::Connector ? "SCORE_CONNECTORS" : kind == style::StockPhrase ? "SCORE_STOCK_PHRASES" : (kind == style::CloseRepetition || kind == style::RepeatedPhrase) ? "SCORE_REPETITIONS"
		: kind == style::SameOpening ? "SCORE_OPENINGS" : kind == style::RedundantParagraphs ? "SCORE_REDUNDANCY" : kind == style::ConstructionFamily ? "SCORE_CONSTRUCTIONS" : kind == style::ParallelStructure ? "SCORE_PARALLELISM" : nullptr;
	if (key)
		for (const style::Contribution& c : _style_report.score.contributions)
			if (c.key == key)
				return &c;
	return nullptr;
}

// Advice for an observation: what to do, and for known French expressions some alternatives
std::string App::style_advice(const style::Observation& o)
{
	const char* key = o.kind == style::Connector ? "ADVICE_CONNECTOR" : o.kind == style::StockPhrase ? "ADVICE_STOCK_PHRASE" : o.kind == style::CloseRepetition ? "ADVICE_CLOSE_REPETITION"
		: o.kind == style::RepeatedPhrase ? "ADVICE_REPEATED_PHRASE" : o.kind == style::SameOpening ? "ADVICE_SAME_OPENING" : o.kind == style::LongSentence ? "ADVICE_LONG_SENTENCE"
		: o.kind == style::RedundantParagraphs ? "ADVICE_REDUNDANT_PARAGRAPHS" : o.kind == style::ConstructionFamily ? "ADVICE_CONSTRUCTION" : o.kind == style::ParallelStructure ? "ADVICE_PARALLEL_STRUCTURE" : o.kind == style::StyleBreak ? "ADVICE_STYLE_BREAK" : "ADVICE_CONSTRUCTION";
	std::string advice = TR(key);
	if (french() && (o.kind == style::Connector || o.kind == style::StockPhrase))
	{
		static const char* const alternatives[][2] = { { "en effet", "car, puisque, (ou rien)" }, { "de plus", "et, aussi, puis, (ou rien)" }, { "par ailleurs", "d'autre part, sinon, (ou rien)" },
			{ "en outre", "et, aussi" }, { "toutefois", "mais, pourtant" }, { "néanmoins", "mais, pourtant, malgré cela" }, { "cependant", "mais, pourtant" }, { "ainsi", "donc, alors, c'est pourquoi" },
			{ "par conséquent", "donc, alors" }, { "en conséquence", "donc, alors" }, { "dès lors", "alors, depuis" }, { "en définitive", "finalement, au fond" }, { "en résumé", "bref, en deux mots" },
			{ "en conclusion", "pour finir, (ou rien)" }, { "dans ce contexte", "alors, dans ce cas" }, { "à cet égard", "sur ce point" }, { "dans cette optique", "dans ce but, pour cela" },
			{ "il est important de noter", "(dire directement la chose)" }, { "il convient de souligner", "(dire directement la chose)" }, { "force est de constater", "on voit que, on constate que" }, { "cela étant dit", "mais, pourtant" } };
		for (const auto& alt : alternatives)
			if (o.text == alt[0])
			{
				advice += "  " + i18n::fmt(TR("ADVICE_ALTERNATIVES"), alt[1]);
				break;
			}
	}
	return advice;
}