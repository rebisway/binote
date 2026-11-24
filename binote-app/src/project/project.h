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

#pragma once
#ifndef _PROJECT_H
#define _PROJECT_H

#include <string>
#include <vector>

struct Heading
{
	int line = 1;					// 1-based
	int level = 1;
	std::string title;
};

// Chapters and titles of a text. Markdown: # ## ### ; text: "Chapter ...", "1.2 Title". Underlined titles (=== and ---) in both.
std::vector<Heading> parse_headings(const std::vector<std::string>& lines, bool markdown);

struct Doc
{
	std::string rel;				// name of the file, relative to the source folder
	std::string src_path;
	std::string dst_path;
	std::vector<Heading> headings;
	bool dirty = false;
	bool crlf = false;
	bool is_markdown() const;
};

class Project
{
public:
	bool is_folder = false;
	std::string name;
	std::string source_root;
	std::string meta_dir;							// where binote.json, binote.align and binote.notes are kept: empty = the project folder, else <project folder>/src
	std::string dest_root;
	std::string language = "fr";					// language of the text (spelling, typography), not the one of the interface
	std::string config_source;					// folder of the originals, relative to the project folder; empty: the originals are in the project folder and the work in the destination
												// (set, e.g. "src": the work is in the project folder, tracked by the version control, and the originals are copied there once)
	std::string config_destination = "dst";		// relative to the source folder, or absolute
	std::vector<std::string> config_order;
	std::vector<Doc> docs;

	static bool config_exists(const std::string& folder);
	// destination: empty = the one of the project file, or <folder>/dst
	static bool open_folder(const std::string& folder, const std::string& destination, Project& out);
	static bool open_file(const std::string& file, const std::string& destination_file, Project& out);

	void refresh();									// reads the folder again, applies the order
	void move(int index, int delta);				// reorder chapters and save the project file
	void set_destination(const std::string& folder);
	void save_config() const;						// binote.json in the source folder (folders only)
	std::string config_path() const;
	std::string meta_path(const char* name) const;	// a project file (binote.json, binote.align, binote.notes)
};

#endif
