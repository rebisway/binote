// Binote - export of a project: one Markdown file, HTML, Word (.docx), EPUB or PDF
// The text is read into blocks (headings, paragraphs, list items) and written by each format; no library is needed.
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#ifndef _EXPORTER_H
#define _EXPORTER_H

#include <string>
#include <vector>

namespace exporter
{
	enum Format { Markdown, Html, Docx, Epub, Pdf };

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
}

#endif
