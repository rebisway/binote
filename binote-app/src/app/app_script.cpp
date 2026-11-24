// Binote - Python scripts: menu, dialog box of the properties, changes in the text, editor of the scripts
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app.h"
#include "version.h"
#include "i18n.h"
#include "json.h"
#include "../common/dialog.h"
#include "../common/logger.h"
#include <TextEditor.h>
#include <algorithm>
#include <filesystem>
#include <GLFW/glfw3.h>
#include "stb/stb_image.h"
#include <imgui_internal.h>

namespace fs = std::filesystem;

//-----------------------------------------------------------------------------
// Context given to a script
//-----------------------------------------------------------------------------

// Text of the lines between two positions
static std::string text_between(const std::vector<std::string>& lines, ImGuiProsePos a, ImGuiProsePos b)
{
	std::string out;
	for (int l = a.Line; l <= b.Line && l <= (int)lines.size(); l++)
	{
		const std::string& s = lines[l - 1];
		const int from = l == a.Line ? std::min<int>(a.Column, (int)s.size()) : 0;
		const int to = l == b.Line ? std::min<int>(b.Column, (int)s.size()) : (int)s.size();
		out += s.substr(from, std::max(0, to - from));
		if (l < b.Line)
			out += "\n";
	}
	return out;
}

// The selection, the paragraph of the caret, its section, the file and the files of the project, as JSON for the Textcore object
std::string App::script_context()
{
	ImGuiProseEditor* ed = _dst.editor;
	const std::vector<std::string> lines = editor_lines(ed);
	const int count = (int)lines.size();
	const int caret = ImGuiProse::GetCaret(ed).Line;
	_script_selection = _script_paragraph = _script_section = _script_file = ScriptRange();

	_script_file.valid = count > 0;
	_script_file.start = ImGuiProsePos(1, 0);
	_script_file.end = ImGuiProsePos(count, count > 0 ? (int)lines.back().size() : 0);
	if (ImGuiProse::GetSelection(ed, &_script_selection.start, &_script_selection.end))
		_script_selection.valid = true;
	if (caret >= 1 && caret <= count && lines[caret - 1].find_first_not_of(" \t") != std::string::npos)
	{
		_script_paragraph.valid = true;
		_script_paragraph.start = ImGuiProsePos(caret, 0);
		_script_paragraph.end = ImGuiProsePos(caret, (int)lines[caret - 1].size());
	}
	// Section: from the heading before the caret to the next heading of the same level or above
	{
		const std::vector<Heading> heads = parse_headings(lines, _s.markdown);
		int first = 1, last = count, level = 99;
		const Heading* current = nullptr;
		for (const Heading& h : heads)
			if (h.line <= caret)
				current = &h;
		if (current)
		{
			first = current->line;
			level = current->level;
		}
		for (const Heading& h : heads)
			if (h.line > first && (!current || h.level <= level))
			{
				last = h.line - 1;
				break;
			}
		last = std::max(first, last);
		if (count > 0)
		{
			_script_section.valid = true;
			_script_section.start = ImGuiProsePos(first, 0);
			_script_section.end = ImGuiProsePos(last, (int)lines[last - 1].size());
		}
	}

	const Doc* doc = current_doc();
	std::string j = "{";
	j += "\"text_language\":" + json::quote(_has_project ? _project.language : "fr");
	j += std::string(",\"markdown\":") + (_s.markdown ? "true" : "false");
	j += ",\"selection\":" + json::quote(_script_selection.valid ? text_between(lines, _script_selection.start, _script_selection.end) : "");
	j += ",\"paragraph\":" + json::quote(_script_paragraph.valid ? lines[caret - 1] : "");
	j += ",\"section\":" + json::quote(_script_section.valid ? text_between(lines, _script_section.start, _script_section.end) : "");
	const std::string file_text = editor_text(ed);
	j += ",\"file\":" + json::quote(file_text);
	j += ",\"file_name\":" + json::quote(doc ? doc->rel : "");
	j += ",\"project_name\":" + json::quote(_project.name);
	j += ",\"files\":{";
	for (int i = 0; i < (int)_project.docs.size(); i++)
	{
		std::string text;
		if (i == _cur)
			text = file_text;
		else if (!(util::exists(_project.docs[i].dst_path) && util::read_text(_project.docs[i].dst_path, text)))
			util::read_text(_project.docs[i].src_path, text);
		j += std::string(i ? "," : "") + json::quote(_project.docs[i].rel) + ":" + json::quote(text);
	}
	j += "}}";
	return j;
}

