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

// Binote - links of the Markdown text: a click in the preview, or Ctrl+click in the editor, follows the link.
// A web address opens the browser; a Markdown or text file opens in Binote (as a chapter of the project when it is one, else as a file);
// an anchor (#heading) goes to the heading; any other file opens with the program of the system.

#include "ui/app.h"
#include "common/dialog.h"
#include "common/i18n.h"
#include "common/util.h"
#include "project/project.h"
#include <algorithm>
#include <cstdlib>

//-----------------------------------------------------------------------------
// [SECTION] Helpers on addresses
//-----------------------------------------------------------------------------

// "%20" -> " ": the addresses of the links are percent-encoded
static std::string percent_decode(const std::string& s)
{
	std::string o;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2]))
		{
			o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
			i += 2;
		}
		else
			o += s[i];
	}
	return o;
}

// The scheme of an address ("https", "mailto", "file"...) in lower case, empty for a path. A drive letter ("C:\...") is not a scheme.
static std::string link_scheme(const std::string& url)
{
	const size_t colon = url.find(':');
	if (colon == std::string::npos || colon < 2)
		return "";
	std::string scheme;
	for (size_t i = 0; i < colon; i++)
	{
		const unsigned char c = (unsigned char)url[i];
		if (!(isalnum(c) || c == '+' || c == '-' || c == '.'))
			return "";
		scheme += (char)tolower(c);
	}
	return scheme;
}

// The anchor of a heading, like GitHub writes it: lower case, spaces become hyphens, punctuation is dropped, accents stay
static std::string heading_slug(const std::string& title)
{
	std::string o;
	for (size_t i = 0; i < title.size(); i++)
	{
		const unsigned char c = (unsigned char)title[i];
		if (c >= 'A' && c <= 'Z')
			o += (char)(c + 32);
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
			o += (char)c;
		else if (c == ' ')
			o += '-';
		else if (c == 0xC3 && i + 1 < title.size() && (unsigned char)title[i + 1] >= 0x80 && (unsigned char)title[i + 1] <= 0x9E && (unsigned char)title[i + 1] != 0x97)
		{
			o += (char)c;							// a capital letter with an accent: its lower case is 0x20 further
			o += (char)((unsigned char)title[++i] + 0x20);
		}
		else if (c >= 0x80)
			o += (char)c;
	}
	return o;
}

// The line (1-based) of the heading which has this anchor in a list of headings, 0 if there is none
static int line_of_anchor(const std::vector<Heading>& headings, const std::string& anchor)
{
	const std::string wanted = heading_slug(percent_decode(anchor));
	for (const Heading& h : headings)
		if (heading_slug(h.title) == wanted)
			return h.line;
	return 0;
}

// The same path written the same way, to compare two paths: one kind of separator, lower case
static std::string path_key(std::string p)
{
	std::replace(p.begin(), p.end(), '\\', '/');
	std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return (char)tolower(c); });
	return p;
}

//-----------------------------------------------------------------------------
// [SECTION] Following a link
//-----------------------------------------------------------------------------

// A link was clicked while the preview was drawn: the work is done at the start of the next frame, because it may replace the project
// and its panes (which are being drawn now). folders: where a relative address is looked for, first the folder of the pane.
void App::queue_link(const std::string& url, const std::vector<std::string>& folders)
{
	_link_url = url;
	_link_folders = folders;
}

// Done at the start of a frame: follows the link which was clicked
void App::open_pending_link()
{
	if (_link_url.empty())
		return;
	const std::string url = _link_url;
	const std::vector<std::string> folders = _link_folders;
	_link_url.clear();
	follow_link(url, folders);
}

