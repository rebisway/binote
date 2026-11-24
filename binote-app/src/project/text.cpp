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

#include "project/text.h"
#include "common/util.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "typo.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

//-----------------------------------------------------------------------------
// UTF-8
//-----------------------------------------------------------------------------

// UTF-8 to code points; invalid bytes are kept as they are
static std::u32string to_u32(const std::string& s)
{
	std::u32string o;
	for (size_t i = 0; i < s.size();)
	{
		unsigned char c = (unsigned char)s[i];
		int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
		char32_t cp = len == 1 ? c : (c & (0xFF >> (len + 1)));
		for (int k = 1; k < len && i + k < s.size(); k++)
			cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
		o += cp;
		i += len;
	}
	return o;
}

// Appends a code point in UTF-8
static void put_utf8(std::string& o, char32_t c)
{
	if (c < 0x80) o += (char)c;
	else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
	else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
	else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
}

// Code points to UTF-8
static std::string to_utf8(const std::u32string& s)
{
	std::string o;
	for (char32_t c : s)
		put_utf8(o, c);
	return o;
}

//-----------------------------------------------------------------------------
// Typography: a thin layer over the typo engine (typography-engine/), one profile per language
//-----------------------------------------------------------------------------

namespace typography
{
	static bool is_letter(char32_t c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || c == 0x153 || c == 0x152;
	}
	static bool is_digit(char32_t c)	{ return c >= '0' && c <= '9'; }

	static int g_quote_style = 0;
	void set_quote_style(int style) { g_quote_style = style; }

	static TypoProfile* g_profile = nullptr;		// the profile of the language, or generic.typo
	static bool g_specific = false;					// false: the generic profile
	static std::string g_language;					// the code the profile was loaded for
	static bool g_loaded = false;
	static std::string g_path, g_name;				// the file of the profile in use, and its name