//-----------------------------------------------------------------------------
// Running a script, applying its changes
//-----------------------------------------------------------------------------

// The icon of the application as a texture (file of the icons folder beside the executable, or of the sources)
static ImTextureID about_icon(ImVec2* size)
{
	static GLuint tex = 0;
	static int w = 0, h = 0;
	static bool tried = false;
	if (!tried)
	{
		tried = true;
		const std::string app = util::application_path();
		const std::string candidates[] = { util::combine(app, "icons/binote-256.png"), util::combine(app, "../icons/binote-256.png"), util::combine(app, "../binote-app/icons/binote-256.png"), util::combine(app, "../../binote-app/icons/binote-256.png") };
		for (const std::string& c : candidates)
		{
			int n;
			unsigned char* data = util::exists(c) ? stbi_load(c.c_str(), &w, &h, &n, 4) : nullptr;
			if (!data)
				continue;
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			stbi_image_free(data);
			break;
		}
	}
	*size = ImVec2((float)w, (float)h);
	return (ImTextureID)(intptr_t)tex;
}

// About box
void App::draw_about()
{
	if (_open_about)
	{
		_open_about = false;
		ImGui::OpenPopup("###about");
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f), ImGuiCond_Appearing);
	const std::string title = std::string(TR("ABOUT_TITLE")) + "###about";
	if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
		return;
	ImVec2 icon_size;
	const ImTextureID icon = about_icon(&icon_size);
	const float icon_px = ImGui::GetFontSize() * 4.5f;
	if (icon_size.x > 0.0f)
	{
		ImGui::Image(icon, ImVec2(icon_px, icon_px));
		ImGui::SameLine();
	}
	ImGui::BeginGroup();
	ImGui::Spacing();
	if (_fonts.heading[0])
		ImGui::PushFont(_fonts.heading[0]);
	ImGui::TextUnformatted("Binote");
	if (_fonts.heading[0])
		ImGui::PopFont();
	ImGui::Text("%s", i18n::fmt(TR("ABOUT_VERSION"), BINOTE_VERSION, __DATE__).c_str());
	ImGui::EndGroup();
	ImGui::Spacing();
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
	ImGui::Text("\xC2\xA9 2026 %s - %s", BINOTE_AUTHOR, BINOTE_URL);
	ImGui::TextDisabled("%s", TR("ABOUT_LICENSE"));
	ImGui::Separator();
	ImGui::TextUnformatted(TR("ABOUT_COMPONENTS"));
	ImGui::TextDisabled("Dear ImGui (MIT), GLFW (zlib), md4c (MIT), imgui-md and imgui-prose (MIT), Hunspell (MPL/GPL/LGPL), Grammalecte lexicon (MPL 2.0), ImGuiColorTextEdit (MIT), stb (public domain)");
	ImGui::TextDisabled("DejaVu fonts (free license), Roboto (Apache 2.0)");
	const std::string python = pyscript::version();
	if (python.empty())
		ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.35f, 1.0f), "%s", TR("ABOUT_PYTHON_MISSING"));
	else
		ImGui::TextDisabled("%s", i18n::fmt(TR("ABOUT_PYTHON_OK"), python.c_str()).c_str());
	ImGui::PopTextWrapPos();
	ImGui::Spacing();
	const float w = 120.0f;
	ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) * 0.5f);
	if (ImGui::Button(TR("BTN_CLOSE"), ImVec2(w, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

void App::start_script(const std::string& path)
{
	if (!_has_project)
		return;
	_script_path = path;
	_script_preview_changes = -1;
	std::string error;
	if (!_script.begin(path, i18n::current(), error))
	{
		Logger::error("Script " + fs::u8path(path).stem().u8string() + ": " + error);
		_s.show_log = true;
		status(i18n::fmt(TR("MSG_SCRIPT_FAILED"), fs::u8path(path).stem().u8string().c_str()));
		return;
	}
	if (_script.has_dialog())
		_script_active = true;							// the dialog box runs the script when Ok is pressed
	else
		run_script_now();								// the script shows its own dialog box (tkinter), or has no parameter
}

void App::run_script_now()
{
	_script_active = false;
	const pyscript::Result result = _script.run(script_context());
	_script.end();
	apply_script(result);
}

// Dialog box of the properties, each frame
void App::draw_script()
{
	if (!_script_active)
		return;
	// The preview: the script runs, nothing is changed; the lines which would change are listed under the parameters
	auto preview = [this]()
	{
		if (_script_preview_changes < 0)
			return;
		ImGui::Separator();
		if (_script_preview_changes == 0)
			ImGui::TextDisabled("%s", TR("SCRIPT_PREVIEW_NONE"));
		else
			ImGui::TextDisabled("%s", i18n::fmt(TR("SCRIPT_PREVIEW_LINES"), _script_preview_changes).c_str());
		ImGui::BeginChild("##script_preview", ImVec2(ImGui::GetFontSize() * 34.0f, ImGui::GetTextLineHeight() * 12.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
		int shown = 0;
		for (const diff::Line& l : _script_preview)
		{
			if (l.sign == ' ')
				continue;
			if (shown++ > 400)
				break;
			std::string text = l.text;
			if (text.size() > 200)
				text.resize(200);
			ImGui::TextColored(l.sign == '+' ? ImVec4(0.45f, 0.85f, 0.5f, 1) : ImVec4(0.95f, 0.45f, 0.45f, 1), "%c %s", l.sign, text.c_str());
		}
		ImGui::EndChild();
	};
	int r = _script.draw(TR("BTN_OK"), TR("BTN_CANCEL"), TR("SCRIPT_PREVIEW"), preview);
	if (r == 3)
	{
		const pyscript::Result result = _script.run(script_context());
		_script_preview.clear();
		_script_preview_changes = 0;
		if (!result.ok)
		{
			Logger::error("Script " + fs::u8path(_script_path).stem().u8string() + ": " + result.error);
			_script_preview.push_back({ '-', result.error });
			_script_preview_changes = 1;
		}
		else
		{
			const std::vector<std::string> lines = editor_lines(_dst.editor);
			for (const pyscript::Command& c : result.commands)
			{
				const ScriptRange* range = c.op == "selection" ? &_script_selection : c.op == "paragraph" ? &_script_paragraph : c.op == "section" ? &_script_section : (c.op == "file" ? &_script_file : nullptr);
				if (!range || !range->valid)
					continue;
				for (const diff::Line& l : diff::lines(text_between(lines, range->start, range->end), c.text, true))
				{
					_script_preview.push_back(l);
					_script_preview_changes++;
				}
			}
		}
		r = 0;
	}
	if (_script_ok)
	{
		_script_ok = false;
		r = 1;
	}
	if (r == 1)
	{
		run_script_now();
		_script_preview_changes = -1;
	}
	else if (r == 2)
	{
		_script_active = false;
		_script_preview_changes = -1;
	}
}

void App::apply_script(const pyscript::Result& result)
{
	const std::string name = fs::u8path(_script_path).stem().u8string();
	if (!result.output.empty())
		Logger::log("[" + name + "] " + result.output);
	if (!result.ok)
	{
		Logger::error("Script " + name + ": " + result.error);
		_s.show_log = true;
		status(i18n::fmt(TR("MSG_SCRIPT_FAILED"), name.c_str()));
		// The line of the error is marked in the editor of the scripts
		if (_script_text && _script_path == _script_edit_path)
		{
			const std::string key = "File \"" + _script_path + "\", line ";
			const size_t at = result.error.rfind(key);
			if (at != std::string::npos)
			{
				const int line = std::atoi(result.error.c_str() + at + key.size());
				if (line > 0)
				{
					TextEditor::ErrorMarkers markers;
					markers[line] = result.error.substr(result.error.rfind('\n', result.error.size() - 2) + 1);
					_script_text->SetErrorMarkers(markers);
					_script_text->SetCursorPosition(TextEditor::Coordinates(line - 1, 0));
				}
			}
		}
		return;
	}
	if (_script_text && _script_path == _script_edit_path)
		_script_text->SetErrorMarkers(TextEditor::ErrorMarkers());

	ImGuiProseEditor* ed = _dst.editor;
	const std::vector<std::string> lines = editor_lines(ed);
	struct Edit { ImGuiProsePos start, end; std::string text; };
	std::vector<Edit> edits;
	int changes = 0;
	Doc* doc = current_doc();
	for (const pyscript::Command& c : result.commands)
	{
		if (c.op == "log")
		{
			Logger::log("[" + name + "] " + c.text);
			continue;
		}
		const ScriptRange* range = c.op == "selection" ? &_script_selection : c.op == "paragraph" ? &_script_paragraph : c.op == "section" ? &_script_section : c.op == "file" ? &_script_file : nullptr;
		if (c.op == "in")
		{
			// Another file of the project: written in its destination (the current one goes through the editor, to be undone)
			if (doc && c.name == doc->rel)
				range = &_script_file;
			else
			{
				for (Doc& d : _project.docs)
					if (d.rel == c.name)
					{
						std::string old;
						if (!(util::exists(d.dst_path) && util::read_text(d.dst_path, old)))
							util::read_text(d.src_path, old);
						if (old != c.text && util::write_text(d.dst_path, c.text, d.crlf))
						{
							History::add(d.dst_path, c.text, "script");
							changes++;
						}
					}
				continue;
			}
		}
		if (!range || !range->valid)
			continue;
		if (text_between(lines, range->start, range->end) != c.text)
			edits.push_back({ range->start, range->end, c.text });
	}
	// From the end of the text to the start, so the positions stay valid; an edit inside one already done is dropped
	std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.start.Line != b.start.Line ? a.start.Line > b.start.Line : a.start.Column > b.start.Column; });
	bool have_last = false;
	ImGuiProsePos last_start;
	for (const Edit& e : edits)
	{
		if (have_last && (e.end.Line > last_start.Line || (e.end.Line == last_start.Line && e.end.Column > last_start.Column)))
			continue;
		ImGuiProse::Replace(ed, e.start, e.end, e.text.c_str());
		last_start = e.start;
		have_last = true;
		changes++;
	}
	if (!edits.empty() && doc)
		doc->dirty = true;
	status(changes ? i18n::fmt(TR("MSG_SCRIPT_DONE"), name.c_str(), changes) : i18n::fmt(TR("MSG_SCRIPT_UNCHANGED"), name.c_str()));
}

//-----------------------------------------------------------------------------
// Scripts menu: one menu per category, then the editor
//-----------------------------------------------------------------------------

void App::draw_script_menu()
{
	if (!ImGui::BeginMenu(TR("MENU_SCRIPTS")))
		return;
	if (!_scripts_loaded)
	{
		_scripts = pyscript::list(i18n::current());
		_scripts_loaded = true;
	}
	if (_scripts.empty())
	{
		if (!pyscript::available())
			ImGui::TextDisabled("%s", i18n::fmt(TR("SCRIPTS_PYTHON_MISSING"), pyscript::last_error().c_str()).c_str());
		else
			ImGui::TextDisabled("%s", TR("SCRIPTS_NONE"));
	}
	std::vector<std::string> categories;
	for (const pyscript::Info& s : _scripts)
		if (std::find(categories.begin(), categories.end(), s.category) == categories.end())
			categories.push_back(s.category);
	for (const std::string& category : categories)
	{
		if (!ImGui::BeginMenu(category.empty() ? "-" : category.c_str()))
			continue;
		for (size_t i = 0; i < _scripts.size(); i++)
		{
			const pyscript::Info& s = _scripts[i];
			if (s.category != category)
				continue;
			ImGui::PushID((int)i);
			if (ImGui::MenuItem(s.description.c_str(), nullptr, false, _has_project && s.error.empty()))
				start_script(s.path);
			if (!s.error.empty() && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", s.error.c_str());
			ImGui::PopID();
		}
		ImGui::EndMenu();
	}
	ImGui::Separator();
	if (ImGui::MenuItem(TR("SCRIPTS_EDIT")))
		_show_script_editor = true;
	if (ImGui::MenuItem(TR("SCRIPTS_REFRESH")))
		_scripts_loaded = false;
	if (ImGui::MenuItem(TR("SCRIPTS_OPEN_FOLDER")))
		dialog::open_folder(pyscript::user_folder());
	ImGui::EndMenu();
}

//-----------------------------------------------------------------------------
// Editor of the scripts: edit, check the syntax, test
//-----------------------------------------------------------------------------

static const TextEditor::LanguageDefinition& python_language()
{
	static TextEditor::LanguageDefinition lang;
	static bool inited = false;
	if (!inited)
	{
		inited = true;
		lang.mName = "Python";
		static const char* const keywords[] = { "and", "as", "assert", "break", "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is",
			"lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield", "None", "True", "False", "self" };
		for (const char* k : keywords)
			lang.mKeywords.insert(k);
		static const char* const builtins[] = { "print", "len", "range", "str", "int", "float", "bool", "list", "dict", "set", "tuple", "open", "isinstance", "enumerate", "zip", "sorted", "min", "max", "sum",
			"abs", "round", "map", "filter", "any", "all", "reversed", "hasattr", "getattr", "super", "type" };
		for (const char* k : builtins)
		{
			TextEditor::Identifier id;
			id.mDeclaration = "Built-in function";
			lang.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}
		lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>("[rbfRBF]?\\\"(\\\\.|[^\\\"])*\\\"", TextEditor::PaletteIndex::String));
		lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>("[rbfRBF]?\\\'(\\\\.|[^\\\'])*\\\'", TextEditor::PaletteIndex::String));
		lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>("0[xX][0-9a-fA-F]+", TextEditor::PaletteIndex::Number));
		lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>("[0-9]+([.][0-9]*)?([eE][+-]?[0-9]+)?", TextEditor::PaletteIndex::Number));
		lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", TextEditor::PaletteIndex::Identifier));
		lang.mTokenRegexStrings.push_back(std::make_pair<std::string, TextEditor::PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.\\:]", TextEditor::PaletteIndex::Punctuation));
		lang.mSingleLineComment = "#";
		lang.mCommentStart = "";
		lang.mCommentEnd = "";
		lang.mCaseSensitive = true;
		lang.mAutoIndentation = true;
	}
	return lang;
}

