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

// Binote - grammar checker

#include "analysis/grammar.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace grammar
{
	//-------------------------------------------------------------------------
	// Text helpers (UTF-8)
	//-------------------------------------------------------------------------

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

	static bool is_letter(unsigned int c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || c == 0x153 || c == 0x152;
	}

	static bool is_upper_cp(unsigned int c)
	{
		return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || c == 0x152;
	}

	static bool is_lower_cp(unsigned int c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) || c == 0x153;
	}

	// Lower case of the Latin letters, the typographic apostrophe becomes the straight one
	static std::string lower(const std::string& s)
	{
		std::string o;
		for (size_t i = 0; i < s.size();)
		{
			int n;
			unsigned int c = decode(s, i, &n);
			i += n;
			if (c == 0x2019)
				c = '\'';
			else if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
				c += 32;
			else if (c == 0x152)
				c = 0x153;
			if (c < 0x80) o += (char)c;
			else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
			else { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
		}
		return o;
	}

	// First code point of a text
	static unsigned int first_cp(const std::string& s)
	{
		if (s.empty())
			return 0;
		int n;
		return decode(s, 0, &n);
	}

	// The replacement written like the original: a capital letter at the start if the original has one
	static std::string match_case(const std::string& original, const std::string& replacement)
	{
		if (!is_upper_cp(first_cp(original)) || replacement.empty())
			return replacement;
		int n;
		unsigned int c = decode(replacement, 0, &n);
		if (c >= 'a' && c <= 'z')
			c -= 32;
		else if (c >= 0xE0 && c <= 0xFE && c != 0xF7)
			c -= 32;
		else
			return replacement;
		std::string o;
		if (c < 0x80) o += (char)c;
		else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
		return o + replacement.substr((size_t)n);
	}

	// True when the text ends with the suffix
	static bool ends_with(const std::string& s, const char* suffix)
	{
		const size_t n = strlen(suffix);
		return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
	}

	// True when the word is in a null-terminated list of words
	static bool in_list(const std::string& w, const char* const* list)
	{
		for (; *list; list++)
			if (w == *list)
				return true;
		return false;
	}

	//-------------------------------------------------------------------------
	// Tokens
	//-------------------------------------------------------------------------

	struct Token
	{
		enum Kind { Word, Number, Punct } kind = Word;
		std::string text, low;
		int start = 0, end = 0;
		bool space_before = false;
	};

	// Cuts a paragraph into words, numbers and punctuation, with their byte range and the space before them.
	// In French an apostrophe ends the word (l', qu', j') so the article is a token of its own.
	static std::vector<Token> tokenize(const std::string& text, bool french)
	{
		static const char* const elisions[] = { "l'", "d'", "j'", "n'", "s'", "c'", "m'", "t'", "qu'", "jusqu'", "lorsqu'", "puisqu'", "quoiqu'", nullptr };
		std::vector<Token> tokens;
		bool space = true;
		for (size_t i = 0; i < text.size();)
		{
			int n;
			const unsigned int c = decode(text, i, &n);
			if (c == ' ' || c == '\t' || c == 0xA0 || c == 0x202F)
			{
				space = true;
				i += n;
				continue;
			}
			Token t;
			t.start = (int)i;
			t.space_before = space;
			space = false;
			if (is_letter(c))
			{
				size_t j = i;
				while (j < text.size())
				{
					int m;
					const unsigned int d = decode(text, j, &m);
					if (is_letter(d))
						j += m;
					else if ((d == '\'' || d == 0x2019 || d == '-') && j + m < text.size())
					{
						int m2;
						if (is_letter(decode(text, j + m, &m2)))
							j += m;
						else
							break;
					}
					else
						break;
				}
				t.kind = Token::Word;
				t.text = text.substr(i, j - i);
				t.low = lower(t.text);
				t.end = (int)j;
				i = j;
				// French elision: "l'homme" is "l'" and "homme"
				const size_t ap = t.low.find('\'');
				if (french && ap != std::string::npos && ap + 1 < t.low.size())
				{
					const std::string prefix = t.low.substr(0, ap + 1);
					if (in_list(prefix, elisions))
					{
						size_t cut = t.text.find('\'');
						if (cut == std::string::npos)
							cut = t.text.find("\xE2\x80\x99");
						const size_t cut_len = t.text[cut] == '\'' ? 1 : 3;
						Token a = t;
						a.text = t.text.substr(0, cut + cut_len);
						a.low = prefix;
						a.end = t.start + (int)(cut + cut_len);
						Token b = t;
						b.text = t.text.substr(cut + cut_len);
						b.low = lower(b.text);
						b.start = a.end;
						b.space_before = false;
						tokens.push_back(a);
						tokens.push_back(b);
						continue;
					}
				}
				tokens.push_back(t);
				continue;
			}
			if (c >= '0' && c <= '9')
			{
				size_t j = i;
				while (j < text.size() && ((text[j] >= '0' && text[j] <= '9') || ((text[j] == '.' || text[j] == ',') && j + 1 < text.size() && text[j + 1] >= '0' && text[j + 1] <= '9')))
					j++;
				t.kind = Token::Number;
				t.text = text.substr(i, j - i);
				t.low = t.text;
				t.end = (int)j;
				i = j;
				tokens.push_back(t);
				continue;
			}
			// Punctuation: three dots are an ellipsis
			t.kind = Token::Punct;
			if (c == '.' && text.compare(i, 3, "...") == 0)
			{
				t.text = "\xE2\x80\xA6";
				t.end = (int)i + 3;
				i += 3;
			}
			else
			{
				t.text = text.substr(i, (size_t)n);
				t.end = (int)i + n;
				i += n;
			}
			t.low = t.text;
			tokens.push_back(t);
		}
		return tokens;
	}

	struct Ctx
	{
		const std::vector<Token>& t;
		const std::string& text;
		const std::function<bool(const std::string&)>& known;
		std::vector<Issue>& out;
		const lexicon::Lexicon* lex = nullptr;
		const Token* at(size_t i) const { return i < t.size() ? &t[i] : nullptr; }
		bool word(size_t i) const { return i < t.size() && t[i].kind == Token::Word; }
		void add(const char* rule, const Token& a, const Token& b, float confidence, const std::string& suggestion, const std::string& arg = "") const
		{
			Issue is;
			is.rule = rule;
			is.start = a.start;
			is.end = b.end;
			is.confidence = confidence;
			is.arg = arg;
			is.suggestions.push_back(suggestion);
			out.push_back(is);
		}
	};

	//-------------------------------------------------------------------------
	// Rules
	//-------------------------------------------------------------------------

	// Any language: a word written twice in a row
	static void rule_repeat(const Ctx& c)
	{
		static const char* const allowed[] = { "nous", "vous", "bien", "pas", "non", "si", "tout", "très", "had", "that", "ha", "no", "bye", "so", "very", "yo", "knock", "blah", nullptr };
		for (size_t i = 1; i < c.t.size(); i++)
			if (c.word(i) && c.word(i - 1) && c.t[i].space_before && c.t[i].text == c.t[i - 1].text && c.t[i].low.size() >= 2 && !in_list(c.t[i].low, allowed))
			{
				Issue is;
				is.rule = "any.repeat";
				is.start = c.t[i - 1].end;
				is.end = c.t[i].end;
				is.arg = c.t[i].text;
				is.confidence = 0.85f;
				is.suggestions.push_back("");
				c.out.push_back(is);
			}
	}

	// Any language: a sentence starts with a lower case letter
	static void rule_capital(const Ctx& c)
	{
		static const char* const abbreviations[] = { "etc", "cf", "vs", "mme", "mlle", "dr", "st", "ste", "av", "apr", "env", "ex", "fig", "mr", "mrs", "ms", "no", "ibid", "approx", "vol", "ch", "chap", "min", "max", nullptr };
		for (size_t i = 1; i + 1 < c.t.size(); i++)
		{
			const Token& p = c.t[i];
			if (p.kind != Token::Punct || (p.text != "." && p.text != "!" && p.text != "?") || !c.word(i + 1) || !c.t[i + 1].space_before || p.space_before)
				continue;
			const Token& before = c.t[i - 1];
			if (before.kind == Token::Word && p.text == "." && before.text.size() <= 4 && is_upper_cp(first_cp(before.text)))
				continue;							// "Abm.", "Dr.", "St.": abbreviations
			if (before.kind == Token::Number || (before.kind == Token::Word && (before.low.size() < 2 || (p.text == "." && in_list(before.low, abbreviations)))))
				continue;
			if (before.kind == Token::Punct && before.text != "\xE2\x80\xA6" && before.text != ")" && before.text != "\"")
				continue;
			// An exclamation or a question which is one word, or starts with an interjection, does not end the sentence:
			// "Thump! thump! down she came", "alas for poor Alice! when she got there"
			if (p.text != ".")
			{
				size_t start = i;
				while (start > 0 && !(c.t[start - 1].kind == Token::Punct && (c.t[start - 1].text == "." || c.t[start - 1].text == "!" || c.t[start - 1].text == "?")))
					start--;
				static const char* const interjections[] = { "alas", "oh", "ah", "hurrah", "hush", "hey", "well", "hélas", nullptr };
				bool interjection = false;
				for (size_t k = std::max(start, i > 8 ? i - 8 : 0); k < i; k++)
					if (c.word(k) && in_list(c.t[k].low, interjections))
						interjection = true;
				if (i - start == 1 || interjection || (c.word(i - 1) && c.t[i - 1].low == c.t[i + 1].low))
					continue;
			}
			const Token& w = c.t[i + 1];
			if (is_lower_cp(first_cp(w.text)))
				c.add("any.capital", w, w, 0.7f, match_case("A", w.text));
		}
	}

	// The word starts a clause: it is the first of the paragraph, comes after a punctuation mark or after a conjunction
	static bool opens_clause(const Ctx& c, size_t i)
	{
		static const char* const starters[] = { "et", "ou", "mais", "que", "qu'", "si", "s'", "quand", "lorsque", "lorsqu'", "comme", "puisque", "puisqu'", "car", "donc", "alors", "o\xC3\xB9", "dont", "pendant", "ni", nullptr };
		return i == 0 || c.t[i - 1].kind == Token::Punct || (c.t[i - 1].kind == Token::Word && in_list(c.t[i - 1].low, starters));
	}

	// French: "il à" -> "il a"
	static void rule_fr_a(const Ctx& c)
	{
		for (size_t i = 1; i < c.t.size(); i++)
			if (c.word(i) && c.word(i - 1) && c.t[i].low == "\xC3\xA0" && (c.t[i - 1].low == "il" || c.t[i - 1].low == "elle" || c.t[i - 1].low == "on") && opens_clause(c, i - 1))
				c.add("fr.a", c.t[i], c.t[i], 0.9f, match_case(c.t[i].text, "a"));
	}

	// French: "ils on" -> "ils ont", "ils son" -> "ils sont"
	static void rule_fr_ont_sont(const Ctx& c)
	{
		for (size_t i = 1; i < c.t.size(); i++)
		{
			if (!c.word(i) || !c.word(i - 1))
				continue;
			const std::string& p = c.t[i - 1].low;
			if (p != "ils" && p != "elles" && p != "ceux" && p != "celles")
				continue;
			if (c.t[i].low == "on" && (p == "ils" || p == "elles"))
				c.add("fr.ont", c.t[i], c.t[i], 0.9f, match_case(c.t[i].text, "ont"));
			else if (c.t[i].low == "son")
				c.add("fr.sont", c.t[i], c.t[i], 0.9f, match_case(c.t[i].text, "sont"));
		}
	}

	// Forms of avoir and être which make a compound tense
	static bool is_auxiliary_fr(const std::string& w)
	{
		static const char* const aux[] = { "ai", "as", "a", "avons", "avez", "ont", "avais", "avait", "avions", "aviez", "avaient", "aurai", "auras", "aura", "aurons", "aurez", "auront", "aurais", "aurait", "aurions",
			"auriez", "auraient", "ayant", "est", "sont", "\xC3\xA9tait", "\xC3\xA9taient", "sera", "seront", "serait", "seraient", "suis", "es", "sommes", "\xC3\xAAtes", "\xC3\xA9tais", "fut", nullptr };
		return in_list(w, aux);
	}

	// French: after an auxiliary a verb ending in "er" is a participle ("il a manger" -> "mangé")
	static void rule_fr_participle(const Ctx& c)
	{
		if (!c.known)
			return;
		for (size_t i = 1; i < c.t.size(); i++)
		{
			if (!c.word(i) || !c.word(i - 1) || !is_auxiliary_fr(c.t[i - 1].low))
				continue;
			// "c'est continuer", "c'\xC3\xA9tait regarder": the verb of being then takes an infinitive; only after a subject pronoun is it an auxiliary
			static const char* const subjects[] = { "il", "elle", "on", "ils", "elles", "je", "j'", "tu", "nous", "vous", nullptr };
			const std::string& aux = c.t[i - 1].low;
			const bool avoir = aux == "a" || aux == "ai" || aux == "as" || aux == "avons" || aux == "avez" || aux == "ont" || aux.compare(0, 3, "avi") == 0 || aux.compare(0, 4, "avai") == 0 || aux.compare(0, 4, "aura") == 0 || aux == "ayant";
			if (!avoir && !(i >= 2 && c.t[i - 2].kind == Token::Word && in_list(c.t[i - 2].low, subjects)))
				continue;
			const std::string& w = c.t[i].low;
			if (w.size() < 5 || !ends_with(w, "er"))
				continue;
			const std::string base = w.substr(0, w.size() - 2) + "\xC3\xA9";
			if (c.known(base) && c.known(c.t[i].text))
				c.add("fr.participle", c.t[i], c.t[i], 0.8f, match_case(c.t[i].text, base), base);
		}
	}

	// French: after "pour", "sans" or a modal verb a participle is an infinitive ("pour mangé" -> "manger")
	static void rule_fr_infinitive(const Ctx& c)
	{
		static const char* const triggers[] = { "pour", "sans", "faut", "peut", "peuvent", "veut", "veulent", "doit", "doivent", "dois", "peux", "veux", "pouvoir", "vouloir", "devoir", "va", "vais", "vas", "vont", "allons", "allez", nullptr };
		static const char* const excluded[] = { "\xC3\xA9t\xC3\xA9", "pass\xC3\xA9", "\xC3\xA9t\xC3\xA9s", "c\xC3\xB4t\xC3\xA9", "degr\xC3\xA9", nullptr };
		if (!c.known)
			return;
		for (size_t i = 1; i < c.t.size(); i++)
		{
			if (!c.word(i) || !c.word(i - 1) || !in_list(c.t[i - 1].low, triggers))
				continue;
			const std::string& w = c.t[i].low;
			if (w.size() < 5 || !ends_with(w, "\xC3\xA9") || in_list(w, excluded))
				continue;
			const std::string base = w.substr(0, w.size() - 2) + "er";
			if (c.known(base))
				c.add("fr.infinitive", c.t[i], c.t[i], 0.7f, match_case(c.t[i].text, base), base);
		}
	}

	// English: "a apple" -> "an apple", "an dog" -> "a dog"
	static void rule_en_a_an(const Ctx& c)
	{
		static const char* const vowel_sound_not[] = { "one", "once", "unit", "units", "university", "uniform", "union", "unique", "useful", "user", "use", "used", "usual", "utility", "european", "eulogy", "ubiquitous", "unicorn", "unified", "universal", nullptr };
		static const char* const silent_h[] = { "hour", "hours", "honest", "honor", "honour", "heir", "hourly", "honorable", "honourable", nullptr };
		for (size_t i = 0; i + 1 < c.t.size(); i++)
		{
			if (!c.word(i) || !c.word(i + 1))
				continue;
			const std::string& a = c.t[i].low;
			const std::string& w = c.t[i + 1].low;
			if ((a != "a" && a != "an") || w.empty())
				continue;
			const bool vowel = strchr("aeiou", w[0]) != nullptr;
			if (c.t[i + 1].text.size() > 1 && std::all_of(c.t[i + 1].text.begin(), c.t[i + 1].text.end(), [](unsigned char ch) { return ch >= 'A' && ch <= 'Z'; }))
				continue;							// acronyms: the sound of the letters decides
			if (a == "a" && vowel && !in_list(w, vowel_sound_not))
				c.add("en.a_an", c.t[i], c.t[i], 0.75f, match_case(c.t[i].text, "an"), c.t[i + 1].text);
			else if (a == "an" && !vowel && !in_list(w, silent_h) && (unsigned char)w[0] < 0x80)
				c.add("en.a_an", c.t[i], c.t[i], 0.75f, match_case(c.t[i].text, "a"), c.t[i + 1].text);
		}
	}

	// English: "better then" -> "better than"
	static void rule_en_then(const Ctx& c)
	{
		static const char* const comparatives[] = { "more", "less", "better", "worse", "bigger", "smaller", "larger", "higher", "lower", "faster", "slower", "greater", "rather", "other", "older", "younger", "easier", "harder", "stronger", "longer", "taller", nullptr };
		for (size_t i = 1; i < c.t.size(); i++)
			if (c.word(i) && c.word(i - 1) && c.t[i].low == "then" && in_list(c.t[i - 1].low, comparatives))
				c.add("en.then_than", c.t[i], c.t[i], 0.8f, match_case(c.t[i].text, "than"));
	}

	// English: the pronoun "i" is always a capital letter
	static void rule_en_i(const Ctx& c)
	{
		for (size_t i = 0; i < c.t.size(); i++)
			if (c.word(i) && c.t[i].text == "i")
				c.add("en.i", c.t[i], c.t[i], 0.9f, "I");
	}

	// Spanish: a question or an exclamation which does not open with its inverted mark
	static void rule_es_inverted(const Ctx& c)
	{
		size_t start = 0;
		bool opened = false;
		for (size_t i = 0; i < c.t.size(); i++)
		{
			const Token& t = c.t[i];
			if (t.kind != Token::Punct)
				continue;
			if (t.text == "\xC2\xBF" || t.text == "\xC2\xA1")
				opened = true;
			else if (t.text == "?" || t.text == "!")
			{
				if (!opened && i > start && c.word(start))
					c.add("es.inverted", c.t[start], c.t[start], 0.85f, (t.text == "?" ? "\xC2\xBF" : "\xC2\xA1") + c.t[start].text, t.text == "?" ? "\xC2\xBF" : "\xC2\xA1");
				start = i + 1;
				opened = false;
			}
			else if (t.text == ".")
			{
				start = i + 1;
				opened = false;
			}
		}
	}

	//-------------------------------------------------------------------------
	// French agreement, with the lexicon
	//-------------------------------------------------------------------------

	typedef std::vector<lexicon::Analysis> Analyses;

	// A word which is next to the previous one, without a punctuation mark between, and not a proper name
	static bool plain_word(const Ctx& c, size_t i)
	{
		return c.word(i) && !(is_upper_cp(first_cp(c.t[i].text)) && i > 0);
	}

	// True when a word starts with a vowel or a mute h (elision, a / an)
	static bool starts_with_vowel(const std::string& low)
	{
		static const char* const vowels[] = { "a", "e", "i", "o", "u", "y", "h", "\xC3\xA0", "\xC3\xA2", "\xC3\xA9", "\xC3\xA8", "\xC3\xAA", "\xC3\xAE", "\xC3\xB4", "\xC3\xBB", nullptr };
		for (const char* const* v = vowels; *v; v++)
			if (low.compare(0, strlen(*v), *v) == 0)
				return true;
		return false;
	}

	// The word is a noun and nothing else; gender and number are the same in all its readings (0 if they differ)
	static bool noun_only(const Analyses& an, int* gender, int* number)
	{
		if (an.empty())
			return false;
		*gender = an[0].gender;
		*number = an[0].number;
		for (const lexicon::Analysis& a : an)
		{
			if (a.pos != lexicon::Noun)
				return false;
			if (a.gender != *gender) *gender = 0;
			if (a.number != *number) *number = 0;
		}
		return true;
	}

	// Determiner and noun: "les chat" -> "les chats", "un voiture" -> "une voiture"
	static void rule_fr_determiner(const Ctx& c)
	{
		static const char* const plural_dets[] = { "les", "des", "ces", "mes", "tes", "ses", "nos", "vos", "leurs", "aux", "quelques", "plusieurs", nullptr };
		static const char* const singular_dets[] = { "le", "la", "un", "une", "ce", "cet", "cette", "mon", "ma", "ton", "ta", "son", "sa", "notre", "votre", "du", "au", nullptr };
		static const char* const masculine[] = { "le", "un", "ce", "cet", "mon", "ton", "son", "du", "au", nullptr };
		static const char* const feminine[] = { "la", "une", "cette", "ma", "ta", "sa", nullptr };
		Analyses an;
		for (size_t i = 0; i + 1 < c.t.size(); i++)
		{
			if (!c.word(i) || !plain_word(c, i + 1) || c.t[i + 1].space_before == false)
				continue;
			const std::string& det = c.t[i].low;
			const bool plural = in_list(det, plural_dets), singular = in_list(det, singular_dets);
			if (!plural && !singular)
				continue;
			if ((det == "un" || det == "une") && i > 0 && (c.t[i - 1].low == "et" || c.t[i - 1].kind == Token::Number))
				continue;							// "trente et un ans": a number
			c.lex->analyses(c.t[i + 1].text, an);
			int gender, number;
			if (!noun_only(an, &gender, &number))
				continue;
			const Token& noun = c.t[i + 1];
			if (plural && number == lexicon::Singular)
			{
				const std::string form = c.lex->inflect(an[0], 0, lexicon::Plural, 0, 0);
				if (!form.empty() && form != noun.low)
					c.add("fr.det_number", noun, noun, 0.8f, match_case(noun.text, form), form);
			}
			else if (singular && number == lexicon::Plural && det != "du" && det != "au")
			{
				const std::string form = c.lex->inflect(an[0], 0, lexicon::Singular, 0, 0);
				if (!form.empty() && form != noun.low)
					c.add("fr.det_number", noun, noun, 0.8f, match_case(noun.text, form), form);
			}
			else if (singular && number != lexicon::Invariable && gender != 0 && gender != lexicon::EpiceneGender && !starts_with_vowel(noun.low))
			{
				std::string other;
				if (gender == lexicon::Feminine && in_list(det, masculine))
					other = det == "le" ? "la" : det == "un" ? "une" : (det == "ce" || det == "cet") ? "cette" : det == "mon" ? "ma" : det == "ton" ? "ta" : det == "son" ? "sa" : "";
				else if (gender == lexicon::Masculine && in_list(det, feminine))
					other = det == "la" ? "le" : det == "une" ? "un" : det == "cette" ? "ce" : det == "ma" ? "mon" : det == "ta" ? "ton" : "son";
				if (!other.empty())
					c.add("fr.det_gender", c.t[i], c.t[i], 0.75f, match_case(c.t[i].text, other), other);
			}
		}
	}

	// Noun and adjective: "des chats noir" -> "des chats noirs", "une voiture noir" -> "une voiture noire"
	static void rule_fr_adjective(const Ctx& c)
	{
		static const char* const colors[] = { "bleu", "vert", "rouge", "jaune", "rose", "marron", "orange", "noir", "blanc", "gris", "violet", "bordeaux", "turquoise", nullptr };
		Analyses noun_an, adj_an;
		for (size_t i = 0; i + 1 < c.t.size(); i++)
		{
			if (!c.word(i) || !plain_word(c, i + 1) || !c.t[i + 1].space_before)
				continue;
			// "avoir l'air fatiguée": the adjective agrees with the subject
			if (c.t[i].low == "air")
				continue;
			// "une carte de vœux manuscrite": after "de" the adjective may agree with the first noun
			if (i > 0 && (c.t[i - 1].low == "de" || c.t[i - 1].low == "d'"))
				continue;
			c.lex->analyses(c.t[i].text, noun_an);
			int gender, number;
			if (!noun_only(noun_an, &gender, &number) || gender == 0 || gender == lexicon::EpiceneGender || number == 0 || number == lexicon::Invariable)
				continue;
			const Token& adj = c.t[i + 1];
			c.lex->analyses(adj.text, adj_an);
			bool any_adj = false, ok = !adj_an.empty();
			for (const lexicon::Analysis& a : adj_an)
			{
				if (a.pos & ~(unsigned)(lexicon::Adj | lexicon::Noun))
				{
					ok = false;						// the word can be something else (a verb): not sure
					break;
				}
				if (a.pos & lexicon::Adj)
					any_adj = true;
			}
			if (!ok || !any_adj)
				continue;
			// A colour followed by another word is invariable ("bleu ciel"), and so are the compounds: no check
			if (in_list(adj.low, colors) && i + 2 < c.t.size() && c.word(i + 2))
			{
				Analyses next;
				c.lex->analyses(c.t[i + 2].text, next);
				bool noun_or_adj = false;
				for (const lexicon::Analysis& a : next)
					if (a.pos & (lexicon::Noun | lexicon::Adj))
						noun_or_adj = true;
				if (noun_or_adj || next.empty())
					continue;
			}
			if (i + 2 < c.t.size() && c.t[i + 2].kind == Token::Punct && c.t[i + 2].text == ",")
				continue;							// "le surlendemain, seule, ...": an apposition does not agree with the noun before it
			if (in_list(adj.low, colors) && i + 2 < c.t.size() && c.word(i + 2) && (c.t[i + 2].low == "et" || c.t[i + 2].low == "ou"))
				continue;							// "des wagons bleu et blanc": colours which agree with each other
			bool compatible = false;
			const lexicon::Analysis* model = nullptr;
			for (const lexicon::Analysis& a : adj_an)
			{
				if (!(a.pos & lexicon::Adj))
					continue;
				if (!model)
					model = &a;
				if ((a.gender == gender || a.gender == lexicon::EpiceneGender) && (a.number == number || a.number == lexicon::Invariable))
					compatible = true;
			}
			if (compatible || !model)
				continue;
			const std::string form = c.lex->inflect(*model, gender, number, 0, 0);
			if (!form.empty() && form != adj.low)
				c.add("fr.noun_adjective", adj, adj, 0.75f, match_case(adj.text, form), form);
		}
	}

	// Subject and verb: "tu mange" -> "tu manges", "ils est" -> "ils sont"
	static void rule_fr_subject_verb(const Ctx& c)
	{
		struct Subject { const char* word; int person, number; };
		static const Subject subjects[] = { { "je", 1, 1 }, { "j'", 1, 1 }, { "tu", 2, 1 }, { "il", 3, 1 }, { "elle", 3, 1 }, { "on", 3, 1 }, { "nous", 1, 2 }, { "vous", 2, 2 }, { "ils", 3, 2 }, { "elles", 3, 2 } };
		static const char* const starters[] = { "et", "ou", "mais", "que", "qu'", "si", "s'", "quand", "lorsque", "lorsqu'", "comme", "puisque", "puisqu'", "car", "donc", "alors", "o\xC3\xB9", "dont", "pendant", "ni", nullptr };
		static const char* const clitics[] = { "ne", "n'", "me", "m'", "te", "t'", "se", "s'", "le", "la", "l'", "les", "lui", "y", "en", "nous", "vous", nullptr };
		Analyses an;
		for (size_t i = 0; i + 1 < c.t.size(); i++)
		{
			if (!c.word(i))
				continue;
			const Subject* subject = nullptr;
			for (const Subject& s : subjects)
				if (c.t[i].low == s.word)
					subject = &s;
			if (!subject)
				continue;
			// The subject opens a clause: after a punctuation mark, the start of the paragraph or a conjunction
			if (i > 0 && !(c.t[i - 1].kind == Token::Punct || (c.t[i - 1].kind == Token::Word && in_list(c.t[i - 1].low, starters))))
				continue;
			// "nous" and "vous" are often objects ("que nous a pr\xC3\xA9sent\xC3\xA9", "vous et vous laisse"): subjects only after a punctuation mark
			if (i > 0 && c.t[i - 1].kind == Token::Word && (subject->word[0] == 'n' || subject->word[0] == 'v') && subject->number == 2 && subject->person != 3)
				continue;
			size_t v = i + 1;
			while (c.word(v) && in_list(c.t[v].low, clitics) && v < i + 4)
				v++;
			if (!c.word(v) || (v > i + 1 && c.t[v - 1].low == "y" && false))
				continue;
			c.lex->analyses(c.t[v].text, an);
			bool finite_found = false, match = false;
			const lexicon::Analysis* model = nullptr;
			for (const lexicon::Analysis& a : an)
			{
				if (!(a.pos & (lexicon::Verb | lexicon::Aux)))
					continue;
				if (a.verb_form < lexicon::Present || a.verb_form > lexicon::SubjunctiveImperfect)
					continue;						// imperative, infinitive and participles do not have a subject
				finite_found = true;
				if (!model)
					model = &a;
				if (a.person == subject->person && a.number == subject->number)
					match = true;
			}
			// A word which can also be a noun after the pronoun is not looked at ("il part", "on lit")
			bool other_reading = false;
			for (const lexicon::Analysis& a : an)
				if (a.pos & (lexicon::Det | lexicon::Pron | lexicon::Adp | lexicon::Adv | lexicon::Cconj | lexicon::Sconj))
					other_reading = true;
			if (!finite_found || match || other_reading || !model)
				continue;
			const std::string form = c.lex->inflect(*model, 0, subject->number, subject->person, model->verb_form);
			if (!form.empty() && form != c.t[v].low)
				c.add("fr.subject_verb", c.t[v], c.t[v], 0.8f, match_case(c.t[v].text, form), form);
		}
	}

	//-------------------------------------------------------------------------
	// Expressions which are written in a fixed way
	//-------------------------------------------------------------------------

	struct Phrase
	{
		const char* language;
		const char* words;							// the tokens, in lower case, separated by a space
		const char* replacement;
		bool not_before_capital;
	};

	static const Phrase PHRASES[] = {
		{ "fr", "lendemain matins", "lendemain matin", false }, { "fr", "lendemain soirs", "lendemain soir", false }, { "fr", "surlendemain matins", "surlendemain matin", false },
		{ "fr", "au dessus", "au-dessus", false }, { "fr", "au dessous", "au-dessous", false }, { "fr", "c' est \xC3\xA0 dire", "c'est-\xC3\xA0-dire", false }, { "fr", "vis \xC3\xA0 vis", "vis-\xC3\xA0-vis", false },
		{ "fr", "ci joint", "ci-joint", false }, { "fr", "quelque soit", "quel que soit", false }, { "fr", "quelque soient", "quels que soient", false }, { "fr", "quand \xC3\xA0", "quant \xC3\xA0", false },
		{ "fr", "a cause de", "\xC3\xA0 cause de", false }, { "fr", "a propos de", "\xC3\xA0 propos de", false }, { "fr", "d' ou", "d'o\xC3\xB9", false }, { "fr", "parce-que", "parce que", false },
		{ "en", "could of", "could have", false }, { "en", "would of", "would have", false }, { "en", "should of", "should have", false }, { "en", "must of", "must have", false }, { "en", "might of", "might have", false },
		{ "en", "alot", "a lot", false }, { "en", "your welcome", "you're welcome", false }, { "en", "its a", "it's a", false }, { "en", "its an", "it's an", false }, { "en", "its been", "it's been", false },
		{ "en", "its not", "it's not", false }, { "en", "its going", "it's going", false }, { "en", "irregardless", "regardless", false },
		{ "es", "a el", "al", true }, { "es", "de el", "del", true }, { "es", "haber si", "a ver si", false }, { "es", "a sido", "ha sido", false }, { "es", "mas o menos", "m\xC3\xA1s o menos", false },
	};

	// Fixed expressions which are always wrong in the language (table PHRASES): "au dessus" -> "au-dessus"
	static void rule_phrases(const Ctx& c, const std::string& language)
	{
		const std::string rule = language + ".phrase";
		for (const Phrase& p : PHRASES)
		{
			if (language != p.language)
				continue;
			std::vector<std::string> words;
			{
				std::string w;
				for (const char* s = p.words;; s++)
				{
					if (*s == ' ' || *s == 0)
					{
						words.push_back(w);
						w.clear();
						if (!*s)
							break;
					}
					else
						w += *s;
				}
			}
			for (size_t i = 0; i + words.size() <= c.t.size(); i++)
			{
				bool ok = true;
				for (size_t k = 0; k < words.size() && ok; k++)
					ok = c.t[i + k].kind == Token::Word && c.t[i + k].low == words[k];
				if (!ok)
					continue;
				if (p.not_before_capital && i + words.size() < c.t.size() && is_upper_cp(first_cp(c.t[i + words.size()].text)))
					continue;
				const std::string replacement = match_case(c.t[i].text, p.replacement);
				// For a French elision the rule keeps the following word: "d' ou" is the two tokens "d'" "ou"
				c.add(rule.c_str(), c.t[i], c.t[i + words.size() - 1], 0.85f, replacement, replacement);
			}
		}
	}

	//-------------------------------------------------------------------------
	// Engine
	//-------------------------------------------------------------------------

	// Every rule of the engine with its id, language and category (for the settings and the messages)
	const std::vector<RuleInfo>& Engine::rules()
	{
		static const std::vector<RuleInfo> list = {
			{ "any.repeat", "any", "repetition", 0.85f }, { "any.capital", "any", "punctuation", 0.7f },
			{ "fr.a", "fr", "spelling", 0.9f }, { "fr.ont", "fr", "spelling", 0.9f }, { "fr.sont", "fr", "spelling", 0.9f }, { "fr.participle", "fr", "conjugation", 0.8f },
			{ "fr.infinitive", "fr", "conjugation", 0.7f }, { "fr.phrase", "fr", "expression", 0.85f },
			{ "fr.det_number", "fr", "agreement", 0.8f }, { "fr.det_gender", "fr", "agreement", 0.75f }, { "fr.noun_adjective", "fr", "agreement", 0.75f }, { "fr.subject_verb", "fr", "agreement", 0.8f },
			{ "en.a_an", "en", "syntax", 0.75f }, { "en.then_than", "en", "spelling", 0.8f }, { "en.i", "en", "punctuation", 0.9f }, { "en.phrase", "en", "expression", 0.85f },
			{ "es.inverted", "es", "punctuation", 0.85f }, { "es.phrase", "es", "expression", 0.85f },
		};
		return list;
	}

	// Key of the translated message of a rule: GRAM_<ID>
	std::string message_key(const std::string& rule)
	{
		std::string k = "GRAM_";
		for (char ch : rule)
			k += ch == '.' ? '_' : (char)std::toupper((unsigned char)ch);
		return k;
	}

	// Language of the text (fr, en, es): decides which rules run
	void Engine::set_language(const std::string& code)
	{
		_language = code.substr(0, 2);
		for (char& ch : _language)
			ch = (char)std::tolower((unsigned char)ch);
	}

	// Runs every rule of the language on one paragraph; the issues are sorted by position, those of ignored rules are dropped
	std::vector<Issue> Engine::check_paragraph(const std::string& text) const
	{
		std::vector<Issue> issues;
		if (text.find_first_not_of(" \t") == std::string::npos)
			return issues;
		const std::vector<Token> tokens = tokenize(text, _language == "fr");
		const Ctx c{ tokens, text, _known, issues, _lexicon && _lexicon->loaded() ? _lexicon : nullptr };
		auto on = [&](const char* id) { return _ignored.find(id) == _ignored.end(); };
		if (on("any.repeat")) rule_repeat(c);
		if (on("any.capital")) rule_capital(c);
		if (_language == "fr")
		{
			if (on("fr.a")) rule_fr_a(c);
			if (on("fr.ont") && on("fr.sont")) rule_fr_ont_sont(c);
			if (on("fr.participle")) rule_fr_participle(c);
			if (on("fr.infinitive")) rule_fr_infinitive(c);
			if (on("fr.phrase")) rule_phrases(c, "fr");
			if (c.lex)
			{
				if (on("fr.det_number") && on("fr.det_gender")) rule_fr_determiner(c);
				if (on("fr.noun_adjective")) rule_fr_adjective(c);
				if (on("fr.subject_verb")) rule_fr_subject_verb(c);
			}
		}
		else if (_language == "en")
		{
			if (on("en.a_an")) rule_en_a_an(c);
			if (on("en.then_than")) rule_en_then(c);
			if (on("en.i")) rule_en_i(c);
			if (on("en.phrase")) rule_phrases(c, "en");
		}
		else if (_language == "es")
		{
			if (on("es.inverted")) rule_es_inverted(c);
			if (on("es.phrase")) rule_phrases(c, "es");
		}
		// Ignored rules which share a function
		issues.erase(std::remove_if(issues.begin(), issues.end(), [&](const Issue& is) { return !on(is.rule.c_str()); }), issues.end());
		std::sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) { return a.start < b.start; });
		// One issue for a stretch of text: the first one wins
		std::vector<Issue> clean;
		for (const Issue& is : issues)
			if (clean.empty() || is.start >= clean.back().end)
				clean.push_back(is);
		return clean;
	}
}
