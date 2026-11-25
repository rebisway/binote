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

// Binote - French typography, diff, table of contents, history

#pragma once
#ifndef _TEXT_H
#define _TEXT_H

#include "project/project.h"
#include <string>
#include <vector>

//-----------------------------------------------------------------------------
// Automatic typography, driven by a profile per language (typography-engine/profiles/*.typo): quotation marks, spaces before the signs,
// apostrophe, ellipsis, dashes. Binote looks for the profiles in %AppData%/Binote/typography, then beside the program.
//-----------------------------------------------------------------------------
namespace typography
{
	const char* const NBSP = "\xC2\xA0";
	const char* const NNBSP = "\xE2\x80\xAF";

	// Quotation marks written with the key ": 0 guillemets « » with narrow no-break spaces, 1 the straight quotes stay, 2 curly quotes “ ”
	void set_quote_style(int style);
	// Loads the profile of a language ("fr", "fr-FR"...): the code gives fr-fr.typo, then fr.typo, then generic.typo
	void set_language(const std::string& code);
	std::string profile_name();					// name and file of the profile in use (empty: none)
	std::string profile_path();
	std::string user_folder();					// where the profiles of the user are (%AppData%/Binote/typography)
	bool profile_is_user();						// the profile in use comes from the folder of the user
	std::vector<std::string> profile_codes();	// codes of the profiles found (fr, de-ch...), without the generic one
	void reload();								// read the profiles again
	bool create_user_copy();					// copies the profile in use to the folder of the user, to edit it
	bool has_profile();							// false: only the generic profile (curly quotes), the language has no profile of its own
	std::string fix_text(const std::string& text, bool markdown);			// code blocks and code spans are left alone
	std::string fix_line(const std::string& line, bool markdown);
	// The opposite: plain quotes, apostrophes and dots, no typographic space before the signs
	std::string simplify_text(const std::string& text, bool markdown);
	std::string simplify_line(const std::string& line, bool markdown);

	// What to do after a character was typed: replace [start, start + length) of the line by text. before = the line up to the caret (typed included)
	struct Typed
	{
		bool valid = false;
		int start = 0;
		int length = 0;
		std::string text;
		int caret_extra = 0;						// bytes to skip after the replacement (a typed space which stays)
	};
	Typed on_typed(const std::string& typed, const std::string& before);
}

//-----------------------------------------------------------------------------
// Differences between source and destination, paragraph by paragraph then word by word
//-----------------------------------------------------------------------------
namespace diff
{
	enum Kind { Changed, Added, Removed, WordAdded, WordRemoved };

	struct Block { int a0, a1, b0, b1; };				// changed ranges [a0, a1) of the source and [b0, b1) of the destination (paragraphs, 0-based)
	struct Mark { int para; int kind; };					// 0-based index of a paragraph
	struct Word { int para; int start; int length; int kind; };		// byte offsets in the paragraph

	struct Result
	{
		std::vector<Mark> src_marks, dst_marks;
		std::vector<Word> src_words, dst_words;
		int changed = 0, added = 0, removed = 0;
		std::vector<Block> blocks;						// what differs, in order: outside of them the paragraphs are the same
	};

	// Paragraph of the other text which faces a paragraph (0-based), to scroll both texts together even when paragraphs were added or removed
	int map_dst_to_src(const Result& r, int para);
	int map_src_to_dst(const Result& r, int para);

	// Paragraphs are the non-empty lines (end trimmed)
	// pins: (source, destination) paragraphs (0-based, increasing) which the user declared to face each other
	Result compute(const std::vector<std::string>& src, const std::vector<std::string>& dst, const std::vector<std::pair<int, int>>& pins = std::vector<std::pair<int, int>>());

	// Whole text, line by line (for the history): + / - lines
	struct Line { char sign; std::string text; };			// ' ' same, '+' added, '-' removed
	std::vector<Line> lines(const std::string& old_text, const std::string& new_text, bool only_changes);
}

//-----------------------------------------------------------------------------
// Table of contents
//-----------------------------------------------------------------------------
namespace toc
{
	struct Chapter { std::string rel; std::vector<Heading> headings; };
	std::string generate(const std::vector<Chapter>& chapters, bool markdown, bool multi);
}

//-----------------------------------------------------------------------------
// History of the versions of a file: <root>/<hash of the path>/<date>_<kind>.txt. The root is the folder .src/history of the project (set_root);
// without a root, <data>/history (the old place).
//-----------------------------------------------------------------------------
class History
{
public:
	struct Snapshot
	{
		std::string path;
		std::string label;							// "22/11/2026 16:55:03 - save"
		long long size = 0;
	};

	// The history of the project is kept in its own folder (in .src), the documents are named by their path relative to base
	static void set_root(const std::string& root, const std::string& base);
	// Takes the history that the old place (<data>/history) holds for a key (the path of the file as it was then) into the one of newkey. False if there is none.
	static bool import_legacy(const std::string& oldkey, const std::string& newkey);
	static std::string folder_of(const std::string& key);									// the folder of the versions of a document
	static bool add(const std::string& key, const std::string& text, const char* kind);		// false if identical to the latest		// false if identical to the latest
	static std::vector<Snapshot> list(const std::string& key);								// newest first
	static std::string read(const Snapshot& s);
	static void remove(const Snapshot& s);
};

#endif
