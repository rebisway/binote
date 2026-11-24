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

// Binote - search and replace

#include "project/search.h"
#include <cstdlib>

namespace search
{
	//-------------------------------------------------------------------------
	// UTF-8 <-> wide text. A wide index is mapped back to its byte offset.
	//-------------------------------------------------------------------------

	// Appends a code point to a wide string (UTF-16 on Windows)
	static void put_wide(std::wstring& out, unsigned int cp)
	{
		if (sizeof(wchar_t) == 2 && cp > 0xFFFF)
		{
			cp -= 0x10000;
			out += (wchar_t)(0xD800 + (cp >> 10));
			out += (wchar_t)(0xDC00 + (cp & 0x3FF));
		}
		else
			out += (wchar_t)cp;
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

	// byte_of[w] = byte offset in the text of the wide unit w (one more entry at the end: the size of the text)
	static std::wstring widen(const std::string& s, std::vector<int>* byte_of = nullptr)
	{
		std::wstring w;
		w.reserve(s.size());
		if (byte_of)
			byte_of->reserve(s.size() + 1);
		for (size_t i = 0; i < s.size();)
		{
			int n;
			const unsigned int cp = decode(s, i, &n);
			const size_t before = w.size();
			put_wide(w, cp);
			if (byte_of)
				for (size_t k = before; k < w.size(); k++)
					byte_of->push_back((int)i);
			i += n;
		}
		if (byte_of)
			byte_of->push_back((int)s.size());
		return w;
	}

	// Wide string back to UTF-8
	static std::string narrow(const std::wstring& w)
	{
		std::string s;
		for (size_t i = 0; i < w.size(); i++)
		{
			unsigned int c = (unsigned int)w[i];
			if (sizeof(wchar_t) == 2 && c >= 0xD800 && c < 0xDC00 && i + 1 < w.size())
				c = 0x10000 + ((c - 0xD800) << 10) + ((unsigned int)w[++i] - 0xDC00);
			if (c < 0x80) s += (char)c;
			else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
			else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
			else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 0x3F)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
		}
		return s;
	}

