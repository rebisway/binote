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

// Binote - project (one file, or a folder with one chapter per file) and headings

#include "project/project.h"
#include "common/util.h"
#include "project/text.h"
#include "export/exporter.h"
#include <fstream>
#include <iterator>
#include <algorithm>
#include <filesystem>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

//-----------------------------------------------------------------------------
// Headings
//-----------------------------------------------------------------------------

// Removes the spaces and tabs at both ends
static std::string trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t\r");
	if (a == std::string::npos)
		return "";
	size_t b = s.find_last_not_of(" \t\r");
	return s.substr(a, b - a + 1);
}

// True when the text starts with the word, ignoring the case
static bool starts_with_nocase(const std::string& s, const char* word)
{
	size_t n = strlen(word);
	if (s.size() < n)
		return false;
	for (size_t i = 0; i < n; i++)
		if (std::tolower((unsigned char)s[i]) != word[i])
			return false;
	return s.size() == n || !(std::isalnum((unsigned char)s[n]) || (unsigned char)s[n] >= 0x80);
}

// True when the text is only made of that character (underlines of setext headings, separators)
static bool all_of_char(const std::string& s, char c)
{
	std::string t = trim(s);
	return !t.empty() && t.find_first_not_of(c) == std::string::npos;
}

// The outline of a text: "#" and underlined headings in Markdown; in plain text, underlined titles, lines starting with Chapitre / Chapter / Partie / Part and numbered titles
std::vector<Heading> parse_headings(const std::vector<std::string>& lines, bool markdown)
{
	std::vector<Heading> result;
	bool in_fence = false;
	for (size_t i = 0; i < lines.size(); i++)
	{
		const std::string& l = lines[i];
		const std::string t = trim(l);
		if (markdown && (t.rfind("```", 0) == 0 || t.rfind("~~~", 0) == 0))
		{
			in_fence = !in_fence;
			continue;
		}
		if (in_fence || t.empty())
			continue;

		// Underlined titles
		if (i + 1 < lines.size() && l.size() <= 120 && t[0] != '#' && t[0] != '-')
		{
			if (lines[i + 1].find_first_not_of(" ") != std::string::npos && all_of_char(lines[i + 1], '='))
			{
				result.push_back({ (int)i + 1, 1, t });
				i++;
				continue;
			}
			if (all_of_char(lines[i + 1], '-') && trim(lines[i + 1]).size() >= 2)
			{
				result.push_back({ (int)i + 1, 2, t });
				i++;
				continue;
			}
		}

		if (markdown)
		{
			size_t p = 0;
			while (p < l.size() && l[p] == ' ' && p < 3)
				p++;
			size_t h = p;
			while (h < l.size() && l[h] == '#')
				h++;
			int level = (int)(h - p);
			if (level >= 1 && level <= 6 && h < l.size() && (l[h] == ' ' || l[h] == '\t'))
			{
				std::string title = trim(l.substr(h));
				while (!title.empty() && title.back() == '#')
					title.pop_back();
				result.push_back({ (int)i + 1, level, trim(title) });
			}
		}
		else
		{
			if (l.size() > 120)
				continue;
			if (starts_with_nocase(t, "chapitre") || starts_with_nocase(t, "chapter") || starts_with_nocase(t, "partie") || starts_with_nocase(t, "part")
				|| starts_with_nocase(t, "livre") || starts_with_nocase(t, "book"))
				result.push_back({ (int)i + 1, 1, t });
			else if (starts_with_nocase(t, "section") || starts_with_nocase(t, "sous-section"))
				result.push_back({ (int)i + 1, 2, t });
			else
			{
				// 1. Title  1.2 Title  1.2.3) Title
				size_t p = 0;
				int dots = 0;
				while (p < t.size() && (std::isdigit((unsigned char)t[p]) || (t[p] == '.' && p > 0 && std::isdigit((unsigned char)t[p - 1]) && p + 1 < t.size() && std::isdigit((unsigned char)t[p + 1]))))
				{
					if (t[p] == '.')
						dots++;
					p++;
				}
				if (p > 0 && p < t.size())
				{
					if (t[p] == '.' || t[p] == ')')
						p++;
					if (p < t.size() && t[p] == ' ' && t.back() != '.')
						result.push_back({ (int)i + 1, dots + 1, t });
				}
			}
		}
	}
	return result;
}