	static std::string read_all(const std::string& path)
	{
		std::ifstream f(fs::u8path(path), std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}

	// Folders which may hold profiles, the first one wins: the user's, the one beside the program, the sources
	static std::vector<std::string> profile_folders()
	{
		const std::string app = util::application_path();
		return { util::combine(util::data_path(), "typography"), util::combine(app, "typography"), util::combine(app, "../../typography-engine/profiles"),
			util::combine(app, "../typography-engine/profiles"), util::combine(app, "../../../typography-engine/profiles") };
	}

	static std::string find_profile(const std::string& code)
	{
		std::error_code ec;
		for (const std::string& d : profile_folders())
		{
			const std::string path = util::combine(d, code + ".typo");
			if (fs::exists(fs::u8path(path), ec))
				return path;
		}
		return "";
	}

	// Loads a profile and the ones it extends. Returns NULL (with a message in the log) when it cannot be read.
	static TypoProfile* load_profile(const std::string& code, int depth = 0)
	{
		const std::string path = find_profile(code);
		if (path.empty() || depth > 4)
			return nullptr;
		const std::string text = read_all(path);
		TypoProfile* parent = nullptr;
		const size_t at = text.find("extends");
		if (at != std::string::npos)
		{
			size_t b = text.find('=', at), e = text.find('\n', at);
			if (b != std::string::npos && b < e)
			{
				std::string name = text.substr(b + 1, e - b - 1);
				while (!name.empty() && (name.back() == '\r' || name.back() == ' '))
					name.pop_back();
				name.erase(0, name.find_first_not_of(" \t"));
				if (!name.empty())
					parent = load_profile(name, depth + 1);
			}
		}
		std::string error;
		TypoProfile* p = Typo::CreateProfile(text.c_str(), parent, &error);
		if (p && depth == 0)
		{
			g_path = path;
			g_name = Typo::GetName(p);
		}
		if (parent)
			Typo::DestroyProfile(parent);
		if (!p)
			Logger::warning("Typography profile " + path + ": " + error);
		return p;
	}

	// The profile of a language: "fr-FR" looks for fr-FR.typo, then fr.typo, then generic.typo
	void set_language(const std::string& code)
	{
		if (g_loaded && code == g_language)
			return;
		g_loaded = true;
		g_language = code;
		if (g_profile)
			Typo::DestroyProfile(g_profile);
		g_profile = nullptr;
		g_specific = false;
		g_path.clear();
		g_name.clear();
		std::string lower;
		for (char c : code)
			lower += (char)tolower((unsigned char)c);
		std::vector<std::string> tries = { lower };
		if (lower.size() > 2)
			tries.push_back(lower.substr(0, 2));
		for (const std::string& t : tries)
			if (!t.empty() && (g_profile = load_profile(t)) != nullptr)
			{
				g_specific = true;
				return;
			}
		g_profile = load_profile("generic");
		if (!g_profile)
			Logger::warning("Typography: no profile found (folder typography beside the program)");
	}

	bool has_profile() { return g_specific; }

	std::string profile_name()	{ return g_name; }
	std::string profile_path()	{ return g_path; }
	std::string user_folder()	{ return util::combine(util::data_path(), "typography"); }
	bool profile_is_user()		{ return !g_path.empty() && g_path.compare(0, user_folder().size(), user_folder()) == 0; }

	// Profiles are read again the next time set_language is called
	void reload() { g_loaded = false; }

	// Copies the profile in use to the folder of the user, where it can be edited and replaces the shipped one. False if it is already there.
	bool create_user_copy()
	{
		if (g_path.empty() || profile_is_user())
			return false;
		std::error_code ec;
		fs::create_directories(fs::u8path(user_folder()), ec);
		const std::string target = util::combine(user_folder(), fs::u8path(g_path).filename().u8string());
		fs::copy_file(fs::u8path(g_path), fs::u8path(target), fs::copy_options::skip_existing, ec);
		if (ec)
			return false;
		reload();
		return true;
	}

	static TypoOptions options(bool markdown)
	{
		TypoOptions o;
		o.QuoteStyle = g_quote_style;
		o.Markdown = markdown;
		return o;
	}

	std::string fix_line(const std::string& line, bool markdown)
	{
		return g_profile ? Typo::FixLine(g_profile, options(markdown), line) : line;
	}

	std::string fix_text(const std::string& text, bool markdown)
	{
		return g_profile ? Typo::FixText(g_profile, options(markdown), text) : text;
	}

	std::string simplify_line(const std::string& line, bool markdown)
	{
		return g_profile ? Typo::SimplifyLine(g_profile, options(markdown), line) : line;
	}

	std::string simplify_text(const std::string& text, bool markdown)
	{
		return g_profile ? Typo::SimplifyText(g_profile, options(markdown), text) : text;
	}

	Typed on_typed(const std::string& typed, const std::string& before)
	{
		Typed r;
		if (!g_profile)
			return r;
		const TypoEdit e = Typo::OnTyped(g_profile, options(false), typed, before);
		r.valid = e.Valid;
		r.start = e.Start;
		r.length = e.Length;
		r.text = e.Text;
		r.caret_extra = e.CaretExtra;
		return r;
	}
}

//-----------------------------------------------------------------------------
// Diff
//-----------------------------------------------------------------------------

namespace diff
{

	// Longest increasing subsequence of the anchors (sorted by source index), by destination index
	static std::vector<std::pair<int, int>> lis(const std::vector<std::pair<int, int>>& anchors)
	{
		std::vector<int> tails, prev(anchors.size(), -1), tail_idx;
		for (size_t i = 0; i < anchors.size(); i++)
		{
			auto it = std::lower_bound(tails.begin(), tails.end(), anchors[i].second);
			size_t k = it - tails.begin();
			if (it == tails.end()) { tails.push_back(anchors[i].second); tail_idx.push_back((int)i); }
			else { *it = anchors[i].second; tail_idx[k] = (int)i; }
			prev[i] = k > 0 ? tail_idx[k - 1] : -1;
		}
		std::vector<std::pair<int, int>> result;
		for (int i = tails.empty() ? -1 : tail_idx.back(); i >= 0; i = prev[i])
			result.push_back(anchors[i]);
		std::reverse(result.begin(), result.end());
		return result;
	}

