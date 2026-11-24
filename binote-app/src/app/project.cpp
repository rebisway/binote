// Binote - project (one file, or a folder with one chapter per file) and headings
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "project.h"
#include "util.h"
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

static std::string trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t\r");
	if (a == std::string::npos)
		return "";
	size_t b = s.find_last_not_of(" \t\r");
	return s.substr(a, b - a + 1);
}

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

static bool all_of_char(const std::string& s, char c)
{
	std::string t = trim(s);
	return !t.empty() && t.find_first_not_of(c) == std::string::npos;
}

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

bool Doc::is_markdown() const
{
	const std::string e = util::extension(src_path);
	return e == ".md" || e == ".markdown";
}

std::string Project::config_path() const
{
	return meta_path("binote.json");
}

std::string Project::meta_path(const char* name) const
{
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

bool Project::config_exists(const std::string& folder)
{
	return util::exists(util::combine(folder, "binote.json")) || !meta_folder(folder).empty();
}

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
	std::string text;
	if (util::read_text(out.config_path(), text))
		read_config(text, out);
	if (!destination.empty())
		out.config_destination = to_stored(folder, destination);
	out.refresh();
	return true;
}

bool Project::open_file(const std::string& file, const std::string& destination_file, Project& out)
{
	if (!util::exists(file))
		return false;
	out = Project();
	out.is_folder = false;
	out.source_root = util::parent_path(file);
	out.name = util::file_name(file);
	out.dest_root = util::combine(out.source_root, "dst");
	Doc d;
	d.rel = util::file_name(file);
	d.src_path = file;
	d.dst_path = destination_file.empty() ? util::combine(out.dest_root, d.rel) : destination_file;
	out.docs.push_back(d);
	return true;
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

void Project::refresh()
{
	if (!is_folder)
		return;
	fs::path dest = fs::u8path(config_destination);
	dest_root = (dest.is_absolute() ? dest : fs::u8path(source_root) / dest).lexically_normal().u8string();
	while (!dest_root.empty() && (dest_root.back() == '\\' || dest_root.back() == '/'))
		dest_root.pop_back();

	std::vector<std::string> files;
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(fs::u8path(source_root), ec))
	{
		if (!entry.is_regular_file())
			continue;
		const std::string e = util::extension(entry.path().u8string());
		if (e == ".md" || e == ".markdown" || e == ".txt")
			files.push_back(entry.path().filename().u8string());
	}
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
	o += order.empty() ? "]\n}\n" : "\n  ]\n}\n";
	util::write_text(config_path(), o);
}