//-----------------------------------------------------------------------------
// Project file (binote.json): Name, Destination, Order
//-----------------------------------------------------------------------------

namespace
{
	// Minimal reader of the project file: an object whose values are strings or arrays of strings
	struct JsonReader
	{
		const std::string& s;
		size_t p = 0;
		explicit JsonReader(const std::string& text) : s(text) {}
		void skip() { while (p < s.size() && std::isspace((unsigned char)s[p])) p++; }
		bool string(std::string& out)
		{
			skip();
			if (p >= s.size() || s[p] != '"')
				return false;
			p++;
			out.clear();
			while (p < s.size() && s[p] != '"')
			{
				if (s[p] == '\\' && p + 1 < s.size())
				{
					p++;
					switch (s[p])
					{
					case 'n': out += '\n'; break;
					case 't': out += '\t'; break;
					case 'u':
					{
						unsigned int c = (unsigned int)std::strtoul(s.substr(p + 1, 4).c_str(), nullptr, 16);
						p += 4;
						if (c < 0x80) out += (char)c;
						else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
						else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
						break;
					}
					default: out += s[p];
					}
				}
				else
					out += s[p];
				p++;
			}
			p++;
			return true;
		}
	};

	std::string json_escape(const std::string& v)
	{
		std::string o;
		for (char c : v)
		{
			if (c == '"' || c == '\\') { o += '\\'; o += c; }
			else if (c == '\n') o += "\\n";
			else o += c;
		}
		return o;
	}

	void read_config(const std::string& text, Project& p)
	{
		JsonReader r(text);
		r.skip();
		if (r.p >= text.size() || text[r.p] != '{')
			return;
		r.p++;
		for (;;)
		{
			std::string key;
			r.skip();
			if (r.p < text.size() && text[r.p] == ',')
				r.p++;
			if (!r.string(key))
				break;
			r.skip();
			if (r.p >= text.size() || text[r.p] != ':')
				break;
			r.p++;
			r.skip();
			if (r.p < text.size() && text[r.p] == '[')
			{
				r.p++;
				std::vector<std::string> items;
				for (;;)
				{
					r.skip();
					if (r.p < text.size() && text[r.p] == ',')
						r.p++;
					std::string item;
					if (!r.string(item))
						break;
					items.push_back(item);
				}
				r.skip();
				if (r.p < text.size() && text[r.p] == ']')
					r.p++;
				if (key == "Order")
					p.config_order = items;
				else if (key == "Excluded")
					p.config_excluded = items;
			}
			else
			{
				std::string value;
				if (!r.string(value))
					break;
				if (key == "Name") p.name = value;
				else if (key == "Destination") p.config_destination = value;
				else if (key == "Source") p.config_source = value;
				else if (key == "Language") p.language = value;
			}
		}
	}
}

// Markdown or plain text, from the extension of the source
bool Doc::is_markdown() const
{
	const std::string e = util::extension(src_path);
	return e == ".md" || e == ".markdown";
}

// binote.json of the project
std::string Project::config_path() const
{
	return meta_path("binote.json");
}

// A project file (binote.json, notes, pins) next to the originals, or in their .src / src folder when there is one
std::string Project::meta_path(const char* name) const
{
	// A single file shares its .src folder with the others of its folder: its project files carry its name
	if (!is_folder && !meta_dir.empty() && !docs.empty())
		return util::combine(meta_dir, docs[0].rel + "." + name);
	return util::combine(meta_dir.empty() ? source_root : meta_dir, name);
}

// The folder of the originals which also holds the project files: hidden (.src) or not (src)
static std::string meta_folder(const std::string& folder)
{
	if (util::exists(util::combine(folder, "binote.json")))
		return std::string();
	for (const char* name : { ".src", "src" })
		if (util::exists(util::combine(util::combine(folder, name), "binote.json")))
			return util::combine(folder, name);
	return std::string();
}

// True when the folder is already a Binote project
bool Project::config_exists(const std::string& folder)
{
	return util::exists(util::combine(folder, "binote.json")) || !meta_folder(folder).empty();
}