	// Patience diff: the lines which are unique on both sides anchor the alignment, the gaps are solved again
	static void patience(const std::vector<std::string>& a, const std::vector<std::string>& b, int a0, int a1, int b0, int b1, std::vector<Block>& out)
	{
		while (a0 < a1 && b0 < b1 && a[a0] == b[b0]) { a0++; b0++; }
		while (a0 < a1 && b0 < b1 && a[a1 - 1] == b[b1 - 1]) { a1--; b1--; }
		if (a0 == a1 || b0 == b1)
		{
			if (a0 < a1 || b0 < b1)
				out.push_back({ a0, a1, b0, b1 });
			return;
		}
		struct Info { int ca = 0, cb = 0, ia = 0, ib = 0; };
		std::unordered_map<std::string, Info> map;
		for (int i = a0; i < a1; i++) { Info& n = map[a[i]]; n.ca++; n.ia = i; }
		for (int i = b0; i < b1; i++) { Info& n = map[b[i]]; n.cb++; n.ib = i; }
		std::vector<std::pair<int, int>> anchors;
		for (int i = a0; i < a1; i++)
		{
			const Info& n = map[a[i]];
			if (n.ca == 1 && n.cb == 1)
				anchors.push_back({ n.ia, n.ib });
		}
		anchors = lis(anchors);
		if (anchors.empty())
		{
			out.push_back({ a0, a1, b0, b1 });
			return;
		}
		int pa = a0, pb = b0;
		for (const auto& an : anchors)
		{
			patience(a, b, pa, an.first, pb, an.second, out);
			pa = an.first + 1;
			pb = an.second + 1;
		}
		patience(a, b, pa, a1, pb, b1, out);
	}

	struct Token { int start, length; };

	static std::vector<Token> tokens(const std::string& s)
	{
		std::vector<Token> t;
		size_t i = 0;
		while (i < s.size())
		{
			while (i < s.size() && s[i] == ' ') i++;
			size_t j = i;
			while (j < s.size() && s[j] != ' ') j++;
			if (j > i)
				t.push_back({ (int)i, (int)(j - i) });
			i = j;
		}
		return t;
	}

	static void words(const std::string& a, const std::string& b, int pa, int pb, Result& r)
	{
		const std::vector<Token> ta = tokens(a), tb = tokens(b);
		const size_t m = ta.size(), n = tb.size();
		if (m == 0 || n == 0 || m * n > 4000000)
			return;
		std::vector<std::vector<int>> lcs(m + 1, std::vector<int>(n + 1, 0));
		for (size_t i = m; i-- > 0;)
			for (size_t j = n; j-- > 0;)
				lcs[i][j] = a.compare(ta[i].start, ta[i].length, b, tb[j].start, tb[j].length) == 0 ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
		std::vector<bool> keep_a(m, false), keep_b(n, false);
		size_t i = 0, j = 0;
		while (i < m && j < n)
		{
			if (a.compare(ta[i].start, ta[i].length, b, tb[j].start, tb[j].length) == 0) { keep_a[i] = keep_b[j] = true; i++; j++; }
			else if (lcs[i + 1][j] >= lcs[i][j + 1]) i++;
			else j++;
		}
		auto emit = [](const std::vector<Token>& t, const std::vector<bool>& keep, int para, int kind, std::vector<Word>& out)
		{
			for (size_t k = 0; k < t.size(); k++)
			{
				if (keep[k])
					continue;
				size_t e = k;
				while (e + 1 < t.size() && !keep[e + 1])
					e++;
				out.push_back({ para, t[k].start, t[e].start + t[e].length - t[k].start, kind });
				k = e;
			}
		};
		emit(ta, keep_a, pa, WordRemoved, r.src_words);
		emit(tb, keep_b, pb, WordAdded, r.dst_words);
	}

	// Share of the words of the shorter paragraph found in the other one (0..1)
	static double similarity(const std::string& a, const std::string& b)
	{
		std::unordered_set<std::string> sa, sb;
		for (const Token& t : tokens(a)) sa.insert(a.substr((size_t)t.start, (size_t)t.length));
		for (const Token& t : tokens(b)) sb.insert(b.substr((size_t)t.start, (size_t)t.length));
		if (sa.empty() || sb.empty())
			return 0.0;
		size_t common = 0;
		for (const std::string& w : sa)
			common += sb.count(w);
		return (double)common / (double)std::min(sa.size(), sb.size());
	}

