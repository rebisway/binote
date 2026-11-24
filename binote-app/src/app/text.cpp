// Binote - French typography, diff, table of contents, history
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "text.h"
#include "util.h"
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

static void put_utf8(std::string& o, char32_t c)
{
	if (c < 0x80) o += (char)c;
	else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
	else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
	else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
}

static std::string to_utf8(const std::u32string& s)
{
	std::string o;
	for (char32_t c : s)
		put_utf8(o, c);
	return o;
}

//-----------------------------------------------------------------------------
// Typography
//-----------------------------------------------------------------------------

namespace typography
{
	static const char32_t C_NBSP = 0x00A0, C_NNBSP = 0x202F;

	static bool is_letter(char32_t c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || c == 0x153 || c == 0x152;
	}
	static bool is_digit(char32_t c)	{ return c >= '0' && c <= '9'; }
	static bool is_space(char32_t c)	{ return c == ' ' || c == '\t' || c == C_NBSP || c == C_NNBSP; }

	static int g_quote_style = 0;
	void set_quote_style(int style) { g_quote_style = style; }

	static std::u32string fix_segment(const std::u32string& in)
	{
		const size_t n = in.size();
		std::u32string s;
		// Ellipsis, apostrophe, dash
		for (size_t i = 0; i < n; i++)
		{
			const char32_t c = in[i];
			if (c == '.' && i + 2 < n + 0 && in[i + 1] == '.' && in[i + 2] == '.' && (i + 3 >= n || in[i + 3] != '.') && (i == 0 || in[i - 1] != '.'))
			{
				s += U'…';
				i += 2;
			}
			else if (c == '\'' && i > 0 && i + 1 < n && is_letter(in[i - 1]) && is_letter(in[i + 1]))
				s += U'’';
			else if (c == '-' && i > 0 && i + 2 < n && in[i + 1] == '-' && std::isspace((int)in[i - 1] & 0x7F) && std::isspace((int)in[i + 2] & 0x7F) && in[i - 1] < 0x80 && in[i + 2] < 0x80)
			{
				s += U'–';
				i += 1;
			}
			else
				s += c;
		}
		// Straight quotes become « and » (or curly quotes, or stay as they are)
		for (size_t i = 0; i < s.size() && g_quote_style != 1; i++)
			if (s[i] == '"')
			{
				const char32_t prev = i > 0 ? s[i - 1] : U' ';
				const bool open = i == 0 || prev == ' ' || prev == '\t' || prev == '(' || prev == '[' || prev == U'–' || prev == U'—' || prev == U'«' || prev == U'\u201c';
				if (g_quote_style == 2)
					s[i] = open ? U'\u201c' : U'\u201d';
				else
					s[i] = open ? U'«' : U'»';
			}
		// Spaces inside « »
		std::u32string t;
		for (size_t i = 0; i < s.size(); i++)
		{
			const char32_t c = s[i];
			if (c == U'«')
			{
				t += c;
				size_t j = i + 1;
				while (j < s.size() && is_space(s[j]))
					j++;
				if (j < s.size())
					t += C_NNBSP;
				i = j - 1;
			}
			else if (is_space(c))
			{
				size_t j = i;
				while (j < s.size() && is_space(s[j]))
					j++;
				if (j < s.size() && s[j] == U'»')
				{
					t += C_NNBSP;
					t += U'»';
					i = j;
				}
				else
				{
					t.append(s, i, j - i);
					i = j - 1;
				}
			}
			else if (c == U'»')
			{
				if (t.empty() || t.back() != C_NNBSP)
					t += C_NNBSP;
				t += c;
			}
			else
				t += c;
		}
		// Before ; ! ? :
		std::u32string o;
		for (size_t i = 0; i < t.size(); i++)
		{
			const char32_t c = t[i];
			if (c == ';' || c == '!' || c == '?' || c == ':')
			{
				size_t k = o.size();
				while (k > 0 && is_space(o[k - 1]))
					k--;
				const bool prev_ok = k > 0 && (is_letter(o[k - 1]) || is_digit(o[k - 1]) || o[k - 1] == ')' || o[k - 1] == ']' || o[k - 1] == U'»'
					|| o[k - 1] == U'…' || o[k - 1] == U'’' || o[k - 1] == '*' || o[k - 1] == '_' || o[k - 1] == '"');
				const char32_t next = i + 1 < t.size() ? t[i + 1] : U' ';
				const bool next_ok = i + 1 >= t.size() || is_space(next) || (c != ':' && (next == ')' || next == '"' || next == U'»' || next == '*' || next == '_' || next == ']'));
				if (prev_ok && next_ok)
				{
					o.resize(k);
					o += (c == ':') ? C_NBSP : C_NNBSP;
				}
			}
			o += c;
		}
		return o;
	}

	std::string fix_line(const std::string& line, bool markdown)
	{
		bool blank = true;
		for (char c : line)
			if (c != ' ' && c != '\t')
				blank = false;
		if (blank)
			return line;
		if (!markdown)
			return to_utf8(fix_segment(to_u32(line)));
		// Code spans stay as they are
		std::string out, current;
		bool in_code = false;
		for (char c : line)
		{
			if (c == '`')
			{
				if (!in_code)
					out += to_utf8(fix_segment(to_u32(current)));
				else
					out += current;
				out += '`';
				current.clear();
				in_code = !in_code;
			}
			else
				current += c;
		}
		out += in_code ? current : to_utf8(fix_segment(to_u32(current)));
		return out;
	}