// Path as stored in binote.json: relative to the folder of the originals when it is inside
static std::string to_stored(const std::string& source_root, const std::string& path)
{
	std::error_code ec;
	fs::path full = fs::weakly_canonical(fs::u8path(path), ec);
	fs::path root = fs::weakly_canonical(fs::u8path(source_root), ec);
	fs::path rel = full.lexically_relative(root);
	if (!rel.empty() && rel.generic_u8string().rfind("..", 0) != 0)
		return rel.generic_u8string();
	return full.u8string();
}

// A folder whose name starts with a dot is hidden for Windows too
static void hide_if_dotted(const std::string& file_in_folder)
{
#ifdef _WIN32
	const fs::path dir = fs::u8path(file_in_folder).parent_path();
	if (dir.filename().u8string().compare(0, 1, ".") == 0)
		SetFileAttributesW(dir.c_str(), GetFileAttributesW(dir.c_str()) | FILE_ATTRIBUTE_HIDDEN);
#else
	(void)file_in_folder;
#endif
}

// Opens a folder of chapters; reads binote.json for the destination and the order
bool Project::open_folder(const std::string& folder, const std::string& destination, Project& out)
{
	if (!util::is_directory(folder))
		return false;
	out = Project();
	out.is_folder = true;
	out.source_root = folder;
	out.name = fs::u8path(folder).filename().u8string();
	if (out.name.empty())
		out.name = fs::u8path(folder).parent_path().filename().u8string();
	out.meta_dir = meta_folder(folder);
	bool fresh = false;
	if (out.meta_dir.empty() && !util::exists(util::combine(folder, "binote.json")) && destination.empty())
	{
		// A new project: the files of the folder are the work, a copy of each one in .src is the original
		out.meta_dir = util::combine(folder, ".src");
		out.config_source = ".src";
		out.config_destination = ".";
		std::error_code ec;
		fs::create_directories(fs::u8path(out.meta_dir), ec);
		hide_if_dotted(util::combine(out.meta_dir, "x"));
		fresh = true;
	}
	std::string text;
	if (util::read_text(out.config_path(), text))
		read_config(text, out);
	if (!destination.empty())
		out.config_destination = to_stored(folder, destination);
	out.refresh();
	if (fresh)
		out.save_config();
	return true;
}

// Opens a single file: it is the work, edited in place; a copy of it in .src (next to it) is the original
bool Project::open_file(const std::string& file, const std::string& destination_file, Project& out)
{
	(void)destination_file;						// the old destination of the work: see migrate_file
	if (!util::exists(file))
		return false;
	out = Project();
	out.is_folder = false;
	out.source_root = util::parent_path(file);
	out.name = util::file_name(file);
	out.meta_dir = util::combine(out.source_root, ".src");
	out.config_source = ".src";
	out.config_destination = ".";
	out.dest_root = out.source_root;
	Doc d;
	d.rel = util::file_name(file);
	d.src_path = util::combine(out.meta_dir, d.rel);
	d.dst_path = file;
	if (!util::exists(d.src_path))
	{
		std::error_code ec;
		fs::create_directories(fs::u8path(out.meta_dir), ec);
		hide_if_dotted(util::combine(out.meta_dir, "x"));
		fs::copy_file(fs::u8path(file), fs::u8path(d.src_path), ec);
	}
	out.docs.push_back(d);
	return true;
}