// Model of a new script
static const char* const SCRIPT_TEMPLATE =
	"# Binote script: describe what it does here.\n"
	"# A script is a class Textscript. Binote shows its properties in a dialog box, then calls run().\n"
	"# See README_scripts.txt, and fix_spaces.py as a model.\n"
	"\n"
	"\n"
	"class Textscript:\n"
	"    def __init__(self):\n"
	"        self.scope = 3		# index of the part of the text to work on: selection, paragraph, section, file, project\n"
	"\n"
	"        self.en_us = {'description': 'My script', 'scope': 'Apply to', 'choices': ['Selection', 'Paragraph', 'Section', 'File', 'Project']}\n"
	"        self.fr_fr = {'description': 'Mon script', 'scope': 'Appliquer à', 'choices': ['Sélection', 'Paragraphe', 'Section', 'Fichier', 'Projet']}\n"
	"        self.es_es = {'description': 'Mi script', 'scope': 'Aplicar a', 'choices': ['Selección', 'Párrafo', 'Sección', 'Archivo', 'Proyecto']}\n"
	"        self.Language = {'en-us': self.en_us, 'fr-fr': self.fr_fr, 'es-es': self.es_es}\n"
	"\n"
	"    def set_language(self, value):\n"
	"        self.lang = self.Language.get(value, self.en_us)	# unknown language: English\n"
	"\n"
	"    def get_description(self):\n"
	"        return self.lang['description']\n"
	"\n"
	"    def get_category(self):\n"
	"        return 'text'\n"
	"\n"
	"    # set the reference to the core of Binote (text, selection, changes), do not modify\n"
	"    def set_core(self, value):\n"
	"        self.core = value\n"
	"\n"
	"    def get_properties_count(self):\n"
	"        return 1\n"
	"\n"
	"    def get_property_key(self, index):\n"
	"        return 'scope'\n"
	"\n"
	"    def get_property_description(self, index):\n"
	"        return self.lang['scope']\n"
	"\n"
	"    def get_property(self, index):\n"
	"        return [str(self.scope)] + self.lang['choices']\n"
	"\n"
	"    def set_property(self, index, val):\n"
	"        self.scope = int(val)\n"
	"\n"
	"    # the text to transform comes in, the new text goes out\n"
	"    def change(self, text):\n"
	"        return text.upper()\n"
	"\n"
	"    def run(self):\n"
	"        self.core.transform(self.change, ('selection', 'paragraph', 'section', 'file', 'project')[self.scope])\n";

