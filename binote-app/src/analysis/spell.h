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