	// Lower case of the letters of Latin 1, Latin extended A, Greek and Cyrillic (the plain C locale only knows ASCII)
	static wchar_t fold(wchar_t c)
	{
		if (c >= 'A' && c <= 'Z') return c + 32;
		if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
		if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x178 && c != 0x17F)
		{
			if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
				return (c % 2 == 1) ? c + 1 : c;
			return (c % 2 == 0) ? c + 1 : c;
		}
		if (c == 0x178) return 0xFF;
		if (c >= 0x391 && c <= 0x3A9) return c + 32;
		if (c >= 0x410 && c <= 0x42F) return c + 32;
		return c;
	}

	// Letters and digits, for the whole word option
	static bool is_word(wchar_t c)
	{
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || (c >= 0x370 && c <= 0x52F);
	}

	//-------------------------------------------------------------------------
	// Escapes
	//-------------------------------------------------------------------------

	// Value of a hexadecimal digit, or -1
	static int hex_value(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	// Appends a code point in UTF-8
	static void put_utf8(std::string& s, unsigned int c)
	{
		std::wstring w;
		put_wide(w, c);
		s += narrow(w);
	}

	// Special characters of the find box: \t \n \r \uXXXX \xHH and the names of the invisible characters (no-break spaces...)
	std::string unescape(const std::string& text)
	{
		std::string out;
		for (size_t i = 0; i < text.size(); i++)
		{
			if (text[i] != '\\' || i + 1 >= text.size())
			{
				out += text[i];
				continue;
			}
			const char e = text[i + 1];
			if (e == 'n') { out += '\n'; i++; }
			else if (e == 't') { out += '\t'; i++; }
			else if (e == 'r') { out += '\r'; i++; }
			else if (e == '0') { out += '\0'; i++; }
			else if (e == '\\') { out += '\\'; i++; }
			else if ((e == 'x' || e == 'u') && i + 2 < text.size())
			{
				size_t p = i + 2;
				const bool braces = e == 'u' && text[p] == '{';
				if (braces)
					p++;
				const size_t digits_max = braces ? 6 : (e == 'x' ? 2 : 4);
				unsigned int value = 0;
				size_t count = 0;
				while (p < text.size() && count < digits_max && hex_value(text[p]) >= 0)
				{
					value = value * 16 + (unsigned int)hex_value(text[p]);
					p++;
					count++;
				}
				const bool closed = !braces || (p < text.size() && text[p] == '}');
				if (count > 0 && (braces || count == digits_max) && closed)
				{
					put_utf8(out, value);
					i = p - 1 + (braces ? 1 : 0);
				}
				else
					out += text[i];
			}
			else
				out += text[i];
		}
		return out;
	}

	// The expression for std::wregex: \w and \W know the letters with accents, \u{...} is written as \uXXXX
	static std::string translate_regex(const std::string& pattern)
	{
		static const char* const accents = "\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u024F";
		std::string out;
		bool in_bracket = false;
		for (size_t i = 0; i < pattern.size(); i++)
		{
			const char c = pattern[i];
			if (c == '\\' && i + 1 < pattern.size())
			{
				const char e = pattern[i + 1];
				if (e == 'w')
				{
					out += in_bracket ? std::string("\\w") + accents : std::string("[\\w") + accents + "]";
					i++;
				}
				else if (e == 'W' && !in_bracket)
				{
					out += std::string("[^\\w") + accents + "]";
					i++;
				}
				else if (e == 'u' && i + 2 < pattern.size() && pattern[i + 2] == '{')
				{
					size_t p = i + 3;
					unsigned int value = 0;
					while (p < pattern.size() && hex_value(pattern[p]) >= 0)
						value = value * 16 + (unsigned int)hex_value(pattern[p++]);
					if (p < pattern.size() && pattern[p] == '}')
					{
						if (value <= 0xFFFF)
						{
							char b[8];
							snprintf(b, sizeof(b), "\\u%04X", value);
							out += b;
						}
						else
							put_utf8(out, value);
						i = p;
					}
					else
						out += c;
				}
				else
				{
					out += c;
					out += e;
					i++;
				}
				continue;
			}
			if (c == '[' && !in_bracket)
				in_bracket = true;
			else if (c == ']' && in_bracket)
				in_bracket = false;
			out += c;
		}
		return out;
	}

	//-------------------------------------------------------------------------
	// Engine
	//-------------------------------------------------------------------------

	// Compiles a pattern with the options (case, regular expression, whole word); false with the error in the engine when it is wrong
	bool Engine::set(const std::string& pattern, const Options& options)
	{
		_pattern = pattern;
		_options = options;
		_error.clear();
		_needle.clear();
		if (pattern.empty())
			return true;
		if (!options.regex)
		{
			_needle = widen(unescape(pattern));
			if (!options.match_case)
				for (wchar_t& c : _needle)
					c = fold(c);
			_whole_text = false;
			return true;
		}
		_whole_text = pattern.find("\\n") != std::string::npos || pattern.find("\\r") != std::string::npos || pattern.find("\\s") != std::string::npos || pattern.find("\\x0a") != std::string::npos
			|| pattern.find("\\u000a") != std::string::npos || pattern.find("\\u000A") != std::string::npos;
		try
		{
			std::regex_constants::syntax_option_type flags = std::regex_constants::ECMAScript;
			if (!options.match_case)
				flags |= std::regex_constants::icase;
			if (_whole_text)
				flags |= std::regex_constants::multiline;
			_regex = std::wregex(widen(translate_regex(pattern)), flags);
		}
		catch (const std::regex_error& e)
		{
			_error = e.what();
			_pattern.clear();
			return false;
		}
		return true;
	}

	// Every match of the pattern in a text, as byte ranges, up to a limit
	void Engine::find_all(const std::string& text, std::vector<Match>& out, size_t limit, const std::string* replacement) const
	{
		out.clear();
		if (_pattern.empty() || !_error.empty())
			return;
		std::vector<int> byte_of;
		const std::wstring wide = widen(text, &byte_of);
		const std::string fmt_text = replacement ? unescape(*replacement) : std::string();

		if (!_options.regex)
		{
			std::wstring hay = wide;
			if (!_options.match_case)
				for (wchar_t& c : hay)
					c = fold(c);
			for (size_t p = hay.find(_needle); p != std::wstring::npos && out.size() < limit; p = hay.find(_needle, p + _needle.size()))
			{
				if (_options.whole_word && ((p > 0 && is_word(wide[p - 1])) || (p + _needle.size() < wide.size() && is_word(wide[p + _needle.size()]))))
					continue;
				Match m;
				m.start = byte_of[p];
				m.end = byte_of[p + _needle.size()];
				if (replacement)
					m.replacement = fmt_text;
				out.push_back(m);
			}
			return;
		}

		const std::wstring wfmt = widen(fmt_text);
		// A chunk is a line, or the whole text if the expression can match a line break
		size_t chunk_begin = 0;
		try
		{
			while (chunk_begin <= wide.size() && out.size() < limit)
			{
				size_t chunk_end = _whole_text ? wide.size() : wide.find(L'\n', chunk_begin);
				if (chunk_end == std::wstring::npos)
					chunk_end = wide.size();
				std::wsregex_iterator it(wide.begin() + chunk_begin, wide.begin() + chunk_end, _regex), end;
				for (; it != end && out.size() < limit; ++it)
				{
					if (it->length() == 0)
						continue;
					const size_t s = chunk_begin + (size_t)it->position(), e = s + (size_t)it->length();
					Match m;
					m.start = byte_of[s];
					m.end = byte_of[e];
					if (replacement)
						m.replacement = narrow(it->format(wfmt));
					out.push_back(m);
				}
				if (chunk_end >= wide.size())
					break;
				chunk_begin = chunk_end + 1;
			}
		}
		catch (const std::exception&)
		{
			// an expression which is too complex for the text: what was found is kept
		}
	}

	// Replaces every match; in a regular expression $1 and $& can be used. count receives the number of replacements.
	std::string Engine::replace_all(const std::string& text, const std::string& replacement, int& count) const
	{
		std::vector<Match> matches;
		find_all(text, matches, (size_t)-1 / 4, &replacement);
		count = (int)matches.size();
		if (matches.empty())
			return text;
		std::string result;
		result.reserve(text.size());
		int pos = 0;
		for (const Match& m : matches)
		{
			result.append(text, (size_t)pos, (size_t)(m.start - pos));
			result += m.replacement;
			pos = m.end;
		}
		result.append(text, (size_t)pos, std::string::npos);
		return result;
	}
}
