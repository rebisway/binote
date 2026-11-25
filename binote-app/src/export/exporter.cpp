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

// Binote - export of a project

#include "export/exporter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace exporter
{
	//-------------------------------------------------------------------------
	// Helpers
	//-------------------------------------------------------------------------

	// Escapes the characters which XML and HTML cannot hold as they are
	static std::string xml_escape(const std::string& s)
	{
		std::string o;
		for (char c : s)
		{
			switch (c)
			{
			case '&': o += "&amp;"; break;
			case '<': o += "&lt;"; break;
			case '>': o += "&gt;"; break;
			case '"': o += "&quot;"; break;
			default: o += c;
			}
		}
		return o;
	}

	// Code point at byte i of a UTF-8 text; n receives its length in bytes
	static unsigned int decode(const std::string& s, size_t i, int* n)
	{
		const unsigned char b = (unsigned char)s[i];
		*n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
		if (i + *n > s.size())
			*n = 1;
		unsigned int c = *n == 1 ? b : (b & (0xFF >> (*n + 1)));
		for (int k = 1; k < *n; k++)
			c = (c << 6) | ((unsigned char)s[i + k] & 0x3F);
		return c;
	}

	// File extension of a format (pdf, docx, epub, html, md)
	std::string extension(Format format)
	{
		switch (format)
		{
		case Markdown: return ".md";
		case Html: return ".html";
		case Docx: return ".docx";
		case Epub: return ".epub";
		case Text: return ".txt";
		default: return ".pdf";
		}
	}

	//-------------------------------------------------------------------------
	// Reading the text into blocks
	//-------------------------------------------------------------------------

	// Removes the spaces and tabs at both ends
	static std::string trim(const std::string& s)
	{
		const size_t a = s.find_first_not_of(" \t\r");
		if (a == std::string::npos)
			return "";
		return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
	}

	// Inline: **bold**, *italic*, _italic_, `code`, [text](link); the other marks are dropped
	static std::vector<Run> inline_runs(const std::string& text, bool markdown)
	{
		std::vector<Run> runs;
		if (!markdown)
		{
			runs.push_back({ text, false, false, false });
			return runs;
		}
		std::string cur;
		bool bold = false, italic = false, code = false;
		auto flush = [&]()
		{
			if (!cur.empty())
				runs.push_back({ cur, bold, italic, code });
			cur.clear();
		};
		for (size_t i = 0; i < text.size(); i++)
		{
			const char c = text[i];
			if (code)
			{
				if (c == '`') { flush(); code = false; }
				else cur += c;
				continue;
			}
			if (c == '\\' && i + 1 < text.size() && std::strchr("\\`*_{}[]()#+-.!|~>", text[i + 1]))
			{
				cur += text[++i];
			}
			else if (c == '`') { flush(); code = true; }
			else if (c == '*' && i + 1 < text.size() && text[i + 1] == '*') { flush(); bold = !bold; i++; }
			else if (c == '*') { flush(); italic = !italic; }
			else if (c == '_' && (i == 0 || !std::isalnum((unsigned char)text[i - 1]) ) && (italic || (i + 1 < text.size() && text[i + 1] != ' ')))
			{
				flush();
				italic = !italic;
			}
			else if (c == '_' && italic && (i + 1 >= text.size() || !std::isalnum((unsigned char)text[i + 1])))
			{
				flush();
				italic = false;
			}
			else if (c == '!' && i + 1 < text.size() && text[i + 1] == '[')
			{
				// image: skipped
				const size_t close = text.find(')', i);
				if (close != std::string::npos) i = close;
			}
			else if (c == '[')
			{
				// link: the text stays
				const size_t mid = text.find("](", i);
				const size_t close = mid != std::string::npos ? text.find(')', mid) : std::string::npos;
				if (mid != std::string::npos && close != std::string::npos)
				{
					cur += text.substr(i + 1, mid - i - 1);
					i = close;
				}
				else
					cur += c;
			}
			else if (c == '<')
			{
				const size_t close = text.find('>', i);
				if (close != std::string::npos && close - i < 80 && (std::isalpha((unsigned char)text[i + 1]) || text[i + 1] == '/'))
					i = close;
				else
					cur += c;
			}
			else
				cur += c;
		}
		flush();
		return runs;
	}

	// Cuts a text into blocks (heading, paragraph, list item, quote, code, rule) with inline runs (bold, italic, code, link)
	std::vector<Block> parse(const std::string& text, bool markdown)
	{
		std::vector<Block> blocks;
		bool in_fence = false;
		size_t pos = 0;
		int number = 0;
		while (pos <= text.size())
		{
			size_t eol = text.find('\n', pos);
			if (eol == std::string::npos)
				eol = text.size();
			std::string line = text.substr(pos, eol - pos);
			pos = eol + 1;
			const std::string t = trim(line);
			if (markdown && (t.rfind("```", 0) == 0 || t.rfind("~~~", 0) == 0))
			{
				in_fence = !in_fence;
				continue;
			}
			if (t.empty())
			{
				number = 0;
				continue;
			}
			Block b;
			if (in_fence)
			{
				b.runs.push_back({ line, false, false, true });
				blocks.push_back(b);
				continue;
			}
			if (markdown)
			{
				size_t h = 0;
				while (h < t.size() && t[h] == '#')
					h++;
				if (h >= 1 && h <= 6 && h < t.size() && t[h] == ' ')
				{
					std::string title = trim(t.substr(h));
					while (!title.empty() && title.back() == '#')
						title.pop_back();
					b.type = Block::Heading;
					b.level = (int)h;
					b.runs = inline_runs(trim(title), true);
					blocks.push_back(b);
					continue;
				}
				if (t == "---" || t == "***" || t == "___" || t == "* * *" || t == "- - -")
				{
					b.type = Block::Rule;
					blocks.push_back(b);
					continue;
				}
				if (t[0] == '>')
				{
					b.type = Block::Quote;
					b.runs = inline_runs(trim(t.substr(1)), true);
					blocks.push_back(b);
					continue;
				}
				if ((t[0] == '-' || t[0] == '*' || t[0] == '+') && t.size() > 2 && t[1] == ' ')
				{
					b.type = Block::Bullet;
					b.runs = inline_runs(trim(t.substr(2)), true);
					blocks.push_back(b);
					continue;
				}
				size_t d = 0;
				while (d < t.size() && std::isdigit((unsigned char)t[d]))
					d++;
				if (d > 0 && d + 1 < t.size() && (t[d] == '.' || t[d] == ')') && t[d + 1] == ' ')
				{
					b.type = Block::Numbered;
					b.number = ++number;
					b.runs = inline_runs(trim(t.substr(d + 1)), true);
					blocks.push_back(b);
					continue;
				}
				if (t[0] == '|')
				{
					// table row: the cells one after the other
					std::string cells;
					for (char c : t)
						cells += c == '|' ? ' ' : c;
					if (cells.find_first_not_of(" -:") == std::string::npos)
						continue;
					b.runs = inline_runs(trim(cells), true);
					blocks.push_back(b);
					continue;
				}
			}
			b.runs = inline_runs(t, markdown);
			if (!b.runs.empty())
				blocks.push_back(b);
		}
		return blocks;
	}

	//-------------------------------------------------------------------------
	// ZIP (stored, no compression): .docx and .epub
	//-------------------------------------------------------------------------

	// CRC-32 of the ZIP format
	static unsigned int crc32_of(const std::string& s)
	{
		static unsigned int table[256];
		static bool ready = false;
		if (!ready)
		{
			for (unsigned int i = 0; i < 256; i++)
			{
				unsigned int c = i;
				for (int k = 0; k < 8; k++)
					c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
				table[i] = c;
			}
			ready = true;
		}
		unsigned int crc = 0xFFFFFFFFu;
		for (unsigned char c : s)
			crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8);
		return crc ^ 0xFFFFFFFFu;
	}

	static void put16(std::string& s, unsigned int v) { s += (char)(v & 0xFF); s += (char)((v >> 8) & 0xFF); }
	static void put32(std::string& s, unsigned int v) { put16(s, v & 0xFFFF); put16(s, v >> 16); }

	// Writes a ZIP archive, files stored without compression (enough for docx and epub, and needs no library)
	static bool write_zip(const std::string& path, const std::vector<ZipEntry>& entries, std::string& error)
	{
		std::string out, central;
		for (const ZipEntry& e : entries)
		{
			const unsigned int crc = crc32_of(e.data), offset = (unsigned int)out.size();
			std::string header;
			put32(header, 0x04034b50);
			put16(header, 20);						// version
			put16(header, 0x0800);					// UTF-8 names
			put16(header, 0);						// stored
			put16(header, 0);						// time
			put16(header, 0x21);					// date 1980-01-01
			put32(header, crc);
			put32(header, (unsigned int)e.data.size());
			put32(header, (unsigned int)e.data.size());
			put16(header, (unsigned int)e.name.size());
			put16(header, 0);
			out += header + e.name + e.data;
			put32(central, 0x02014b50);
			put16(central, 20);
			put16(central, 20);
			put16(central, 0x0800);
			put16(central, 0);
			put16(central, 0);
			put16(central, 0x21);
			put32(central, crc);
			put32(central, (unsigned int)e.data.size());
			put32(central, (unsigned int)e.data.size());
			put16(central, (unsigned int)e.name.size());
			put16(central, 0);						// extra
			put16(central, 0);						// comment
			put16(central, 0);						// disk
			put16(central, 0);						// internal attributes
			put32(central, 0);						// external attributes
			put32(central, offset);
			central += e.name;
		}
		const unsigned int central_offset = (unsigned int)out.size();
		out += central;
		put32(out, 0x06054b50);
		put16(out, 0);
		put16(out, 0);
		put16(out, (unsigned int)entries.size());
		put16(out, (unsigned int)entries.size());
		put32(out, (unsigned int)central.size());
		put32(out, central_offset);
		put16(out, 0);
		std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
		if (!f)
		{
			error = "Cannot write " + path;
			return false;
		}
		f.write(out.data(), (std::streamsize)out.size());
		return true;
	}

	bool zip_write(const std::string& path, const std::vector<ZipEntry>& entries, std::string& error)
	{
		return write_zip(path, entries, error);
	}

	static unsigned int get16(const std::string& s, size_t p) { return (unsigned char)s[p] | ((unsigned int)(unsigned char)s[p + 1] << 8); }
	static unsigned int get32(const std::string& s, size_t p) { return get16(s, p) | (get16(s, p + 2) << 16); }

	bool zip_read(const std::string& path, std::vector<ZipEntry>& entries, std::string& error)
	{
		entries.clear();
		std::ifstream f(fs::u8path(path), std::ios::binary);
		if (!f)
		{
			error = "Cannot read " + path;
			return false;
		}
		const std::string z((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		// End of central directory record: the last 22 bytes (no comment in the archives we write)
		size_t eocd = std::string::npos;
		for (size_t i = z.size() >= 22 ? z.size() - 22 : 0; i + 4 <= z.size(); i--)
		{
			if (get32(z, i) == 0x06054b50)
			{
				eocd = i;
				break;
			}
			if (i == 0)
				break;
		}
		if (eocd == std::string::npos || eocd + 22 > z.size())
		{
			error = "Not a ZIP archive";
			return false;
		}
		const unsigned int count = get16(z, eocd + 10);
		size_t p = get32(z, eocd + 16);
		for (unsigned int n = 0; n < count; n++)
		{
			if (p + 46 > z.size() || get32(z, p) != 0x02014b50)
			{
				error = "Damaged archive";
				return false;
			}
			const unsigned int method = get16(z, p + 10), crc = get32(z, p + 16), csize = get32(z, p + 20), usize = get32(z, p + 24);
			const unsigned int nlen = get16(z, p + 28), xlen = get16(z, p + 30), clen = get16(z, p + 32), offset = get32(z, p + 42);
			if (p + 46 + nlen > z.size())
			{
				error = "Damaged archive";
				return false;
			}
			ZipEntry e;
			e.name = z.substr(p + 46, nlen);
			p += 46 + nlen + xlen + clen;
			if (method != 0 || csize != usize)
			{
				error = "Compressed archives are not supported: " + e.name;
				return false;
			}
			if ((size_t)offset + 30 > z.size())
			{
				error = "Damaged archive";
				return false;
			}
			const size_t data = (size_t)offset + 30 + get16(z, offset + 26) + get16(z, offset + 28);
			if (data + usize > z.size())
			{
				error = "Damaged archive";
				return false;
			}
			e.data = z.substr(data, usize);
			if (crc32_of(e.data) != crc)
			{
				error = "Checksum error: " + e.name;
				return false;
			}
			entries.push_back(e);
		}
		return true;
	}

	//-------------------------------------------------------------------------
	// HTML (also the chapters of the EPUB)
	//-------------------------------------------------------------------------

	// Inline runs as HTML
	static std::string runs_html(const std::vector<Run>& runs)
	{
		std::string o;
		for (const Run& r : runs)
		{
			std::string t = xml_escape(r.text);
			if (r.code) t = "<code>" + t + "</code>";
			if (r.italic) t = "<em>" + t + "</em>";
			if (r.bold) t = "<strong>" + t + "</strong>";
			o += t;
		}
		return o;
	}

	// Blocks as HTML (also used by the EPUB chapters)
	static std::string blocks_html(const std::vector<Block>& blocks)
	{
		std::string o;
		std::string open_list;							// "ul" or "ol"
		auto close_list = [&]() { if (!open_list.empty()) o += "</" + open_list + ">\n"; open_list.clear(); };
		for (const Block& b : blocks)
		{
			if (b.type != Block::Bullet && b.type != Block::Numbered)
				close_list();
			switch (b.type)
			{
			case Block::Heading: o += "<h" + std::to_string(std::min(b.level, 6)) + ">" + runs_html(b.runs) + "</h" + std::to_string(std::min(b.level, 6)) + ">\n"; break;
			case Block::Rule: o += "<p class=\"rule\">* * *</p>\n"; break;
			case Block::Quote: o += "<blockquote><p>" + runs_html(b.runs) + "</p></blockquote>\n"; break;
			case Block::Bullet:
			case Block::Numbered:
			{
				const std::string kind = b.type == Block::Bullet ? "ul" : "ol";
				if (open_list != kind)
				{
					close_list();
					o += "<" + kind + ">\n";
					open_list = kind;
				}
				o += "<li>" + runs_html(b.runs) + "</li>\n";
				break;
			}
			default: o += "<p>" + runs_html(b.runs) + "</p>\n";
			}
		}
		close_list();
		return o;
	}

	static const char* const CSS =
		"body { font-family: Georgia, 'Times New Roman', serif; line-height: 1.5; max-width: 40em; margin: 2em auto; padding: 0 1em; }\n"
		"h1, h2, h3 { font-family: Helvetica, Arial, sans-serif; line-height: 1.2; }\n"
		"p { margin: 0 0 0.8em 0; text-align: justify; }\n"
		".rule { text-align: center; }\n"
		".title { text-align: center; margin-top: 30%; }\n"
		"blockquote { margin: 1em 2em; font-style: italic; }\n"
		"code { font-family: Consolas, monospace; }\n";

	// Title of a chapter: its first heading, else its name
	static std::string chapter_blocks_title(const Chapter& c, const std::vector<Block>& blocks)
	{
		if (!c.title.empty())
			return c.title;
		for (const Block& b : blocks)
			if (b.type == Block::Heading)
			{
				std::string t;
				for (const Run& r : b.runs) t += r.text;
				return t;
			}
		return "";
	}

	// One HTML file with a table of contents and the chapters
	static bool write_html(const std::string& path, const std::vector<Chapter>& chapters, const Options& o, std::string& error)
	{
		std::string h = "<!DOCTYPE html>\n<html lang=\"" + xml_escape(o.language) + "\">\n<head>\n<meta charset=\"utf-8\">\n<title>" + xml_escape(o.title) + "</title>\n<style>\n" + CSS + "</style>\n</head>\n<body>\n";
		if (o.title_page && !o.title.empty())
			h += "<div class=\"title\"><h1>" + xml_escape(o.title) + "</h1>\n<p style=\"text-align:center\">" + xml_escape(o.author) + "</p></div>\n";
		for (const Chapter& c : chapters)
			h += std::string(o.page_break ? "<div style=\"page-break-before: always\">\n" : "<div>\n") + blocks_html(parse(c.text, o.markdown)) + "</div>\n";
		h += "</body>\n</html>\n";
		std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
		if (!f) { error = "Cannot write " + path; return false; }
		f << h;
		return true;
	}

	//-------------------------------------------------------------------------
	// Markdown: the chapters in one file
	//-------------------------------------------------------------------------

	// One Markdown file: the chapters one after the other
	static bool write_markdown(const std::string& path, const std::vector<Chapter>& chapters, const Options& o, std::string& error)
	{
		std::string m;
		if (o.title_page && !o.title.empty())
			m += "# " + o.title + "\n\n" + (o.author.empty() ? "" : "*" + o.author + "*\n\n");
		for (size_t i = 0; i < chapters.size(); i++)
		{
			std::string t = chapters[i].text;
			while (!t.empty() && (t.back() == '\n' || t.back() == '\r'))
				t.pop_back();
			size_t a = 0;
			while (a < t.size() && (t[a] == '\n' || t[a] == '\r'))
				a++;
			if (i > 0)
				m += "\n\n";
			m += t.substr(a) + "\n";
		}
		std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
		if (!f) { error = "Cannot write " + path; return false; }
		f << m;
		return true;
	}

	//-------------------------------------------------------------------------
	// Plain text: the chapters one after the other, without any Markdown code
	//-------------------------------------------------------------------------

	static std::string runs_plain(const std::vector<Run>& runs)
	{
		std::string t;
		for (const Run& r : runs)
			t += r.text;
		return t;
	}

	static bool write_plain(const std::string& path, const std::vector<Chapter>& chapters, const Options& o, std::string& error)
	{
		std::string out;
		if (o.title_page && !o.title.empty())
			out += o.title + "\n" + (o.author.empty() ? std::string() : o.author + "\n") + "\n\n";
		for (size_t i = 0; i < chapters.size(); i++)
		{
			std::string chapter;
			const std::vector<Block> blocks = parse(chapters[i].text, o.markdown);
			for (size_t b = 0; b < blocks.size(); b++)
			{
				const Block& bl = blocks[b];
				const std::string text = runs_plain(bl.runs);
				switch (bl.type)
				{
				case Block::Bullet: chapter += "- " + text + "\n"; break;
				case Block::Numbered: chapter += std::to_string(bl.number) + ". " + text + "\n"; break;
				case Block::Rule: chapter += "* * *\n\n"; break;
				default: chapter += text + "\n\n"; break;
				}
				// A list is followed by an empty line
				const bool list = bl.type == Block::Bullet || bl.type == Block::Numbered;
				const bool same_list = b + 1 < blocks.size() && blocks[b + 1].type == bl.type;
				if (list && !same_list)
					chapter += "\n";
			}
			while (!chapter.empty() && chapter.back() == '\n')
				chapter.pop_back();
			if (i > 0)
				out += "\n\n\n";
			out += chapter;
		}
		while (!out.empty() && out.back() == '\n')
			out.pop_back();
		out += "\n";
		std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
		if (!f) { error = "Cannot write " + path; return false; }
		f << out;
		return true;
	}

	//-------------------------------------------------------------------------
	// DOCX
	//-------------------------------------------------------------------------

	static std::string runs_docx(const std::vector<Run>& runs, bool force_italic = false, bool force_bold = false)
	{
		std::string o;
		for (const Run& r : runs)
		{
			std::string props;
			if (r.bold || force_bold) props += "<w:b/>";
			if (r.italic || force_italic) props += "<w:i/>";
			if (r.code) props += "<w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\"/>";
			o += "<w:r>" + (props.empty() ? std::string() : "<w:rPr>" + props + "</w:rPr>") + "<w:t xml:space=\"preserve\">" + xml_escape(r.text) + "</w:t></w:r>";
		}
		return o;
	}

	// Word document (Office Open XML in a ZIP): styles for the headings, paragraphs, lists and quotes
	static bool write_docx(const std::string& path, const std::vector<Chapter>& chapters, const Options& o, std::string& error)
	{
		std::string body;
		auto para = [&](const std::string& ppr, const std::string& runs) { body += "<w:p>" + (ppr.empty() ? std::string() : "<w:pPr>" + ppr + "</w:pPr>") + runs + "</w:p>"; };
		if (o.title_page && !o.title.empty())
		{
			para("<w:pStyle w:val=\"Title\"/>", "<w:r><w:t xml:space=\"preserve\">" + xml_escape(o.title) + "</w:t></w:r>");
			if (!o.author.empty())
				para("<w:jc w:val=\"center\"/>", "<w:r><w:t xml:space=\"preserve\">" + xml_escape(o.author) + "</w:t></w:r>");
		}
		for (size_t ci = 0; ci < chapters.size(); ci++)
		{
			bool first = true;
			for (const Block& b : parse(chapters[ci].text, o.markdown))
			{
				std::string ppr;
				if (first && (o.page_break && (ci > 0 || o.title_page)))
					ppr += "<w:pageBreakBefore/>";
				first = false;
				switch (b.type)
				{
				case Block::Heading:
					para("<w:pStyle w:val=\"Heading" + std::to_string(std::min(b.level, 3)) + "\"/>" + ppr, runs_docx(b.runs));
					break;
				case Block::Rule:
					para(ppr + "<w:jc w:val=\"center\"/>", "<w:r><w:t>* * *</w:t></w:r>");
					break;
				case Block::Quote:
					para(ppr + "<w:ind w:left=\"720\"/>", runs_docx(b.runs, true));
					break;
				case Block::Bullet:
					para(ppr + "<w:ind w:left=\"567\" w:hanging=\"283\"/>", "<w:r><w:t xml:space=\"preserve\">\xE2\x80\xA2 </w:t></w:r>" + runs_docx(b.runs));
					break;
				case Block::Numbered:
					para(ppr + "<w:ind w:left=\"567\" w:hanging=\"283\"/>", "<w:r><w:t xml:space=\"preserve\">" + std::to_string(b.number) + ". </w:t></w:r>" + runs_docx(b.runs));
					break;
				default:
					para(ppr, runs_docx(b.runs));
				}
			}
		}
		const std::string lang = o.language.rfind("fr", 0) == 0 ? "fr-FR" : (o.language.rfind("es", 0) == 0 ? "es-ES" : "en-US");
		std::vector<ZipEntry> e;
		e.push_back({ "[Content_Types].xml", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/><Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/></Types>" });
		e.push_back({ "_rels/.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>" });
		e.push_back({ "word/_rels/document.xml.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>" });
		e.push_back({ "word/document.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>" + body + "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1417\" w:right=\"1417\" w:bottom=\"1417\" w:left=\"1417\" w:header=\"709\" w:footer=\"709\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>" });
		e.push_back({ "word/styles.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
			"<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Times New Roman\" w:hAnsi=\"Times New Roman\" w:cs=\"Times New Roman\"/><w:sz w:val=\"23\"/><w:lang w:val=\"" + lang + "\"/></w:rPr></w:rPrDefault>"
			"<w:pPrDefault><w:pPr><w:spacing w:after=\"120\" w:line=\"300\" w:lineRule=\"auto\"/><w:jc w:val=\"both\"/></w:pPr></w:pPrDefault></w:docDefaults>"
			"<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
			"<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/><w:basedOn w:val=\"Normal\"/><w:pPr><w:spacing w:before=\"3000\" w:after=\"240\"/><w:jc w:val=\"center\"/></w:pPr><w:rPr><w:b/><w:sz w:val=\"56\"/></w:rPr></w:style>"
			"<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:keepNext/><w:spacing w:before=\"360\" w:after=\"240\"/><w:jc w:val=\"left\"/><w:outlineLvl w:val=\"0\"/></w:pPr><w:rPr><w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\"/><w:b/><w:sz w:val=\"36\"/></w:rPr></w:style>"
			"<w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:keepNext/><w:spacing w:before=\"280\" w:after=\"160\"/><w:jc w:val=\"left\"/><w:outlineLvl w:val=\"1\"/></w:pPr><w:rPr><w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\"/><w:b/><w:sz w:val=\"28\"/></w:rPr></w:style>"
			"<w:style w:type=\"paragraph\" w:styleId=\"Heading3\"><w:name w:val=\"heading 3\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:keepNext/><w:spacing w:before=\"240\" w:after=\"120\"/><w:jc w:val=\"left\"/><w:outlineLvl w:val=\"2\"/></w:pPr><w:rPr><w:rFonts w:ascii=\"Arial\" w:hAnsi=\"Arial\"/><w:b/><w:sz w:val=\"24\"/></w:rPr></w:style>"
			"</w:styles>" });
		return write_zip(path, e, error);
	}

	//-------------------------------------------------------------------------
	// EPUB 3
	//-------------------------------------------------------------------------

	// EPUB 3: one XHTML file per chapter, navigation document, manifest
	static bool write_epub(const std::string& path, const std::vector<Chapter>& chapters, const Options& o, std::string& error)
	{
		std::vector<ZipEntry> e;
		e.push_back({ "mimetype", "application/epub+zip" });
		e.push_back({ "META-INF/container.xml", "<?xml version=\"1.0\"?>\n<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>" });
		e.push_back({ "OEBPS/style.css", CSS });
		std::string manifest, spine, nav;
		const std::string xhtml_head = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" lang=\"" + xml_escape(o.language) + "\" xml:lang=\"" + xml_escape(o.language) + "\">\n<head><meta charset=\"utf-8\"/><title>";
		int index = 0;
		if (o.title_page && !o.title.empty())
		{
			e.push_back({ "OEBPS/title.xhtml", xhtml_head + xml_escape(o.title) + "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head>\n<body><div class=\"title\"><h1>" + xml_escape(o.title) + "</h1><p style=\"text-align:center\">" + xml_escape(o.author) + "</p></div></body></html>" });
			manifest += "<item id=\"title\" href=\"title.xhtml\" media-type=\"application/xhtml+xml\"/>\n";
			spine += "<itemref idref=\"title\"/>\n";
		}
		for (const Chapter& c : chapters)
		{
			const std::vector<Block> blocks = parse(c.text, o.markdown);
			const std::string id = "ch" + std::to_string(++index), file = id + ".xhtml";
			std::string title = chapter_blocks_title(c, blocks);
			if (title.empty())
				title = "Chapitre " + std::to_string(index);
			e.push_back({ "OEBPS/" + file, xhtml_head + xml_escape(title) + "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head>\n<body>\n" + blocks_html(blocks) + "</body></html>" });
			manifest += "<item id=\"" + id + "\" href=\"" + file + "\" media-type=\"application/xhtml+xml\"/>\n";
			spine += "<itemref idref=\"" + id + "\"/>\n";
			nav += "<li><a href=\"" + file + "\">" + xml_escape(title) + "</a></li>\n";
		}
		e.push_back({ "OEBPS/nav.xhtml", xhtml_head + "Sommaire</title></head>\n<body><nav epub:type=\"toc\"><h1>Sommaire</h1><ol>\n" + nav + "</ol></nav></body></html>" });
		char date[32];
		const std::time_t now = std::time(nullptr);
		std::tm tm_utc{};
#ifdef _WIN32
		gmtime_s(&tm_utc, &now);
#else
		gmtime_r(&now, &tm_utc);
#endif
		std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
		e.push_back({ "OEBPS/content.opf", "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"uid\">\n<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n<dc:identifier id=\"uid\">urn:binote:" + std::to_string(crc32_of(o.title + o.author + date)) + "</dc:identifier>\n<dc:title>" + xml_escape(o.title.empty() ? "Sans titre" : o.title) + "</dc:title>\n<dc:creator>" + xml_escape(o.author) + "</dc:creator>\n<dc:language>" + xml_escape(o.language) + "</dc:language>\n<meta property=\"dcterms:modified\">" + date + "</meta>\n</metadata>\n<manifest>\n<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n" + manifest + "</manifest>\n<spine>\n" + spine + "</spine>\n</package>" });
		return write_zip(path, e, error);
	}

	//-------------------------------------------------------------------------
	// PDF: A4, Times (standard fonts, WinAnsi), no library
	//-------------------------------------------------------------------------

	// Times-Roman widths (1/1000 em) of the characters 32..126
	static const short TIMES_WIDTHS[95] = {
		250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444,
		921, 722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722, 556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333, 278, 333, 469, 500,
		333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500, 500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541 };

	// A character of the text in the WinAnsi encoding (0 = none)
	static unsigned char winansi(unsigned int c)
	{
		if (c < 0x80) return c >= 32 || c == '\t' ? (unsigned char)c : 0;
		if (c == 0xA0 || c == 0x202F || c == 0x2009 || c == 0x2007) return 0xA0;
		if (c >= 0xA1 && c <= 0xFF) return (unsigned char)c;
		switch (c)
		{
		case 0x20AC: return 0x80;
		case 0x2018: return 0x91;
		case 0x2019: return 0x92;
		case 0x201C: return 0x93;
		case 0x201D: return 0x94;
		case 0x2022: return 0x95;
		case 0x2013: return 0x96;
		case 0x2014: return 0x97;
		case 0x2026: return 0x85;
		case 0x2122: return 0x99;
		case 0x152: return 0x8C;
		case 0x153: return 0x9C;
		case 0x178: return 0x9F;
		case 0x2011: return '-';
		case 0x200B: case 0x200C: case 0x200D: case 0xFEFF: case 0xAD: return 0;
		}
		return '?';
	}

	// Width of a character of the standard Times fonts (thousandths of em), for the line breaks of the PDF
	static int glyph_width(unsigned char w)
	{
		if (w >= 32 && w <= 126)
			return TIMES_WIDTHS[w - 32];
		switch (w)
		{
		case 0xA0: return 250;
		case 0x85: return 1000;
		case 0x91: case 0x92: return 333;
		case 0x93: case 0x94: return 444;
		case 0x95: return 350;
		case 0x96: return 500;
		case 0x97: return 1000;
		case 0x9C: return 722;
		case 0x8C: return 889;
		case 0xAB: case 0xBB: return 500;
		case 0xC6: return 889;
		case 0xE6: return 667;
		}
		// Letters with accents: the width of the letter
		static const char* const base = "AAAAAA?CEEEEIIIIDNOOOOO?OUUUUYPsaaaaaa?ceeeeiiiionooooo?ouuuuypy";
		if (w >= 0xC0)
		{
			const char b = base[w - 0xC0];
			if (b != '?')
				return TIMES_WIDTHS[b - 32];
		}
		return 500;
	}

	struct PdfWriter
	{
		std::vector<std::string> pages;					// content stream of each page
		std::string cur;
		double y = 0.0;
		int page_number = 0;
		const double width = 595.0, height = 842.0, margin = 64.0;

		void new_page()
		{
			finish_page();
			y = height - margin;
			page_number++;
		}

		void finish_page()
		{
			if (page_number == 0)
				return;
			char buf[128];
			const std::string number = std::to_string(page_number);
			snprintf(buf, sizeof(buf), "BT /F1 9 Tf %.2f %.2f Td (", width / 2.0 - number.size() * 2.3, margin / 2.0);
			cur += buf + number + ") Tj ET\n";
			pages.push_back(cur);
			cur.clear();
		}

		static std::string pdf_string(const std::string& bytes)
		{
			std::string o = "(";
			for (unsigned char c : bytes)
			{
				if (c == '(' || c == ')' || c == '\\') { o += '\\'; o += (char)c; }
				else if (c < 32 || c > 126) { char b[8]; snprintf(b, sizeof(b), "\\%03o", c); o += b; }
				else o += (char)c;
			}
			return o + ")";
		}
	};

	struct Piece
	{
		std::string bytes;								// WinAnsi
		int font;										// 1 roman, 2 bold, 3 italic, 4 mono (Courier)
		double width;									// of the piece at 1 pt
		bool space;
	};

	// A paragraph written with a font size; words are not cut except if one is longer than the line
	static void pdf_paragraph(PdfWriter& w, const std::vector<Run>& runs, double size, double indent, double first_indent, double after, bool heading, bool center, int force_font)
	{
		const double line_height = size * 1.38;
		const double max_width = w.width - 2.0 * w.margin - indent;
		std::vector<Piece> pieces;
		for (const Run& r : runs)
		{
			const int font = force_font ? force_font : (r.code ? 4 : (r.bold ? 2 : (r.italic ? 3 : 1)));
			std::string word;
			auto push = [&](bool space)
			{
				if (word.empty() && !space)
					return;
				Piece p;
				p.bytes = space ? " " : word;
				p.font = font;
				p.space = space;
				double tw = 0.0;
				for (unsigned char c : p.bytes)
					tw += font == 4 ? 600 : glyph_width(c) * (font == 2 ? 1.04 : 1.0);
				p.width = tw / 1000.0;
				pieces.push_back(p);
				word.clear();
			};
			for (size_t i = 0; i < r.text.size();)
			{
				int n;
				const unsigned int cp = decode(r.text, i, &n);
				i += n;
				if (cp == ' ' || cp == '\t' || cp == '\n')
				{
					push(false);
					push(true);
				}
				else
				{
					const unsigned char b = winansi(cp);
					if (b)
						word += (char)b;
				}
			}
			push(false);
		}
		// Lines
		struct Line { std::vector<Piece> items; double width = 0.0; };
		std::vector<Line> lines(1);
		double avail = max_width - first_indent;
		for (const Piece& p : pieces)
		{
			Line& l = lines.back();
			if (p.space)
			{
				if (!l.items.empty())
				{
					l.items.push_back(p);
					l.width += p.width * size;
				}
				continue;
			}
			double pw = p.width * size;
			if (l.width + pw > avail && !l.items.empty())
			{
				while (!l.items.empty() && l.items.back().space)
				{
					l.width -= l.items.back().width * size;
					l.items.pop_back();
				}
				lines.emplace_back();
				avail = max_width;
			}
			lines.back().items.push_back(p);
			lines.back().width += pw;
		}
		for (size_t li = 0; li < lines.size(); li++)
		{
			Line& l = lines[li];
			while (!l.items.empty() && l.items.back().space)
			{
				l.width -= l.items.back().width * size;
				l.items.pop_back();
			}
			if (l.items.empty() && lines.size() > 1)
				continue;
			if (w.y - line_height < w.margin)
				w.new_page();
			w.y -= line_height;
			double x = w.margin + indent + (li == 0 ? first_indent : 0.0);
			if (center)
				x = (w.width - l.width) / 2.0;
			// consecutive pieces with the same font are written together
			size_t i = 0;
			while (i < l.items.size())
			{
				size_t j = i;
				std::string text;
				double tw = 0.0;
				while (j < l.items.size() && l.items[j].font == l.items[i].font)
				{
					text += l.items[j].bytes;
					tw += l.items[j].width * size;
					j++;
				}
				char buf[96];
				snprintf(buf, sizeof(buf), "BT /F%d %.2f Tf %.2f %.2f Td ", l.items[i].font, size, x, w.y);
				w.cur += buf + PdfWriter::pdf_string(text) + " Tj ET\n";
				x += tw;
				i = j;
			}
		}
		(void)heading;
		w.y -= after;
	}

	// PDF written by hand: standard Times fonts (WinAnsi), line breaking and pagination by the exporter, page numbers, outline
	static bool write_pdf(const std::string& path, const std::vector<Chapter>& chapters, const Options& o, std::string& error)
	{
		PdfWriter w;
		auto plain = [](const std::string& t) { return std::vector<Run>{ { t, false, false, false } }; };
		if (o.title_page && !o.title.empty())
		{
			w.new_page();
			w.y -= 230.0;
			pdf_paragraph(w, plain(o.title), 30.0, 0, 0, 24.0, true, true, 2);
			if (!o.author.empty())
				pdf_paragraph(w, plain(o.author), 14.0, 0, 0, 0, false, true, 1);
		}
		for (size_t ci = 0; ci < chapters.size(); ci++)
		{
			bool first = true;
			for (const Block& b : parse(chapters[ci].text, o.markdown))
			{
				if (first && (o.page_break || w.page_number == 0))
					w.new_page();
				first = false;
				switch (b.type)
				{
				case Block::Heading:
				{
					const double size = b.level == 1 ? 22.0 : (b.level == 2 ? 16.0 : 13.0);
					if (w.y - size * 3.0 < w.margin)
						w.new_page();
					w.y -= b.level == 1 ? 18.0 : 12.0;
					pdf_paragraph(w, b.runs, size, 0, 0, b.level == 1 ? 14.0 : 8.0, true, false, 2);
					break;
				}
				case Block::Rule:
					pdf_paragraph(w, plain("* * *"), 11.0, 0, 0, 8.0, false, true, 1);
					break;
				case Block::Quote:
					pdf_paragraph(w, b.runs, 11.0, 28.0, 0, 7.0, false, false, 3);
					break;
				case Block::Bullet:
				{
					std::vector<Run> runs = { { "\xE2\x80\xA2 ", false, false, false } };
					runs.insert(runs.end(), b.runs.begin(), b.runs.end());
					pdf_paragraph(w, runs, 11.0, 14.0, -14.0, 3.0, false, false, 0);
					break;
				}
				case Block::Numbered:
				{
					std::vector<Run> runs = { { std::to_string(b.number) + ". ", false, false, false } };
					runs.insert(runs.end(), b.runs.begin(), b.runs.end());
					pdf_paragraph(w, runs, 11.0, 14.0, -14.0, 3.0, false, false, 0);
					break;
				}
				default:
					pdf_paragraph(w, b.runs, 11.0, 0, 0, 7.0, false, false, 0);
				}
			}
		}
		w.finish_page();
		if (w.pages.empty())
		{
			error = "Nothing to export";
			return false;
		}
		// Objects: 1 catalog, 2 pages, 3..6 fonts, 7 info, then a page and its content for each page
		std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
		std::vector<size_t> offsets;
		auto add = [&](const std::string& body)
		{
			offsets.push_back(pdf.size());
			pdf += std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
		};
		std::string kids;
		const size_t first_page_obj = 8;
		for (size_t i = 0; i < w.pages.size(); i++)
			kids += std::to_string(first_page_obj + i * 2) + " 0 R ";
		add("<< /Type /Catalog /Pages 2 0 R >>");
		add("<< /Type /Pages /Kids [ " + kids + "] /Count " + std::to_string(w.pages.size()) + " >>");
		add("<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>");
		add("<< /Type /Font /Subtype /Type1 /BaseFont /Times-Bold /Encoding /WinAnsiEncoding >>");
		add("<< /Type /Font /Subtype /Type1 /BaseFont /Times-Italic /Encoding /WinAnsiEncoding >>");
		add("<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>");
		add("<< /Title " + PdfWriter::pdf_string(o.title) + " /Author " + PdfWriter::pdf_string(o.author) + " /Producer (Binote) >>");
		for (size_t i = 0; i < w.pages.size(); i++)
		{
			const size_t content = first_page_obj + i * 2 + 1;
			add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R >> >> /Contents " + std::to_string(content) + " 0 R >>");
			add("<< /Length " + std::to_string(w.pages[i].size()) + " >>\nstream\n" + w.pages[i] + "endstream");
		}
		const size_t xref = pdf.size();
		pdf += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
		for (size_t off : offsets)
		{
			char b[32];
			snprintf(b, sizeof(b), "%010zu 00000 n \n", off);
			pdf += b;
		}
		pdf += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R /Info 7 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
		std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
		if (!f) { error = "Cannot write " + path; return false; }
		f.write(pdf.data(), (std::streamsize)pdf.size());
		return true;
	}

	// Exports the chapters in the chosen format
	bool write(const std::string& path, const std::vector<Chapter>& chapters, const Options& options, std::string& error)
	{
		error.clear();
		if (chapters.empty())
		{
			error = "Nothing to export";
			return false;
		}
		switch (options.format)
		{
		case Markdown: return write_markdown(path, chapters, options, error);
		case Text: return write_plain(path, chapters, options, error);
		case Html: return write_html(path, chapters, options, error);
		case Docx: return write_docx(path, chapters, options, error);
		case Epub: return write_epub(path, chapters, options, error);
		default: return write_pdf(path, chapters, options, error);
		}
	}
}