// Opens what an address points to: the browser for a web address, Binote for a text, the program of the system for another file
void App::follow_link(std::string url, const std::vector<std::string>& folders)
{
	while (!url.empty() && isspace((unsigned char)url.back()))
		url.pop_back();
	while (!url.empty() && isspace((unsigned char)url.front()))
		url.erase(0, 1);
	if (url.size() > 1 && url.front() == '<' && url.back() == '>')
		url = url.substr(1, url.size() - 2);
	if (url.empty())
		return;

	// An anchor in this text: go to its heading
	if (url[0] == '#')
	{
		const int line = line_of_anchor(_headings, url.substr(1));
		if (line > 0)
			request_open_doc(_cur, line);
		else
			status(i18n::fmt(TR("MSG_ANCHOR_NOT_FOUND"), url.c_str()));
		return;
	}

	// A web address, a mail address: the system opens them. Other schemes are refused (a link must not start any program).
	std::string scheme = link_scheme(url);
	if (scheme == "http" || scheme == "https" || scheme == "mailto" || scheme == "ftp" || scheme == "tel")
	{
		dialog::open_external(url);
		status(i18n::fmt(TR("MSG_LINK_OPENED"), url.c_str()));
		return;
	}
	std::string path = url;
	if (scheme == "file")
	{
		path = percent_decode(url.substr(5));				// file:///C:/folder/text.md -> C:/folder/text.md
		while (path.size() > 2 && path[0] == '/' && path[1] == '/')
			path.erase(0, 1);
		if (path.size() > 2 && path[0] == '/' && path[2] == ':')
			path.erase(0, 1);
	}
	else if (!scheme.empty())
	{
		status(i18n::fmt(TR("MSG_LINK_UNSUPPORTED"), url.c_str()));
		return;
	}
	else
		path = percent_decode(url);

	// A file: without the anchor and the query, looked for beside the pane (then in the other folders)
	std::string anchor;
	const size_t hash = path.find('#');
	if (hash != std::string::npos)
	{
		anchor = path.substr(hash + 1);
		path.erase(hash);
	}
	const size_t query = path.find('?');
	if (query != std::string::npos)
		path.erase(query);
	if (path.empty())
		return;
	std::replace(path.begin(), path.end(), '\\', '/');
	std::string found;
	const bool absolute = path[0] == '/' || (path.size() > 1 && path[1] == ':');
	if (absolute)
		found = util::exists(path) ? path : "";
	else
		for (const std::string& folder : folders)
			if (!folder.empty() && util::exists(util::combine(folder, path)))
			{
				found = util::combine(folder, path);
				break;
			}
	if (found.empty())
	{
		status(i18n::fmt(TR("MSG_FILE_NOT_FOUND"), path.c_str()));
		return;
	}
	if (util::is_directory(found))
	{
		dialog::open_folder(found);
		return;
	}
	const std::string ext = util::extension(found);
	if (ext != ".md" && ext != ".markdown" && ext != ".txt")
	{
		dialog::open_external(found);						// an image, a PDF, a Word file...
		return;
	}

	// A text: a chapter of this project if it is one, else a file of its own
	std::string text;
	util::read_text(found, text);
	const int line = anchor.empty() ? 0 : line_of_anchor(parse_headings(util::split_lines(text), ext != ".txt"), anchor);
	if (_has_project)
	{
		const std::string key = path_key(found);
		for (size_t i = 0; i < _project.docs.size(); i++)
			if (path_key(_project.docs[i].src_path) == key || path_key(_project.docs[i].dst_path) == key)
			{
				request_open_doc((int)i, line);
				return;
			}
	}
	open_file_path(found);
}

// The address of the link under a position of a line of Markdown: [text](address), <address> or a bare http(s) address. Empty if none.
std::string App::link_in_line(const char* line, int length, int column)
{
	const std::string s(line, (size_t)length);
	// [text](address "title")
	for (size_t open = s.find("]("); open != std::string::npos; open = s.find("](", open + 1))
	{
		size_t start = s.rfind('[', open);
		const size_t from = open + 2;
		size_t close = from;
		int depth = 1;
		while (close < s.size() && depth > 0)
		{
			if (s[close] == '(') depth++;
			else if (s[close] == ')') depth--;
			if (depth > 0) close++;
		}
		if (start == std::string::npos || close >= s.size() || (int)start > column || column > (int)close)
			continue;
		std::string address = s.substr(from, close - from);
		const size_t space = address.find(' ');
		if (space != std::string::npos && address[0] != '<')
			address.erase(space);						// the title after the address
		return address;
	}
	// A bare address
	static const char* const prefixes[] = { "https://", "http://", "mailto:" };
	for (const char* prefix : prefixes)
		for (size_t at = s.find(prefix); at != std::string::npos; at = s.find(prefix, at + 1))
		{
			size_t end = at;
			while (end < s.size() && !isspace((unsigned char)s[end]) && s[end] != ')' && s[end] != '>' && s[end] != ']' && s[end] != '"')
				end++;
			while (end > at && (s[end - 1] == '.' || s[end - 1] == ',' || s[end - 1] == ';' || s[end - 1] == ':'))
				end--;
			if ((int)at <= column && column <= (int)end)
				return s.substr(at, end - at);
		}
	return "";
}
