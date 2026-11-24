// Binote - spelling (Hunspell)
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#ifndef _SPELL_H
#define _SPELL_H

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class Hunspell;

class Spell
{
public:
	Spell();
	~Spell();

	// Looks for <code>.dic and <code>.aff: beside the executable (dict), in the user folder, in the sources, then in LibreOffice
	bool load(const std::string& code = "fr");
	bool available() const { return _hunspell != nullptr; }
	const std::string& code() const { return _code; }
	// Codes of the dictionaries found (fr, en_US...)
	static std::vector<std::string> languages();
	const std::string& folder() const { return _folder; }

	bool check(const std::string& word);									// word as typed (UTF-8)
	std::vector<std::string> suggest(const std::string& word);
	void ignore(const std::string& word);									// until the end of the session
	void add(const std::string& word);										// personal dictionary

	// Misspelled words of a line: [start, end) in bytes. Code spans, links and URLs are skipped in Markdown.
	void find_errors(const char* line, int length, bool markdown, std::vector<std::pair<int, int>>& out);

private:
	std::unique_ptr<Hunspell> _hunspell;
	std::string _folder;
	std::string _code;
	std::unordered_map<std::string, bool> _cache;
	std::unordered_set<std::string> _personal, _ignored;
	bool check_uncached(const std::string& word);
};

#endif