void App::free_script_editor()
{
	delete _script_text;
	_script_text = nullptr;
}

void App::load_script_in_editor(const std::string& path)
{
	std::string text;
	if (!util::read_text(path, text))
		return;
	_script_edit_path = path;
	const std::string user = pyscript::user_folder();
	_script_edit_user = path.compare(0, user.size(), user) == 0;
	_script_text->SetText(text);
	_script_text->SetReadOnly(!_script_edit_user);
	_script_text->SetErrorMarkers(TextEditor::ErrorMarkers());
	_script_edit_dirty = false;
}

bool App::save_script_in_editor()
{
	if (_script_edit_path.empty() || !_script_edit_user)
		return false;
	if (!util::write_text(_script_edit_path, _script_text->GetText()))
		return false;
	_script_edit_dirty = false;
	_scripts_loaded = false;							// the description may have changed
	status(i18n::fmt(TR("MSG_SCRIPT_SAVED"), util::file_name(_script_edit_path).c_str()));
	return true;
}

void App::draw_script_editor()
{
	if (!_show_script_editor)
		return;
	if (!_script_text)
	{
		_script_text = new TextEditor();
		_script_text->SetLanguageDefinition(python_language());
		_script_text->SetShowWhitespaces(false);
		_script_text->SetColorizerEnable(true);
		_script_text->SetTabSize(4);
		_script_edit_dark = !_s.dark;					// forces the palette
	}
	if (_script_edit_dark != _s.dark)
	{
		_script_text->SetPalette(_s.dark ? TextEditor::GetDarkPalette() : TextEditor::GetLightPalette());
		_script_edit_dark = _s.dark;
	}
	ImGui::SetNextWindowSize(ImVec2(860.0f, 620.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(120.0f, 90.0f), ImGuiCond_FirstUseEver);
	const std::string title = std::string(TR("SCRIPT_EDITOR_TITLE")) + (_script_edit_dirty ? " *" : "") + "###script_editor";
	if (!ImGui::Begin(title.c_str(), &_show_script_editor))
	{
		ImGui::End();
		return;
	}
	if (!_scripts_loaded)
	{
		_scripts = pyscript::list(i18n::current());
		_scripts_loaded = true;
	}
	if (_script_edit_path.empty() && !_scripts.empty())
		load_script_in_editor(_scripts[0].path);
	// The script: a combo of all of them, then the buttons
	std::string current = _script_edit_path.empty() ? "" : util::file_name(_script_edit_path);
	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::BeginCombo("##script_choice", current.c_str()))
	{
		for (const pyscript::Info& s : _scripts)
		{
			if (ImGui::Selectable(util::file_name(s.path).c_str(), s.path == _script_edit_path))
				load_script_in_editor(s.path);
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button(TR("SCRIPT_NEW")))
	{
		_script_name_mode = 1;
		_script_name[0] = 0;
		ImGui::OpenPopup("##script_name");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(_script_edit_path.empty());
	if (ImGui::Button(TR("SCRIPT_COPY")))
	{
		_script_name_mode = 2;
		snprintf(_script_name, sizeof(_script_name), "%s_copy", fs::u8path(_script_edit_path).stem().u8string().c_str());
		ImGui::OpenPopup("##script_name");
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!_script_edit_user || !_script_edit_dirty);
	if (ImGui::Button(TR("BTN_SAVE")))
		save_script_in_editor();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(_script_edit_path.empty());
	if (ImGui::Button(TR("SCRIPT_CHECK")))
	{
		int line = 0, column = 0;
		const std::string name = util::file_name(_script_edit_path);
		const std::string error = pyscript::check_syntax(_script_text->GetText(), name, line, column);
		if (error.empty())
		{
			_script_text->SetErrorMarkers(TextEditor::ErrorMarkers());
			status(i18n::fmt(TR("MSG_SCRIPT_SYNTAX_OK"), name.c_str()));
		}
		else
		{
			Logger::error("Script " + name + " line " + std::to_string(line) + ": " + error);
			_s.show_log = true;
			status(i18n::fmt(TR("MSG_SCRIPT_SYNTAX_ERROR"), name.c_str(), line, error.c_str()));
			if (line > 0)
			{
				TextEditor::ErrorMarkers markers;
				markers[line] = error;
				_script_text->SetErrorMarkers(markers);
				_script_text->SetCursorPosition(TextEditor::Coordinates(line - 1, column > 0 ? column - 1 : 0));
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(TR("SCRIPT_RUN")) && _has_project)
	{
		if (_script_edit_dirty)
			save_script_in_editor();
		start_script(_script_edit_path);
	}
	ImGui::EndDisabled();
	if (!_script_edit_path.empty() && !_script_edit_user)
		ImGui::TextDisabled("%s", TR("SCRIPT_READ_ONLY"));

	// Name of a new script or of a copy
	if (ImGui::BeginPopup("##script_name"))
	{
		ImGui::TextUnformatted(TR("SCRIPT_NAME"));
		ImGui::SetNextItemWidth(260.0f);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		const bool enter = ImGui::InputText("##name", _script_name, sizeof(_script_name), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
		std::string name = _script_name;
		const bool exists = !name.empty() && util::exists(util::combine(pyscript::user_folder(), name + ".py"));
		if (exists)
			ImGui::TextDisabled("%s", TR("SCRIPT_EXISTS"));
		ImGui::BeginDisabled(name.empty() || exists);
		if (ImGui::Button(TR("BTN_OK")) || (enter && !name.empty() && !exists))
		{
			const std::string path = util::combine(pyscript::user_folder(), name + ".py");
			std::string text = SCRIPT_TEMPLATE;
			if (_script_name_mode == 2)
				text = _script_text->GetText();
			if (util::write_text(path, text))
			{
				_scripts_loaded = false;
				load_script_in_editor(path);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}

	ImGui::Separator();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f));
	if (_fonts.mono)
		ImGui::PushFont(_fonts.mono);
	_script_text->Render("##script_text");
	if (_fonts.mono)
		ImGui::PopFont();
	ImGui::PopStyleVar();
	if (_script_text->IsTextChanged() && _script_edit_user)
		_script_edit_dirty = true;
	ImGui::End();
}
