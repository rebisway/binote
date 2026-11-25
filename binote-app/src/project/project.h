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
	bool modified = false;				// saved, and different from the original (the reference copy in .src)
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
	std::vector<std::string> config_excluded;		// files of the folder which are not chapters of the project (binote.json: "Excluded")
	std::vector<std::string> excluded;				// the excluded files which are in the folder (set by refresh)
	std::vector<Doc> docs;

	// The way of working: the files of the project folder (or the opened file) are the work, edited in place so that Git or SVN follow them. A copy of each
	// original is kept in the hidden folder .src: it is the "Original" view. binote.json, the notes and the history of the changes are kept there too.
	bool in_place() const { return !config_source.empty(); }
	std::string data_folder() const { return meta_dir; }			// .src
	// The old way: the work in another folder (dst). Such a project (or file) is migrated before it is opened.
	static bool is_legacy_folder(const std::string& folder);
	static bool migrate_folder(const std::string& folder, std::string& report);
	static bool is_legacy_file(const std::string& file, const std::string& destination_file);
	static bool migrate_file(const std::string& file, const std::string& destination_file, std::string& report);
	void update_modified();											// sets Doc::modified of every chapter (the work compared with the original)
	void auto_exclude(const std::string& rel);						// a file written by Binote in the folder (export, table of contents) is not a chapter
	bool delete_data(std::string& error);							// removes .src: the originals, binote.json, notes, history. The work is not touched.
	bool export_data(const std::string& archive, std::string& error) const;
	bool import_data(const std::string& archive, std::string& error);

	static bool config_exists(const std::string& folder);
	// destination: empty = the one of the project file, or <folder>/dst
	static bool open_folder(const std::string& folder, const std::string& destination, Project& out);
	static bool open_file(const std::string& file, const std::string& destination_file, Project& out);

	void refresh();									// reads the folder again, applies the order
	bool exclude(int index);						// takes a chapter out of the project (the file is not touched) and saves the project file
	void include_again(const std::string& rel);	// puts an excluded file back, at the end, and saves
	void move_to(int from, int to);				// puts the chapter at the place of another one (drag and drop) and saves
	void move(int index, int delta);				// reorder chapters and save the project file
	void set_destination(const std::string& folder);
	void save_config() const;						// binote.json in the source folder (folders only)
	std::string config_path() const;
	std::string meta_path(const char* name) const;	// a project file (binote.json, binote.align, binote.notes)
};

#endif
