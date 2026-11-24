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

// Binote - spelling (Hunspell)

#include "analysis/spell.h"
#include "common/util.h"
#include "common/logger.h"
#include "hunspell.hxx"
#include <algorithm>
#include <filesystem>

Spell::Spell() {}
Spell::~Spell() {}

// The personal dictionary of the user (words added with "Add to dictionary")
static std::string personal_file()
{
	return util::combine(util::data_path(), "dictionary.txt");
}

// Folders which may hold dictionaries
static std::vector<std::string> dictionary_folders(bool bundled_only = false)
{
	const std::string app = util::application_path();
	std::vector<std::string> dirs = { util::combine(app, "dict"), util::combine(util::data_path(), "dict"), util::combine(app, "../../binote-app/dict"),
		util::combine(app, "../binote-app/dict"), util::combine(app, "../../dict"), "/usr/share/hunspell", "/usr/share/myspell" };
	if (bundled_only)
	{
		dirs.resize(5);								// the folder of the program, the one of the user, the ones of the sources
		return dirs;
	}
	// The extensions of LibreOffice: share/extensions/dict-*
	std::error_code ec;
	for (const char* root : { "C:/Program Files/LibreOffice/share/extensions", "/usr/lib/libreoffice/share/extensions", "/opt/libreoffice/share/extensions" })
		for (const auto& e : std::filesystem::directory_iterator(root, ec))
			if (e.is_directory(ec) && e.path().filename().string().rfind("dict-", 0) == 0)
				dirs.push_back(e.path().generic_string());
	return dirs;
}

// Codes of the dictionaries found (a .dic with its .aff) in the folders above, sorted
std::vector<std::string> Spell::languages(bool bundled_only)
{
	std::vector<std::string> codes;
	std::error_code ec;
	for (const std::string& dir : dictionary_folders(bundled_only))
		for (const auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(dir), ec))
		{
			if (e.path().extension() != ".dic")
				continue;
			const std::string code = e.path().stem().string();
			if (std::filesystem::exists(e.path().parent_path() / (code + ".aff"), ec) && std::find(codes.begin(), codes.end(), code) == codes.end())
				codes.push_back(code);
		}
	std::sort(codes.begin(), codes.end());
	return codes;
}

// Loads the dictionary of a language and the personal words; false when none is found
bool Spell::load(const std::string& code)
{
	_hunspell.reset();
	_cache.clear();
	_personal.clear();
	_ignored.clear();
	_code = code;
	// A code without a country ("en", "es") takes the dictionary of a country: the usual one, else the first found
	std::string file = code;
	if (code.find('_') == std::string::npos)
	{
		const std::vector<std::string> all = languages();
		if (std::find(all.begin(), all.end(), code) == all.end())
		{
			static const char* const usual[][2] = { { "en", "en_US" }, { "es", "es_ES" }, { "pt", "pt_BR" }, { "de", "de_DE" }, { "it", "it_IT" } };
			std::string chosen;
			for (const auto& u : usual)
				if (code == u[0] && std::find(all.begin(), all.end(), u[1]) != all.end())
					chosen = u[1];
			for (size_t i = 0; chosen.empty() && i < all.size(); i++)
				if (all[i].compare(0, code.size() + 1, code + "_") == 0)
					chosen = all[i];
			if (!chosen.empty())
				file = chosen;
		}
	}
	for (const std::string& dir : dictionary_folders())
	{
		const std::string dic = util::combine(dir, file + ".dic"), aff = util::combine(dir, file + ".aff");
		if (!util::exists(dic) || !util::exists(aff))
			continue;
		try
		{
			_hunspell.reset(new Hunspell(aff.c_str(), dic.c_str()));
			_folder = dir;
			break;
		}
		catch (...) { _hunspell.reset(); }
	}
	if (!_hunspell)
	{
		Logger::warning("No spelling dictionary found for " + code);
		return false;
	}
	std::string text;
	if (util::read_text(personal_file(), text))
		for (const std::string& w : util::split_lines(text))
			if (!w.empty())
			{
				_personal.insert(w);
				_hunspell->add(w);
			}
	Logger::log("Spelling dictionary: " + _folder);
	return true;
}

// Letters of any script, and the apostrophes and hyphens which can sit inside a word
static bool is_letter(unsigned int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || (c >= 0x370 && c <= 0x52F);
}

static bool is_digit(unsigned int c)	{ return c >= '0' && c <= '9'; }

// Code point at byte i of a UTF-8 text; n receives its length in bytes
static unsigned int decode(const char* s, int len, int i, int* n)
{
	const unsigned char b = (unsigned char)s[i];
	*n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
	unsigned int c = *n == 1 ? b : (b & (0xFF >> (*n + 1)));
	for (int k = 1; k < *n && i + k < len; k++)
		c = (c << 6) | ((unsigned char)s[i + k] & 0x3F);
	return c;
}