// Lists the chapters of a folder (md, markdown, txt) in the order of binote.json, then the new files by name
void Project::refresh()
{
	if (!is_folder)
		return;
	fs::path dest = fs::u8path(config_destination);
	dest_root = (dest.is_absolute() ? dest : fs::u8path(source_root) / dest).lexically_normal().u8string();
	while (!dest_root.empty() && (dest_root.back() == '\\' || dest_root.back() == '/'))
		dest_root.pop_back();

	std::vector<std::string> files;
	excluded.clear();
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(fs::u8path(source_root), ec))
	{
		if (!entry.is_regular_file())
			continue;
		const std::string e = util::extension(entry.path().u8string());
		if (e != ".md" && e != ".markdown" && e != ".txt")
			continue;
		const std::string name = entry.path().filename().u8string();
		if (std::find(config_excluded.begin(), config_excluded.end(), name) != config_excluded.end())
			excluded.push_back(name);
		else
			files.push_back(name);
	}
	std::sort(excluded.begin(), excluded.end(), util::natural_less);
	std::vector<std::string> ordered;
	for (const std::string& n : config_order)
		if (std::find(files.begin(), files.end(), n) != files.end() && std::find(ordered.begin(), ordered.end(), n) == ordered.end())
			ordered.push_back(n);
	std::vector<std::string> rest;
	for (const std::string& f : files)
		if (std::find(ordered.begin(), ordered.end(), f) == ordered.end())
			rest.push_back(f);
	std::sort(rest.begin(), rest.end(), util::natural_less);
	ordered.insert(ordered.end(), rest.begin(), rest.end());

	docs.clear();
	for (const std::string& f : ordered)
	{
		Doc d;
		d.rel = f;
		d.src_path = util::combine(config_source.empty() ? source_root : util::combine(source_root, config_source), f);
		d.dst_path = util::combine(dest_root, f);
		if (!config_source.empty() && !util::exists(d.src_path))
		{
			// First time this chapter is seen: its text is the original
			if (fs::create_directories(fs::u8path(d.src_path).parent_path(), ec))
				hide_if_dotted(d.src_path);
			fs::copy_file(fs::u8path(d.dst_path), fs::u8path(d.src_path), ec);
		}
		docs.push_back(d);
	}
}

// Takes a chapter out of the project: the file stays where it is, it is only not listed, exported or searched any more
bool Project::exclude(int index)
{
	if (!is_folder || index < 0 || index >= (int)docs.size() || docs.size() < 2)
		return false;
	const std::string rel = docs[index].rel;
	docs.erase(docs.begin() + index);
	if (std::find(config_excluded.begin(), config_excluded.end(), rel) == config_excluded.end())
		config_excluded.push_back(rel);
	excluded.push_back(rel);
	std::sort(excluded.begin(), excluded.end(), util::natural_less);
	config_order.clear();
	for (const Doc& d : docs)
		config_order.push_back(d.rel);
	save_config();
	return true;
}

// Puts an excluded file back in the project (at the end of the chapters)
void Project::include_again(const std::string& rel)
{
	config_excluded.erase(std::remove(config_excluded.begin(), config_excluded.end(), rel), config_excluded.end());
	config_order.clear();
	for (const Doc& d : docs)
		config_order.push_back(d.rel);
	config_order.push_back(rel);
	save_config();
	refresh();
}

// Puts a chapter at the place of another one: the others shift
void Project::move_to(int from, int to)
{
	if (!is_folder || from < 0 || to < 0 || from >= (int)docs.size() || to >= (int)docs.size() || from == to)
		return;
	Doc d = docs[from];
	docs.erase(docs.begin() + from);
	docs.insert(docs.begin() + to, d);
	config_order.clear();
	for (const Doc& x : docs)
		config_order.push_back(x.rel);
	save_config();
}

// Moves a chapter up or down in the order and saves it
void Project::move(int index, int delta)
{
	const int j = index + delta;
	if (!is_folder || index < 0 || j < 0 || index >= (int)docs.size() || j >= (int)docs.size())
		return;
	std::swap(docs[index], docs[j]);
	config_order.clear();
	for (const Doc& d : docs)
		config_order.push_back(d.rel);
	save_config();
}

// Changes the destination folder (for a single file, the destination file)
void Project::set_destination(const std::string& folder)
{
	if (!is_folder)
	{
		dest_root = folder;
		for (Doc& d : docs)
			d.dst_path = util::combine(folder, d.rel);
		return;
	}
	config_destination = to_stored(source_root, folder);
	config_order.clear();
	for (const Doc& d : docs)
		config_order.push_back(d.rel);
	save_config();
	refresh();
}