	// A block with a different number of paragraphs on each side (split, merged): the paragraphs which look alike, in order, get their words compared
	static void words_in_block(const std::vector<std::string>& src, const std::vector<std::string>& dst, const Block& b, Result& r)
	{
		const int n = b.a1 - b.a0, m = b.b1 - b.b0;
		if (n * m > 400)
			return;
		std::vector<double> sim((size_t)(n * m));
		for (int i = 0; i < n; i++)
			for (int j = 0; j < m; j++)
				sim[(size_t)(i * m + j)] = similarity(src[(size_t)(b.a0 + i)], dst[(size_t)(b.b0 + j)]);
		std::vector<double> best((size_t)((n + 1) * (m + 1)), 0.0);
		auto at = [&](int i, int j) -> double& { return best[(size_t)(i * (m + 1) + j)]; };
		for (int i = n - 1; i >= 0; i--)
			for (int j = m - 1; j >= 0; j--)
			{
				double v = std::max(at(i + 1, j), at(i, j + 1));
				const double s = sim[(size_t)(i * m + j)];
				if (s >= 0.7)
					v = std::max(v, at(i + 1, j + 1) + 1.0);				// as many pairs as possible; ties go to the first paragraphs
				at(i, j) = v;
			}
		int i = 0, j = 0;
		while (i < n && j < m)
		{
			const double s = sim[(size_t)(i * m + j)];
			if (s >= 0.7 && at(i, j) == at(i + 1, j + 1) + 1.0)
			{
				words(src[(size_t)(b.a0 + i)], dst[(size_t)(b.b0 + j)], b.a0 + i, b.b0 + j, r);
				i++; j++;
			}
			else if (at(i, j) == at(i + 1, j)) i++;
			else j++;
		}
	}

	Result compute(const std::vector<std::string>& src, const std::vector<std::string>& dst, const std::vector<std::pair<int, int>>& pins)
	{
		Result r;
		std::vector<Block> blocks;
		int pa = 0, pb = 0;
		for (const std::pair<int, int>& pin : pins)
		{
			if (pin.first < pa || pin.second < pb || pin.first >= (int)src.size() || pin.second >= (int)dst.size())
				continue;
			patience(src, dst, pa, pin.first, pb, pin.second, blocks);
			if (src[pin.first] != dst[pin.second])
				blocks.push_back({ pin.first, pin.first + 1, pin.second, pin.second + 1 });
			pa = pin.first + 1;
			pb = pin.second + 1;
		}
		patience(src, dst, pa, (int)src.size(), pb, (int)dst.size(), blocks);
		r.blocks = blocks;
		for (const Block& b : blocks)
		{
			const int da = b.a1 - b.a0, db = b.b1 - b.b0;
			if (da > 0 && db > 0)
			{
				r.changed += db;
				for (int k = 0; k < da; k++) r.src_marks.push_back({ b.a0 + k, Changed });
				for (int k = 0; k < db; k++) r.dst_marks.push_back({ b.b0 + k, Changed });
				if (da == db)
					for (int k = 0; k < da; k++)
						words(src[b.a0 + k], dst[b.b0 + k], b.a0 + k, b.b0 + k, r);
				else
					words_in_block(src, dst, b, r);
			}
			else if (da > 0)
			{
				r.removed += da;
				for (int k = 0; k < da; k++) r.src_marks.push_back({ b.a0 + k, Removed });
			}
			else
			{
				r.added += db;
				for (int k = 0; k < db; k++) r.dst_marks.push_back({ b.b0 + k, Added });
			}
		}
		return r;
	}


	static int map_para(const Result& r, int p, bool from_dst)
	{
		int offset = 0;
		for (const Block& b : r.blocks)
		{
			const int f0 = from_dst ? b.b0 : b.a0, f1 = from_dst ? b.b1 : b.a1, t0 = from_dst ? b.a0 : b.b0, t1 = from_dst ? b.a1 : b.b1;
			if (p >= f1)
			{
				offset += (t1 - t0) - (f1 - f0);
				continue;
			}
			if (p >= f0)
				return t1 > t0 ? t0 + std::min(p - f0, t1 - t0 - 1) : t0;			// inside a difference: the matching paragraph, or the next one if the paragraph was added
			break;
		}
		return std::max(0, p + offset);
	}