// Asks Hunspell; also accepts the forms with a typographic apostrophe, numbers and words of the personal dictionary
bool Spell::check_uncached(const std::string& word)
{
	if (_personal.count(word) || _ignored.count(word))
		return true;
	std::string ascii;
	for (size_t i = 0; i < word.size(); i++)
	{
		if ((unsigned char)word[i] == 0xE2 && i + 2 < word.size() && (unsigned char)word[i + 1] == 0x80 && (unsigned char)word[i + 2] == 0x99)
		{
			ascii += '\'';
			i += 2;
		}
		else
			ascii += word[i];
	}
	if (_hunspell->spell(ascii))
		return true;
	// "l'homme", "peut-être" missing as such: every part must be right
	for (char sep : { '\'', '-' })
	{
		if (ascii.find(sep) == std::string::npos)
			continue;
		bool all = true;
		size_t from = 0;
		int parts = 0;
		while (from <= ascii.size())
		{
			size_t to = ascii.find(sep, from);
			if (to == std::string::npos)
				to = ascii.size();
			const std::string part = ascii.substr(from, to - from);
			if (!part.empty())
			{
				parts++;
				if (part.size() > 1 && !_personal.count(part) && !_hunspell->spell(part))
					all = false;
			}
			from = to + 1;
		}
		if (all && parts > 1)
			return true;
	}
	return false;
}

// True when the word is correct; the answers are kept in a cache
bool Spell::check(const std::string& word)
{
	if (!_hunspell)
		return true;
	auto it = _cache.find(word);
	if (it != _cache.end())
		return it->second;
	const bool ok = check_uncached(word);
	if (_cache.size() > 300000)
		_cache.clear();
	_cache[word] = ok;
	return ok;
}

// Corrections proposed by Hunspell, best first
std::vector<std::string> Spell::suggest(const std::string& word)
{
	std::vector<std::string> out;
	if (!_hunspell)
		return out;
	std::string ascii;
	for (size_t i = 0; i < word.size(); i++)
	{
		if ((unsigned char)word[i] == 0xE2 && i + 2 < word.size() && (unsigned char)word[i + 1] == 0x80 && (unsigned char)word[i + 2] == 0x99)
		{
			ascii += '\'';
			i += 2;
		}
		else
			ascii += word[i];
	}
	out = _hunspell->suggest(ascii);
	if (out.size() > 8)
		out.resize(8);
	return out;
}

// Ignores a word until the application closes
void Spell::ignore(const std::string& word)
{
	_ignored.insert(word);
	_cache.clear();
}

// Adds a word to the personal dictionary
void Spell::add(const std::string& word)
{
	_personal.insert(word);
	if (_hunspell)
		_hunspell->add(word);
	_cache.clear();
	std::string text;
	util::read_text(personal_file(), text);
	if (!text.empty() && text.back() != '\n')
		text += '\n';
	util::write_text(personal_file(), text + word + "\n");
}

// Misspelled words of a line as (start, end) byte ranges; Markdown syntax, links and code spans are skipped
void Spell::find_errors(const char* s, int len, bool markdown, std::vector<std::pair<int, int>>& out)
{
	out.clear();
	if (!_hunspell || len == 0)
		return;

	// Regions which are not prose: URLs, e-mail addresses, code spans, link targets
	std::vector<std::pair<int, int>> skips;
	for (int i = 0; i < len; i++)
	{
		const bool url = (len - i >= 7 && (strncmp(s + i, "http://", 7) == 0)) || (len - i >= 8 && strncmp(s + i, "https://", 8) == 0) || (len - i >= 4 && strncmp(s + i, "www.", 4) == 0);
		if (url)
		{
			int j = i;
			while (j < len && s[j] != ' ' && s[j] != ')' && s[j] != '>' && s[j] != '\t')
				j++;
			skips.push_back({ i, j });
			i = j;
		}
		else if (markdown && s[i] == '`')
		{
			int j = i + 1;
			while (j < len && s[j] != '`')
				j++;
			skips.push_back({ i, std::min(j + 1, len) });
			i = j;
		}
		else if (markdown && s[i] == ']' && i + 1 < len && s[i + 1] == '(')
		{
			int j = i + 2;
			while (j < len && s[j] != ')')
				j++;
			skips.push_back({ i, std::min(j + 1, len) });
			i = j;
		}
		else if (s[i] == '@')
		{
			int a = i, b = i;
			while (a > 0 && s[a - 1] != ' ' && s[a - 1] != '\t') a--;
			while (b < len && s[b] != ' ' && s[b] != '\t') b++;
			skips.push_back({ a, b });
			i = b;
		}
	}
	auto skipped = [&](int pos) { for (const auto& k : skips) if (pos >= k.first && pos < k.second) return true; return false; };

	for (int i = 0; i < len;)
	{
		int n;
		unsigned int c = decode(s, len, i, &n);
		if (!is_letter(c))
		{
			i += n;
			continue;
		}
		// A word: letters, with ' ’ - inside
		const int start = i;
		int end = i;
		bool has_digit_next = false;
		while (i < len)
		{
			c = decode(s, len, i, &n);
			if (is_letter(c))
			{
				i += n;
				end = i;
			}
			else if ((c == '\'' || c == 0x2019 || c == '-') && i + n < len)
			{
				int n2;
				const unsigned int next = decode(s, len, i + n, &n2);
				if (is_letter(next))
					i += n;
				else
					break;
			}
			else
				break;
		}
		if (i < len)
		{
			int n3;
			const unsigned int after = decode(s, len, i, &n3);
			has_digit_next = is_digit(after);
		}
		if (start > 0 && is_digit((unsigned char)s[start - 1]))
			has_digit_next = true;
		if (has_digit_next || skipped(start))
			continue;
		const std::string word(s + start, end - start);
		// One letter, or all capitals (acronyms), are not checked
		bool upper = true;
		for (size_t k = 0; k < word.size(); k++)
			if ((unsigned char)word[k] >= 'a' && (unsigned char)word[k] <= 'z')
				upper = false;
		if (word.size() < 2 || (upper && word.size() <= 6))
			continue;
		if (!check(word))
			out.push_back({ start, end });
	}
}
