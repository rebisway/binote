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

#include "common/util.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <Windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace util
{
	// Path of the executable
	std::string application_full_path()
	{
#ifdef _WIN32
		wchar_t buf[MAX_PATH];
		DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
		return fs::path(std::wstring(buf, n)).u8string();
#else
		char buf[PATH_MAX];
		ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
		return n > 0 ? std::string(buf, n) : std::string();
#endif
	}

	// Folder of the executable (dictionaries, fonts, lexicon, scripts and languages are next to it)
	std::string application_path()
	{
#ifdef _WIN32
		wchar_t buf[MAX_PATH];
		DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
		return fs::path(std::wstring(buf, n)).parent_path().string();
#else
		char buf[PATH_MAX];
		ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
		if (n <= 0)
			return ".";
		buf[n] = 0;
		return fs::path(buf).parent_path().string();
#endif
	}

	// Folder of the user data (%AppData%\Binote, ~/.config/binote): settings, history, goals, dictionary, scripts
	std::string data_path()
	{
		fs::path p;
#ifdef _WIN32
		const char* appdata = std::getenv("APPDATA");
		p = fs::path(appdata ? appdata : ".") / "Binote";
#else
		const char* xdg = std::getenv("XDG_CONFIG_HOME");
		const char* home = std::getenv("HOME");
		p = xdg ? fs::path(xdg) / "binote" : fs::path(home ? home : ".") / ".config" / "binote";
#endif
		std::error_code ec;
		fs::create_directories(p, ec);
		return p.string();
	}

	// Joins two path parts with a separator
	std::string combine(const std::string& left, const std::string& right)
	{
		return (fs::u8path(left) / fs::u8path(right)).u8string();
	}

	std::string file_name(const std::string& path)			{ return fs::u8path(path).filename().u8string(); }
	std::string parent_path(const std::string& path)		{ return fs::u8path(path).parent_path().u8string(); }

	// Extension of a file name in lower case, with the dot
	std::string extension(const std::string& path)
	{
		std::string e = fs::u8path(path).extension().u8string();
		std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return e;
	}

	bool exists(const std::string& path)					{ std::error_code ec; return fs::exists(fs::u8path(path), ec); }
	bool is_directory(const std::string& path)				{ std::error_code ec; return fs::is_directory(fs::u8path(path), ec); }

	// True when the bytes are valid UTF-8
	static bool valid_utf8(const std::string& s)
	{
		size_t i = 0, n = s.size();
		while (i < n)
		{
			unsigned char c = (unsigned char)s[i];
			int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
			if (len == 0 || i + len > n)
				return false;
			for (int k = 1; k < len; k++)
				if (((unsigned char)s[i + k] & 0xC0) != 0x80)
					return false;
			i += len;
		}
		return true;
	}

	// Appends a code point in UTF-8
	static void append_utf8(std::string& out, unsigned int c)
	{
		if (c < 0x80) out += (char)c;
		else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
		else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
	}

	// Reads a text file as UTF-8 (BOM removed; a text which is not valid UTF-8 is read as Windows-1252). The line breaks become LF and crlf tells whether the file used CR LF.
	bool read_text(const std::string& path, std::string& text, bool* crlf)
	{
		std::ifstream f(fs::u8path(path), std::ios::binary);
		if (!f)
			return false;
		std::stringstream ss;
		ss << f.rdbuf();
		std::string raw = ss.str();
		if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
			raw.erase(0, 3);
		if (!valid_utf8(raw))
		{
			// Windows-1252
			static const unsigned short cp1252[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
				0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
			std::string out;
			for (unsigned char c : raw)
				append_utf8(out, (c >= 0x80 && c < 0xA0) ? cp1252[c - 0x80] : c);
			raw = out;
		}
		bool has_crlf = raw.find("\r\n") != std::string::npos;
		if (crlf)
			*crlf = has_crlf;
		text.clear();
		text.reserve(raw.size());
		for (char c : raw)
			if (c != '\r')
				text += c;
		return true;
	}

	// Writes a text file in UTF-8 with the line breaks asked for
	bool write_text(const std::string& path, const std::string& text, bool crlf)
	{
		std::error_code ec;
		fs::create_directories(fs::u8path(path).parent_path(), ec);
		std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
		if (!f)
			return false;
		if (!crlf)
			f.write(text.data(), (std::streamsize)text.size());
		else
			for (char c : text)
			{
				if (c == '\n')
					f.put('\r');
				f.put(c);
			}
		return (bool)f;
	}

	// Order of the file names with the numbers in their natural order (2 before 10)
	bool natural_less(const std::string& a, const std::string& b)
	{
		size_t i = 0, j = 0;
		while (i < a.size() && j < b.size())
		{
			if (std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j]))
			{
				size_t ei = i, ej = j;
				while (ei < a.size() && std::isdigit((unsigned char)a[ei])) ei++;
				while (ej < b.size() && std::isdigit((unsigned char)b[ej])) ej++;
				std::string na = a.substr(i, ei - i), nb = b.substr(j, ej - j);
				na.erase(0, std::min(na.find_first_not_of('0'), na.size()));
				nb.erase(0, std::min(nb.find_first_not_of('0'), nb.size()));
				if (na.size() != nb.size())
					return na.size() < nb.size();
				if (na != nb)
					return na < nb;
				i = ei;
				j = ej;
			}
			else
			{
				int ca = std::tolower((unsigned char)a[i]), cb = std::tolower((unsigned char)b[j]);
				if (ca != cb)
					return ca < cb;
				i++;
				j++;
			}
		}
		return (a.size() - i) < (b.size() - j);
	}

	// Lines of a text cut at LF (a final line break gives a last empty line)
	std::vector<std::string> split_lines(const std::string& text)
	{
		std::vector<std::string> lines;
		size_t pos = 0;
		while (pos <= text.size())
		{
			size_t eol = text.find('\n', pos);
			if (eol == std::string::npos)
				eol = text.size();
			lines.push_back(text.substr(pos, eol - pos));
			pos = eol + 1;
		}
		return lines;
	}

	//-----------------------------------------------------------------------------
	// Settings: key=value lines
	//-----------------------------------------------------------------------------

	// settings.ini
	static std::string settings_file()
	{
		return combine(data_path(), "settings.ini");
	}

	// Reads settings.ini; the missing keys keep their default value
	void Settings::load()
	{
		std::string text;
		if (!read_text(settings_file(), text))
			return;
		recents.clear();
		for (const std::string& line : split_lines(text))
		{
			size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
			const bool b = value == "1";
			if (key == "dark") dark = b;
			else if (key == "wrap") wrap = b;
			else if (key == "markdown") markdown = b;
			else if (key == "line_numbers") line_numbers = b;
			else if (key == "paragraph_numbers") paragraph_numbers = b;
			else if (key == "show_diff") show_diff = b;
			else if (key == "sync_scroll") sync_scroll = b;
			else if (key == "align_paragraphs") align_paragraphs = b;
			else if (key == "show_tree") show_tree = b;
			else if (key == "show_log") show_log = b;
			else if (key == "reopen_last") reopen_last = b;
			else if (key == "python_seen") python_seen = b;
			else if (key == "python_never") python_never = b;
			else if (key == "spell_check") spell_check = b;
			else if (key == "grammar_check") grammar_check = b;
			else if (key == "grammar_ignored") grammar_ignored = value;
			else if (key == "system_open_mode") system_open_mode = std::atoi(value.c_str());
			else if (key == "quote_style") quote_style = std::atoi(value.c_str());
			else if (key == "export_format") export_format = std::max(0, std::min(4, std::atoi(value.c_str())));
			else if (key == "export_author") export_author = value;
			else if (key == "log_height") log_height = (float)std::atof(value.c_str());
			else if (key == "reading_mode") reading_mode = b;
			else if (key == "dest_only") dest_only = b;
			else if (key == "autosave_on_switch") autosave_on_switch = b;
			else if (key == "auto_snapshot") auto_snapshot = b;
			else if (key == "show_nbsp") show_nbsp = b;
			else if (key == "show_invisible") show_invisible = b;
			else if (key == "auto_typography") auto_typography = b;
			else if (key == "mono_editor") mono_editor = b;
			else if (key == "highlight_mode") highlight_mode = std::atoi(value.c_str());
			else if (key == "accent") accent = std::max(0, std::min(5, std::atoi(value.c_str())));
			else if (key == "accent_custom") sscanf(value.c_str(), "%u,%u,%u,%u", &accent_custom[0], &accent_custom[1], &accent_custom[2], &accent_custom[3]);
			else if (key == "invisible_colors") sscanf(value.c_str(), "%u,%u,%u,%u,%u,%u,%u", &invisible_colors[0], &invisible_colors[1], &invisible_colors[2], &invisible_colors[3], &invisible_colors[4], &invisible_colors[5], &invisible_colors[6]);
			else if (key == "font_size") font_size = (float)std::atof(value.c_str());
			else if (key == "ui_font") ui_font = value;
			else if (key == "charmap_font") charmap_font = value;
			else if (key == "cjk_font") cjk_font = value;
			else if (key == "editor_font") editor_font = value;
			else if (key == "mono_font") mono_font = value;
			else if (key == "ui_font_size") ui_font_size = (float)std::atof(value.c_str());
			else if (key == "split") split = (float)std::atof(value.c_str());
			else if (key == "window_x") window_x = std::atoi(value.c_str());
			else if (key == "window_y") window_y = std::atoi(value.c_str());
			else if (key == "maximized") maximized = b;
			else if (key == "find_case") find_case = b;
			else if (key == "history_only_changes") history_only_changes = b;
			else if (key == "goto_kind") goto_kind = std::atoi(value.c_str());
			else if (key == "preview_source") preview_source = b;
			else if (key == "preview_destination") preview_destination = b;
			else if (key == "language") language = value;
			else if (key == "tree_width") tree_width = (float)std::atof(value.c_str());
			else if (key == "window_w") window_w = std::atoi(value.c_str());
			else if (key == "window_h") window_h = std::atoi(value.c_str());
			else if (key == "recent")
			{
				std::vector<std::string> p;
				size_t from = 0;
				for (size_t bar; (bar = value.find('|', from)) != std::string::npos; from = bar + 1)
					p.push_back(value.substr(from, bar - from));
				p.push_back(value.substr(from));
				if (p.size() >= 2)
				{
					Recent r;
					r.source = p[0];
					r.destination = p[1];
					if (p.size() >= 4) { r.last_doc = p[2]; r.last_line = std::atoi(p[3].c_str()); }
					if (p.size() >= 5) r.language = p[4];
					recents.push_back(r);
				}
			}
		}
	}

	// Writes settings.ini
	void Settings::save() const
	{
		std::ostringstream o;
		o << "dark=" << dark << "\nwrap=" << wrap << "\nmarkdown=" << markdown << "\nline_numbers=" << line_numbers
			<< "\nparagraph_numbers=" << paragraph_numbers << "\nshow_diff=" << show_diff << "\nsync_scroll=" << sync_scroll << "\nalign_paragraphs=" << align_paragraphs
			<< "\nshow_tree=" << show_tree << "\nshow_log=" << show_log << "\nreopen_last=" << reopen_last << "\npython_seen=" << python_seen << "\npython_never=" << python_never << "\nspell_check=" << spell_check << "\ngrammar_check=" << grammar_check << "\ngrammar_ignored=" << grammar_ignored << "\nsystem_open_mode=" << system_open_mode << "\nquote_style=" << quote_style << "\nexport_format=" << export_format << "\nexport_author=" << export_author << "\nlog_height=" << log_height << "\nreading_mode=" << reading_mode << "\ndest_only=" << dest_only << "\nautosave_on_switch=" << autosave_on_switch << "\nauto_snapshot=" << auto_snapshot
			<< "\nshow_nbsp=" << show_nbsp << "\nshow_invisible=" << show_invisible << "\nauto_typography=" << auto_typography << "\nmono_editor=" << mono_editor
			<< "\nhighlight_mode=" << highlight_mode << "\naccent=" << accent << "\naccent_custom=" << accent_custom[0] << "," << accent_custom[1] << "," << accent_custom[2] << "," << accent_custom[3]
			<< "\ninvisible_colors=" << invisible_colors[0] << "," << invisible_colors[1] << "," << invisible_colors[2] << "," << invisible_colors[3] << "," << invisible_colors[4] << "," << invisible_colors[5] << "," << invisible_colors[6] << "\nfont_size=" << font_size << "\ntree_width=" << tree_width
			<< "\nlanguage=" << language << "\ncjk_font=" << cjk_font << "\ncharmap_font=" << charmap_font << "\nui_font=" << ui_font << "\neditor_font=" << editor_font << "\nmono_font=" << mono_font << "\nui_font_size=" << ui_font_size << "\nsplit=" << split << "\nwindow_x=" << window_x << "\nwindow_y=" << window_y << "\nmaximized=" << maximized << "\nfind_case=" << find_case << "\nhistory_only_changes=" << history_only_changes << "\ngoto_kind=" << goto_kind << "\npreview_source=" << preview_source << "\npreview_destination=" << preview_destination << "\nwindow_w=" << window_w << "\nwindow_h=" << window_h << "\n";
		for (const Recent& r : recents)
			o << "recent=" << r.source << "|" << r.destination << "|" << r.last_doc << "|" << r.last_line << "|" << r.language << "\n";
		write_text(settings_file(), o.str());
	}

	// The fonts installed on the system which the interface can use
	std::vector<FontFile> list_fonts()
	{
		std::vector<FontFile> fonts;
		auto scan = [&](const fs::path& dir, bool recursive, bool bundled)
		{
			std::error_code ec;
			if (!fs::is_directory(dir, ec))
				return;
			auto add = [&](const fs::directory_entry& e)
			{
				std::string ext = e.path().extension().string();
				for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
				if (ext != ".ttf" && ext != ".otf" && ext != ".ttc")
					return;
				const std::string name = e.path().stem().u8string();
				for (const FontFile& f : fonts)
					if (f.name == name)
						return;
				fonts.push_back({ name, bundled ? e.path().filename().u8string() : e.path().u8string() });
			};
			if (recursive)
				for (const auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
					add(e);
			else
				for (const auto& e : fs::directory_iterator(dir, ec))
					add(e);
		};
		scan(fs::u8path(combine(application_path(), "fonts")), false, true);
		const size_t bundled = fonts.size();
#ifdef _WIN32
		const char* windir = std::getenv("WINDIR");
		scan(fs::path(windir ? windir : "C:\\Windows") / "Fonts", false, false);
#else
		scan("/usr/share/fonts", true, false);
		scan("/usr/local/share/fonts", true, false);
		if (const char* home = std::getenv("HOME"))
			scan(fs::path(home) / ".local/share/fonts", true, false);
#endif
		std::sort(fonts.begin() + bundled, fonts.end(), [](const FontFile& a, const FontFile& b) { return a.name < b.name; });
		return fonts;
	}

	// The entry of the recent files for a source, or null
	Recent* Settings::find_recent(const std::string& source)
	{
		for (Recent& r : recents)
			if (r.source == source)
				return &r;
		return nullptr;
	}

	// Puts a project at the top of the recent files (the list is limited)
	void Settings::add_recent(const std::string& source, const std::string& destination)
	{
		Recent keep;
		if (Recent* old = find_recent(source))
			keep = *old;
		recents.erase(std::remove_if(recents.begin(), recents.end(), [&](const Recent& r) { return r.source == source; }), recents.end());
		keep.source = source;
		keep.destination = destination;
		recents.insert(recents.begin(), keep);
		if (recents.size() > 15)
			recents.resize(15);
		save();
	}

	//-----------------------------------------------------------------------------
	// File associations
	//-----------------------------------------------------------------------------

#ifdef _WIN32
	// Writes a string value in the registry (Windows)
	static bool reg_set(const std::wstring& key, const wchar_t* name, const std::wstring& value)
	{
		return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
	}

	bool register_file_associations(std::string& message)
	{
		const std::wstring exe = fs::u8path(application_full_path()).wstring();
		const std::wstring prog = L"Software\\Classes\\Binote.Document";
		bool ok = reg_set(prog, nullptr, L"Binote")
			&& reg_set(prog + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0")
			&& reg_set(prog + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
		for (const wchar_t* ext : { L".md", L".markdown" })
		{
			// A value without data in OpenWithProgids puts Binote in the "Open with" list of the extension
			const std::wstring key = std::wstring(L"Software\\Classes\\") + ext + L"\\OpenWithProgids";
			HKEY h;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS)
			{
				RegSetValueExW(h, L"Binote.Document", 0, REG_NONE, nullptr, 0);
				RegCloseKey(h);
			}
			else
				ok = false;
		}
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
		message = ok ? "OK" : "registry";
		return ok;
	}
#else
	bool register_file_associations(std::string& message)
	{
		const char* home = std::getenv("HOME");
		if (!home)
		{
			message = "HOME";
			return false;
		}
		const std::string exe = application_full_path();
		const std::string icon = combine(combine(application_path(), "icons"), "binote-256.png");
		const std::string desktop = combine(combine(home, ".local/share/applications"), "binote.desktop");
		const std::string text = "[Desktop Entry]\nType=Application\nName=Binote\nComment=Rewrite a text paragraph by paragraph\nExec=\"" + exe + "\" %f\nIcon=" + icon
			+ "\nTerminal=false\nCategories=Office;TextEditor;\nMimeType=text/markdown;text/x-markdown;\n";
		if (!write_text(desktop, text))
		{
			message = desktop;
			return false;
		}
		std::system("update-desktop-database \"$HOME/.local/share/applications\" >/dev/null 2>&1");
		std::system("xdg-mime default binote.desktop text/markdown text/x-markdown >/dev/null 2>&1");
		message = desktop;
		return true;
	}
#endif
}