	int map_dst_to_src(const Result& r, int para) { return map_para(r, para, true); }
	int map_src_to_dst(const Result& r, int para) { return map_para(r, para, false); }

	std::vector<Line> lines(const std::string& old_text, const std::string& new_text, bool only_changes)
	{
		const std::vector<std::string> a = util::split_lines(old_text), b = util::split_lines(new_text);
		std::vector<Block> blocks;
		patience(a, b, 0, (int)a.size(), 0, (int)b.size(), blocks);
		std::vector<Line> out;
		int ia = 0;
		for (const Block& bl : blocks)
		{
			if (!only_changes)
				for (; ia < bl.a0; ia++)
					out.push_back({ ' ', a[ia] });
			ia = bl.a0;
			for (int k = bl.a0; k < bl.a1; k++) out.push_back({ '-', a[k] });
			for (int k = bl.b0; k < bl.b1; k++) out.push_back({ '+', b[k] });
			ia = bl.a1;
		}
		if (!only_changes)
			for (; ia < (int)a.size(); ia++)
				out.push_back({ ' ', a[ia] });
		return out;
	}
}

//-----------------------------------------------------------------------------
// Table of contents
//-----------------------------------------------------------------------------

namespace toc
{
	static std::string clean(const std::string& t)
	{
		std::string o;
		for (char c : t)
			if (c != '*' && c != '_' && c != '`')
				o += c;
		return o;
	}

	static std::string slug(const std::string& title, std::map<std::string, int>& used)
	{
		std::string s;
		for (char32_t c : to_u32(title))
		{
			if (c >= 'A' && c <= 'Z') c += 32;
			else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) c += 32;
			if (typography::is_letter(c) || typography::is_digit(c) || c == '-' || c == '_')
				put_utf8(s, c);
			else if (c == ' ')
				s += '-';
		}
		auto it = used.find(s);
		if (it != used.end())
			return s + "-" + std::to_string(it->second++);
		used[s] = 1;
		return s;
	}

	static std::string url_escape(const std::string& s)
	{
		static const char* hex = "0123456789ABCDEF";
		std::string o;
		for (unsigned char c : s)
		{
			if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
				o += (char)c;
			else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
		}
		return o;
	}

	std::string generate(const std::vector<Chapter>& chapters, bool markdown, bool multi)
	{
		std::string out;
		for (const Chapter& ch : chapters)
		{
			if (ch.headings.empty())
				continue;
			int min_level = 99;
			for (const Heading& h : ch.headings)
				min_level = std::min(min_level, h.level);
			std::map<std::string, int> used;
			if (multi)
			{
				if (markdown)
					out += "- **[" + fs::u8path(ch.rel).stem().u8string() + "](" + url_escape(ch.rel) + ")**\n";
				else
					out += ch.rel + "\n";
			}
			for (const Heading& h : ch.headings)
			{
				const std::string pad((size_t)((h.level - min_level) + (multi ? 1 : 0)) * 2, ' ');
				if (markdown)
				{
					const std::string title = clean(h.title);
					const std::string target = (multi ? url_escape(ch.rel) : std::string()) + "#" + slug(title, used);
					out += pad + "- [" + title + "](" + target + ")\n";
				}
				else
					out += pad + h.title + "  (l. " + std::to_string(h.line) + ")\n";
			}
		}
		return out;
	}
}

//-----------------------------------------------------------------------------
// History
//-----------------------------------------------------------------------------

// Folder of the snapshots of a document: one folder per document, named after a hash of its path
static std::string history_dir(const std::string& key)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)std::hash<std::string>()(key));
	return util::combine(util::combine(util::data_path(), "history"), buf);
}