// Writes binote.json (Name, Destination, Order)
void Project::save_config() const
{
	if (!is_folder)
		return;
	std::vector<std::string> order = config_order;
	if (order.empty())
		for (const Doc& d : docs)
			order.push_back(d.rel);
	std::string o = "{\n  \"Name\": \"" + json_escape(name) + "\",\n  \"Destination\": \"" + json_escape(config_destination) + "\",\n" + (config_source.empty() ? std::string() : "  \"Source\": \"" + json_escape(config_source) + "\",\n") + "  \"Language\": \""+ json_escape(language) + "\",\n  \"Order\": [";
	for (size_t i = 0; i < order.size(); i++)
		o += std::string(i ? "," : "") + "\n    \"" + json_escape(order[i]) + "\"";
	o += order.empty() ? "]" : "\n  ]";
	if (!config_excluded.empty())
	{
		o += ",\n  \"Excluded\": [";
		for (size_t i = 0; i < config_excluded.size(); i++)
			o += std::string(i ? "," : "") + "\n    \"" + json_escape(config_excluded[i]) + "\"";
		o += "\n  ]";
	}
	o += "\n}\n";
	util::write_text(config_path(), o);
}

//-----------------------------------------------------------------------------
// Working in place: migration of the old projects, project data
//-----------------------------------------------------------------------------

