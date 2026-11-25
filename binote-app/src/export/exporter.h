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

// Binote - export of a project: one Markdown file, HTML, Word (.docx), EPUB or PDF
// The text is read into blocks (headings, paragraphs, list items) and written by each format; no library is needed.

#pragma once
#ifndef _EXPORTER_H
#define _EXPORTER_H

#include <string>
#include <vector>

namespace exporter
{
	enum Format { Markdown, Html, Docx, Epub, Pdf, Text };

	struct Run
	{
		std::string text;
		bool bold = false, italic = false, code = false;
	};

	struct Block
	{
		enum Type { Heading, Paragraph, Bullet, Numbered, Rule, Quote } type = Paragraph;
		int level = 1;								// heading
		int number = 0;								// numbered item
		std::vector<Run> runs;
	};

	struct Chapter
	{
		std::string title;
		std::string text;							// the text of the file (Markdown or plain)
	};

	struct Options
	{
		Format format = Pdf;
		std::string title, author, language = "fr";
		bool markdown = true;
		bool page_break = true;						// a new page for each chapter
		bool title_page = true;
	};

	// The blocks of a text
	std::vector<Block> parse(const std::string& text, bool markdown);

	// Write the project. False and the reason in error.
	bool write(const std::string& path, const std::vector<Chapter>& chapters, const Options& options, std::string& error);

	std::string extension(Format format);			// ".pdf"...

	// A ZIP archive (files stored without compression): the .docx and .epub files, and the export of the data of a project
	struct ZipEntry
	{
		std::string name, data;
	};
	bool zip_write(const std::string& path, const std::vector<ZipEntry>& entries, std::string& error);
	// Reads an archive written by zip_write (stored entries only); false and the reason in error otherwise
	bool zip_read(const std::string& path, std::vector<ZipEntry>& entries, std::string& error);
}

#endif