// Snapshots are stored incrementally: <date>_<kind>.txt is a full text, <date>_<kind>.dlt is a line delta against the previous snapshot
// ("#<size>" then, for each changed block, "@<a0> <a1> <count>" followed by <count> new lines). A full text is kept every KeyEvery versions.
static const int KeyEvery = 20;

// Lines of a text, each with its line break
static std::vector<std::string> split_keep(const std::string& t)
{
	std::vector<std::string> v;
	size_t i = 0;
	while (i < t.size())
	{
		size_t e = t.find('\n', i);
		e = (e == std::string::npos) ? t.size() : e + 1;
		v.push_back(t.substr(i, e - i));
		i = e;
	}
	return v;
}

// Line delta of a new text against the previous one (see the format above)
static std::string make_delta(const std::string& old_text, const std::string& new_text)
{
	const std::vector<std::string> a = split_keep(old_text), b = split_keep(new_text);
	std::vector<diff::Block> blocks;
	diff::patience(a, b, 0, (int)a.size(), 0, (int)b.size(), blocks);
	std::string out = "#" + std::to_string(new_text.size()) + "\n";
	for (const diff::Block& bl : blocks)
	{
		out += "@" + std::to_string(bl.a0) + " " + std::to_string(bl.a1) + " " + std::to_string(bl.b1 - bl.b0) + "\n";
		for (int k = bl.b0; k < bl.b1; k++)
			out += b[k];
	}
	return out;
}

// Rebuilds the new text from the previous one and a delta
static std::string apply_delta(const std::string& old_text, const std::string& delta)
{
	const std::vector<std::string> a = split_keep(old_text);
	std::string out;
	size_t pos = delta.find('\n');
	pos = (pos == std::string::npos) ? delta.size() : pos + 1;
	size_t ia = 0;
	while (pos < delta.size())
	{
		size_t e = delta.find('\n', pos);
		if (e == std::string::npos) break;
		int a0 = 0, a1 = 0, n = 0;
		sscanf(delta.c_str() + pos + 1, "%d %d %d", &a0, &a1, &n);
		pos = e + 1;
		for (; ia < (size_t)a0 && ia < a.size(); ia++)
			out += a[ia];
		ia = (size_t)a1;
		for (int k = 0; k < n && pos < delta.size(); k++)
		{
			e = delta.find('\n', pos);
			e = (e == std::string::npos) ? delta.size() : e + 1;
			out.append(delta, pos, e - pos);
			pos = e;
		}
	}
	for (; ia < a.size(); ia++)
		out += a[ia];
	return out;
}

static bool is_delta(const std::string& path) { return path.size() > 4 && path.compare(path.size() - 4, 4, ".dlt") == 0; }

// Keeps a version of the text unless it is the same as the last one: a delta, or a full text every KeyEvery versions
bool History::add(const std::string& key, const std::string& text, const char* kind)
{
	const std::string dir = history_dir(key);
	std::vector<Snapshot> all = list(key);
	std::string previous;
	int deltas = 0;
	if (!all.empty())
	{
		previous = read(all[0]);
		if (previous == text)
			return false;
		for (const Snapshot& s : all)
		{
			if (!is_delta(s.path))
				break;
			deltas++;
		}
	}
	using namespace std::chrono;
	const auto now = system_clock::now();
	const std::time_t t = system_clock::to_time_t(now);
	std::tm tm;
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	char name[64];
	snprintf(name, sizeof(name), "%04d%02d%02d-%02d%02d%02d-%03d_%s", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
		(int)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000), kind);
	util::write_text(util::combine(dir, "path.txt"), key);
	std::string delta;
	if (!all.empty() && deltas < KeyEvery - 1)
		delta = make_delta(previous, text);
	const bool as_delta = !all.empty() && deltas < KeyEvery - 1 && delta.size() < text.size() / 2;
	if (!util::write_text(util::combine(dir, std::string(name) + (as_delta ? ".dlt" : ".txt")), as_delta ? delta : text))
		return false;
	all = list(key);
	for (size_t i = 300; i < all.size(); i++)
		remove(all[i]);
	return true;
}