	std::string fix_text(const std::string& text, bool markdown)
	{
		std::string out;
		bool in_fence = false;
		size_t pos = 0;
		while (pos <= text.size())
		{
			size_t eol = text.find('\n', pos);
			if (eol == std::string::npos)
				eol = text.size();
			const std::string line = text.substr(pos, eol - pos);
			size_t a = line.find_first_not_of(" \t");
			const bool fence = markdown && a != std::string::npos && (line.compare(a, 3, "```") == 0 || line.compare(a, 3, "~~~") == 0);
			if (fence)
			{
				in_fence = !in_fence;
				out += line;
			}
			else
				out += in_fence ? line : fix_line(line, markdown);
			if (eol < text.size())
				out += '\n';
			pos = eol + 1;
		}
		return out;
	}

	Typed on_typed(const std::string& typed, const std::string& before)
	{
		Typed r;
		if (typed.size() != 1 || before.empty())
			return r;
		const char c = typed[0];
		// The last three code points of 'before' with their byte positions
		struct Cp { char32_t cp; int pos; };
		std::vector<Cp> cps;
		for (int i = (int)before.size(); i > 0 && cps.size() < 4;)
		{
			int j = i - 1;
			while (j > 0 && ((unsigned char)before[j] & 0xC0) == 0x80)
				j--;
			cps.push_back({ to_u32(before.substr(j, i - j))[0], j });
			i = j;
		}
		const int end = (int)before.size();
		auto at = [&](size_t k) -> Cp { return k < cps.size() ? cps[k] : Cp{ 0, -1 }; };		// 0 = typed, 1 = before it...
		const Cp prev = at(1);

		switch (c)
		{
		case '"':
		{
			if (g_quote_style == 1)
				return r;										// straight quotes: nothing to replace
			const bool open = prev.pos < 0 || prev.cp == ' ' || prev.cp == '\t' || prev.cp == '(' || prev.cp == '[' || prev.cp == U'–' || prev.cp == U'—';
			r.valid = true;
			if (g_quote_style == 2)
			{
				r.start = end - 1; r.length = 1; r.text = open ? "\xE2\x80\x9C" : "\xE2\x80\x9D";
				return r;
			}
			if (open)
			{
				r.start = end - 1; r.length = 1; r.text = std::string("\xC2\xAB") + NNBSP;
			}
			else if (prev.cp == ' ' || prev.cp == C_NBSP || prev.cp == C_NNBSP)
			{
				r.start = prev.pos; r.length = end - prev.pos; r.text = std::string(NNBSP) + "\xC2\xBB";
			}
			else
			{
				r.start = end - 1; r.length = 1; r.text = std::string(NNBSP) + "\xC2\xBB";
			}
			return r;
		}
		case '\'':
			r.valid = true; r.start = end - 1; r.length = 1; r.text = "\xE2\x80\x99";
			return r;
		case '.':
			if (end >= 3 && before.compare(end - 3, 3, "...") == 0 && (end < 4 || before[end - 4] != '.'))
			{
				r.valid = true; r.start = end - 3; r.length = 3; r.text = "\xE2\x80\xA6";
			}
			return r;
		case ' ':
		{
			// A space typed after "x;" "x!" "x?" "x:" : the no-break space goes before the sign
			const Cp p = at(1), x = at(2);
			if (p.pos >= 0 && x.pos >= 0 && (p.cp == ';' || p.cp == '!' || p.cp == '?' || p.cp == ':'))
			{
				const std::string sp = p.cp == ':' ? NBSP : NNBSP;
				const std::string sign(1, (char)p.cp);
				if (is_letter(x.cp) || is_digit(x.cp) || x.cp == ')' || x.cp == ']' || x.cp == U'»' || x.cp == U'…' || x.cp == U'’')
				{
					r.valid = true; r.start = p.pos; r.length = 1; r.text = sp + sign;
				}
				else if (x.cp == ' ')
				{
					r.valid = true; r.start = x.pos; r.length = 2; r.text = sp + sign;
				}
			}
			return r;
		}
		}
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

static std::string history_dir(const std::string& key)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)std::hash<std::string>()(key));
	return util::combine(util::combine(util::data_path(), "history"), buf);
}

// Snapshots are stored incrementally: <date>_<kind>.txt is a full text, <date>_<kind>.dlt is a line delta against the previous snapshot
// ("#<size>" then, for each changed block, "@<a0> <a1> <count>" followed by <count> new lines). A full text is kept every KeyEvery versions.
static const int KeyEvery = 20;

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
		s.label = name.substr(6, 2) + "/" + name.substr(4, 2) + "/" + name.substr(0, 4) + " " + name.substr(9, 2) + ":" + name.substr(11, 2) + ":" + name.substr(13, 2)
			+ "  -  " + (kind == "save" ? "enregistrement" : kind == "auto" ? "auto" : kind) + "  -  " + std::to_string((s.size + 512) / 1024) + " Ko";
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
