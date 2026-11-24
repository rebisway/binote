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

// Binote - search and replace: plain text or regular expression, escapes (\t \n     \x..), case, whole words
// Offsets are bytes of the UTF-8 text. Matches are looked for in a whole text (the lines joined by \n).

#pragma once
#ifndef _SEARCH_H
#define _SEARCH_H

#include <regex>
#include <string>
#include <vector>

namespace search
{
	struct Options
	{
		bool regex = false;
		bool match_case = false;
		bool whole_word = false;				// plain text only
	};

	struct Match
	{
		int start = 0, end = 0;					// bytes
		std::string replacement;				// the text which replaces it (only when a replacement was given)
	};

	// \t \n \r \\ \0 \uXXXX \u{X...} \xHH become the character; the other backslashes stay
	std::string unescape(const std::string& text);

	class Engine
	{
	public:
		// False if the expression is wrong (error() tells why). An empty pattern matches nothing.
		bool set(const std::string& pattern, const Options& options);
		const std::string& error() const { return _error; }
		bool empty() const { return _pattern.empty(); }

		// The matches in order (at most limit). With a replacement, each match tells the text which replaces it
		// (in a regular expression $1, $& and $$ work).
		void find_all(const std::string& text, std::vector<Match>& out, size_t limit, const std::string* replacement = nullptr) const;
		// The text with every match replaced
		std::string replace_all(const std::string& text, const std::string& replacement, int& count) const;

	private:
		std::string _pattern, _error;
		Options _options;
		std::wstring _needle;					// plain text, in the case used to compare
		std::wregex _regex;
		bool _whole_text = false;				// the expression can match a line break: the text is not cut into lines
	};
}

#endif