// The versions of a document, most recent first
std::vector<History::Snapshot> History::list(const std::string& key)
{
	std::vector<Snapshot> all;
	std::error_code ec;
	const std::string dir = history_dir(key);
	for (const auto& e : fs::directory_iterator(fs::u8path(dir), ec))
	{
		const std::string name = e.path().filename().u8string();
		const size_t us = name.find('_');
		if (us != 19 || name.size() < 24 || name == "path.txt")
			continue;
		Snapshot s;
		s.path = e.path().u8string();
		s.size = (long long)e.file_size(ec);
		if (is_delta(s.path))
		{
			std::ifstream f(e.path(), std::ios::binary);
			std::string first;
			if (f && std::getline(f, first) && first.size() > 1 && first[0] == '#')
				s.size = atoll(first.c_str() + 1);
		}
		std::string kind = name.substr(us + 1);
		kind = kind.substr(0, kind.size() - 4);
		const char* kind_key = kind == "save" ? "HISTORY_KIND_SAVE" : kind == "auto" ? "HISTORY_KIND_AUTO" : (kind == "manual" || kind == "manuel") ? "HISTORY_KIND_MANUAL"
			: kind == "replace" ? "HISTORY_KIND_REPLACE" : kind == "script" ? "HISTORY_KIND_SCRIPT" : nullptr;
		s.label = name.substr(0, 4) + "-" + name.substr(4, 2) + "-" + name.substr(6, 2) + " " + name.substr(9, 2) + ":" + name.substr(11, 2) + ":" + name.substr(13, 2)
			+ "  -  " + (kind_key ? std::string(TR(kind_key)) : kind) + "  -  " + std::to_string((s.size + 512) / 1024) + " " + TR("HISTORY_KB");
		all.push_back(s);
	}
	std::sort(all.begin(), all.end(), [](const Snapshot& x, const Snapshot& y) { return fs::u8path(x.path).filename().u8string() > fs::u8path(y.path).filename().u8string(); });
	return all;
}

// Snapshots of the folder of s, newest first
static std::vector<History::Snapshot> list_beside(const History::Snapshot& s)
{
	std::vector<History::Snapshot> all;
	std::error_code ec;
	const fs::path dir = fs::u8path(s.path).parent_path();
	for (const auto& e : fs::directory_iterator(dir, ec))
	{
		const std::string name = e.path().filename().u8string();
		if (name.find('_') != 19 || name.size() < 24 || name == "path.txt")
			continue;
		History::Snapshot x;
		x.path = e.path().u8string();
		all.push_back(x);
	}
	std::sort(all.begin(), all.end(), [](const History::Snapshot& x, const History::Snapshot& y) { return fs::u8path(x.path).filename().u8string() > fs::u8path(y.path).filename().u8string(); });
	return all;
}

// The full text of a version (the deltas are applied from the nearest older full text)
std::string History::read(const Snapshot& s)
{
	std::string t;
	if (!is_delta(s.path))
	{
		util::read_text(s.path, t);
		return t;
	}
	const std::vector<Snapshot> all = list_beside(s);
	size_t i = 0;
	while (i < all.size() && all[i].path != s.path)
		i++;
	size_t base = i;
	while (base < all.size() && is_delta(all[base].path))
		base++;
	if (base >= all.size())
		return t;
	util::read_text(all[base].path, t);
	for (size_t k = base; k-- > i;)
	{
		std::string d;
		util::read_text(all[k].path, d);
		t = apply_delta(t, d);
	}
	return t;
}

// Deletes a version; the newer one is first stored in full when it was a delta based on this one
void History::remove(const Snapshot& s)
{
	// The next (newer) snapshot may be a delta based on this one: store it in full first
	const std::vector<Snapshot> all = list_beside(s);
	for (size_t i = 1; i < all.size(); i++)
		if (all[i].path == s.path && is_delta(all[i - 1].path))
		{
			const std::string text = read(all[i - 1]);
			const std::string full = all[i - 1].path.substr(0, all[i - 1].path.size() - 4) + ".txt";
			if (util::write_text(full, text))
			{
				std::error_code ec;
				fs::remove(fs::u8path(all[i - 1].path), ec);
			}
			break;
		}
	std::error_code ec;
	fs::remove(fs::u8path(s.path), ec);
}