namespace
{
	std::string read_binary(const std::string& path)
	{
		std::ifstream f(fs::u8path(path), std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}

	bool write_binary(const std::string& path, const std::string& data)
	{
		std::error_code ec;
		fs::create_directories(fs::u8path(path).parent_path(), ec);
		std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
		if (!f)
			return false;
		f.write(data.data(), (std::streamsize)data.size());
		return (bool)f;
	}

	// The same text whatever the line breaks
	std::string without_cr(std::string s)
	{
		s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
		return s;
	}

	bool is_chapter_file(const std::string& name)
	{
		const std::string e = util::extension(name);
		return e == ".md" || e == ".markdown" || e == ".txt";
	}

	// A path which does not exist yet, next to wanted: wanted, wanted-2, wanted-3...
	std::string free_name(const std::string& wanted)
	{
		std::error_code ec;
		if (!fs::exists(fs::u8path(wanted), ec))
			return wanted;
		for (int i = 2; i < 1000; i++)
			if (!fs::exists(fs::u8path(wanted + "-" + std::to_string(i)), ec))
				return wanted + "-" + std::to_string(i);
		return wanted;
	}
}

// A folder whose work is in another folder (the old way of working): its binote.json names a destination, or it has a dst folder with the same chapters
bool Project::is_legacy_folder(const std::string& folder)
{
	if (!meta_folder(folder).empty())
		return false;
	std::string text;
	if (util::read_text(util::combine(folder, "binote.json"), text))
	{
		Project p;
		read_config(text, p);
		return p.config_source.empty();
	}
	std::error_code ec;
	const fs::path dst = fs::u8path(util::combine(folder, "dst"));
	if (!fs::is_directory(dst, ec))
		return false;
	for (const auto& e : fs::directory_iterator(dst, ec))
		if (e.is_regular_file() && is_chapter_file(e.path().filename().u8string()) && util::exists(util::combine(folder, e.path().filename().u8string())))
			return true;
	return false;
}

// Moves a project made the old way to the new one. Nothing is deleted: the originals are copied to .src, the work (dst) is copied over the files of the folder,
// the old dst folder and binote.json are kept in .src.
bool Project::migrate_folder(const std::string& folder, std::string& report)
{
	std::error_code ec;
	Project old;
	if (!open_folder(folder, util::combine(folder, "dst"), old))
	{
		report = "Cannot open " + folder;
		return false;
	}
	// open_folder with a destination reads binote.json if there is one; the destination named there wins over the default one
	{
		std::string text;
		Project p;
		if (util::read_text(util::combine(folder, "binote.json"), text))
		{
			read_config(text, p);
			old.config_destination = p.config_destination.empty() ? "dst" : p.config_destination;
		}
		else
			old.config_destination = "dst";
		old.refresh();
	}
	const std::string meta = util::combine(folder, ".src");
	fs::create_directories(fs::u8path(meta), ec);
	hide_if_dotted(util::combine(meta, "x"));
	History::set_root(util::combine(meta, "history"), folder);
	int copied = 0, updated = 0, history = 0;
	for (const Doc& d : old.docs)
	{
		const std::string work = util::combine(folder, d.rel), ref = util::combine(meta, d.rel);
		if (!util::exists(ref))
		{
			fs::copy_file(fs::u8path(work), fs::u8path(ref), ec);
			copied++;
		}
		if (util::exists(d.dst_path) && fs::u8path(d.dst_path) != fs::u8path(work) && without_cr(read_binary(d.dst_path)) != without_cr(read_binary(work)))
		{
			fs::copy_file(fs::u8path(d.dst_path), fs::u8path(work), fs::copy_options::overwrite_existing, ec);
			updated++;
		}
		if (History::import_legacy(d.dst_path, work))
			history++;
	}
	// The project file, then the notes, pins and alignment which were next to the originals
	Project p = old;
	p.config_source = ".src";
	p.config_destination = ".";
	p.meta_dir = meta;
	p.refresh();
	p.save_config();
	for (const char* name : { "binote.align", "binote.notes" })
		if (util::exists(util::combine(folder, name)))
			fs::rename(fs::u8path(util::combine(folder, name)), fs::u8path(util::combine(meta, name)), ec);
	if (util::exists(util::combine(folder, "binote.json")))
		fs::rename(fs::u8path(util::combine(folder, "binote.json")), fs::u8path(free_name(util::combine(meta, "binote.json.before-migration"))), ec);
	// The old destination folder, when it is inside the project, is kept in .src
	const fs::path dest = fs::u8path(old.dest_root);
	bool kept = false;
	if (!old.dest_root.empty() && fs::is_directory(dest, ec) && dest.parent_path() == fs::u8path(folder).lexically_normal().parent_path() / fs::u8path(folder).filename())
	{
		fs::rename(dest, fs::u8path(free_name(util::combine(meta, "dst-before-migration"))), ec);
		kept = !ec;
	}
	report = std::to_string((int)old.docs.size()) + " chapter(s), " + std::to_string(updated) + " file(s) replaced by the work, " + std::to_string(history) + " history folder(s)" + (kept ? ", the old dst folder is in .src" : "");
	return true;
}

// A single file whose work is in another file (dst/<name> or a destination chosen before)
bool Project::is_legacy_file(const std::string& file, const std::string& destination_file)
{
	const std::string dir = util::parent_path(file), name = util::file_name(file);
	if (util::exists(util::combine(util::combine(dir, ".src"), name)))
		return false;
	const std::string dest = destination_file.empty() ? util::combine(util::combine(dir, "dst"), name) : destination_file;
	std::error_code ec;
	return util::exists(dest) && fs::u8path(dest).lexically_normal() != fs::u8path(file).lexically_normal();
}

bool Project::migrate_file(const std::string& file, const std::string& destination_file, std::string& report)
{
	std::error_code ec;
	const std::string dir = util::parent_path(file), name = util::file_name(file), meta = util::combine(dir, ".src");
	const std::string dest = destination_file.empty() ? util::combine(util::combine(dir, "dst"), name) : destination_file;
	fs::create_directories(fs::u8path(meta), ec);
	hide_if_dotted(util::combine(meta, "x"));
	const std::string ref = util::combine(meta, name);
	if (!util::exists(ref))
		fs::copy_file(fs::u8path(file), fs::u8path(ref), ec);
	History::set_root(util::combine(meta, "history"), dir);
	bool replaced = false;
	if (util::exists(dest) && without_cr(read_binary(dest)) != without_cr(read_binary(file)))
	{
		fs::copy_file(fs::u8path(dest), fs::u8path(file), fs::copy_options::overwrite_existing, ec);
		replaced = true;
	}
	History::import_legacy(dest, file);
	if (util::exists(dest))
		fs::rename(fs::u8path(dest), fs::u8path(free_name(util::combine(meta, name + ".before-migration"))), ec);
	// The dst folder of this file, when nothing else is in it, is not needed any more
	if (fs::u8path(dest).parent_path().filename() == "dst")
		fs::remove(fs::u8path(dest).parent_path(), ec);
	report = replaced ? "the file was replaced by its work" : "the work was the same as the file";
	return true;
}

// Compares the work with the original (the reference copy) for each chapter
void Project::update_modified()
{
	for (Doc& d : docs)
		d.modified = util::exists(d.src_path) && util::exists(d.dst_path) && without_cr(read_binary(d.src_path)) != without_cr(read_binary(d.dst_path));
}

// A file written in the folder by Binote (an export, the table of contents) is not a chapter
void Project::auto_exclude(const std::string& rel)
{
	if (!is_folder || !is_chapter_file(rel))
		return;
	if (std::find(config_excluded.begin(), config_excluded.end(), rel) == config_excluded.end())
		config_excluded.push_back(rel);
	docs.erase(std::remove_if(docs.begin(), docs.end(), [&](const Doc& d) { return d.rel == rel; }), docs.end());
	config_order.clear();
	for (const Doc& d : docs)
		config_order.push_back(d.rel);
	if (std::find(excluded.begin(), excluded.end(), rel) == excluded.end())
		excluded.push_back(rel);
	std::sort(excluded.begin(), excluded.end(), util::natural_less);
	save_config();
}

// Removes the data of the project: .src (the originals, binote.json, notes, pins, history). The work is not touched.
bool Project::delete_data(std::string& error)
{
	std::error_code ec;
	if (meta_dir.empty())
	{
		error = "No project data";
		return false;
	}
	if (is_folder)
	{
		const std::string leaf = fs::u8path(meta_dir).filename().u8string();
		if (leaf != ".src" && leaf != "src")
		{
			error = "Unexpected folder: " + meta_dir;
			return false;
		}
		fs::remove_all(fs::u8path(meta_dir), ec);
		if (ec)
		{
			error = ec.message();
			return false;
		}
		return true;
	}
	// A single file: its reference, its project files and its history; the other files of the folder keep theirs
	const std::string name = docs.empty() ? std::string() : docs[0].rel;
	if (name.empty())
		return false;
	for (const auto& e : fs::directory_iterator(fs::u8path(meta_dir), ec))
	{
		const std::string n = e.path().filename().u8string();
		if (e.is_regular_file() && (n == name || n.compare(0, name.size() + 1, name + ".") == 0))
			fs::remove(e.path(), ec);
	}
	fs::remove_all(fs::u8path(History::folder_of(docs[0].dst_path)), ec);
	fs::remove(fs::u8path(meta_dir), ec);			// only if it is empty now
	return true;
}

// The data of the project in one archive (ZIP): everything of .src, or for a single file its own files
bool Project::export_data(const std::string& archive, std::string& error) const
{
	std::error_code ec;
	if (meta_dir.empty() || !fs::is_directory(fs::u8path(meta_dir), ec))
	{
		error = "No project data";
		return false;
	}
	const std::string name = docs.empty() ? std::string() : docs[0].rel;
	const fs::path root = fs::u8path(meta_dir);
	std::vector<exporter::ZipEntry> entries;
	std::string history_prefix;
	if (!is_folder)
	{
		std::string h = History::folder_of(docs[0].dst_path);
		std::error_code e2;
		history_prefix = fs::relative(fs::u8path(h), root, e2).generic_u8string() + "/";
	}
	for (const auto& e : fs::recursive_directory_iterator(root, ec))
	{
		if (!e.is_regular_file())
			continue;
		const std::string rel = fs::relative(e.path(), root, ec).generic_u8string();
		if (!is_folder && !(rel == name || rel.compare(0, name.size() + 1, name + ".") == 0 || rel.compare(0, history_prefix.size(), history_prefix) == 0))
			continue;
		exporter::ZipEntry z;
		z.name = rel;
		z.data = read_binary(e.path().u8string());
		entries.push_back(z);
	}
	if (entries.empty())
	{
		error = "No project data";
		return false;
	}
	return exporter::zip_write(archive, entries, error);
}

// Puts the data of an archive made by export_data into .src (the files of the same name are replaced)
bool Project::import_data(const std::string& archive, std::string& error)
{
	std::vector<exporter::ZipEntry> entries;
	if (!exporter::zip_read(archive, entries, error))
		return false;
	if (meta_dir.empty())
	{
		error = "No project data folder";
		return false;
	}
	for (const exporter::ZipEntry& e : entries)
		if (e.name.empty() || e.name[0] == '/' || e.name.find("..") != std::string::npos || e.name.find(':') != std::string::npos || e.name.find('\\') != std::string::npos)
		{
			error = "Unsafe name in the archive: " + e.name;
			return false;
		}
	int n = 0;
	for (const exporter::ZipEntry& e : entries)
		if (write_binary(util::combine(meta_dir, e.name), e.data))
			n++;
	if (n == 0)
	{
		error = "Nothing was imported";
		return false;
	}
	return true;
}
