// Binote - writing style analysis
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "style.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace style
{
	//-------------------------------------------------------------------------
	// Text helpers (UTF-8; the lower case keeps the length in bytes, so offsets stay valid)
	//-------------------------------------------------------------------------

	static unsigned int decode(const std::string& s, size_t i, int* n)
	{
		const unsigned char b = (unsigned char)s[i];
		*n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
		unsigned int c = *n == 1 ? b : (b & (0xFF >> (*n + 1)));
		for (int k = 1; k < *n && i + k < s.size(); k++)
			c = (c << 6) | ((unsigned char)s[i + k] & 0x3F);
		return c;
	}

	static bool is_letter(unsigned int c)
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || (c >= 0x370 && c <= 0x52F) || c == 0x153 || c == 0x152;
	}

	static std::string lower(const std::string& s)
	{
		std::string o = s;
		for (size_t i = 0; i < o.size(); i++)
		{
			unsigned char c = (unsigned char)o[i];
			if (c >= 'A' && c <= 'Z')
				o[i] = (char)(c + 32);
			else if (c == 0xC3 && i + 1 < o.size())			// U+00C0..U+00DE -> +0x20 (second byte 0x80..0x9E, except 0x97)
			{
				unsigned char d = (unsigned char)o[i + 1];
				if (d >= 0x80 && d <= 0x9E && d != 0x97)
					o[i + 1] = (char)(d + 0x20);
				i++;
			}
		}
		return o;
	}

	struct Word
	{
		std::string text;							// lower case
		int start, end;								// bytes in the line
	};

	// Words of a line: letters with ' ’ - inside
	static void words_of(const std::string& line, std::vector<Word>& out)
	{
		out.clear();
		const std::string low = lower(line);
		for (size_t i = 0; i < low.size();)
		{
			int n;
			unsigned int c = decode(low, i, &n);
			if (!is_letter(c))
			{
				i += n;
				continue;
			}
			size_t start = i, end = i;
			while (i < low.size())
			{
				c = decode(low, i, &n);
				if (is_letter(c))
				{
					i += n;
					end = i;
				}
				else if ((c == '\'' || c == 0x2019 || c == '-') && i + n < low.size())
				{
					int n2;
					if (is_letter(decode(low, i + n, &n2)))
						i += n;
					else
						break;
				}
				else
					break;
			}
			out.push_back({ low.substr(start, end - start), (int)start, (int)end });
		}
	}

	//-------------------------------------------------------------------------
	// Lists of expressions
	//-------------------------------------------------------------------------

	static const char* const FR_CONNECTORS[] = { "en effet", "de plus", "par ailleurs", "en outre", "de surcroît", "cependant", "toutefois", "néanmoins", "ainsi",
		"par conséquent", "en conséquence", "dès lors", "or,", "en revanche", "d'autre part", "d'une part", "en somme", "de ce fait", "c'est pourquoi", "par la suite", nullptr };
	static const char* const FR_STOCK[] = { "il est important de noter", "il convient de souligner", "il convient toutefois de", "il est à noter", "force est de constater",
		"il faut souligner", "cela étant dit", "dans cette optique", "dans ce contexte", "à cet égard", "en définitive", "en résumé", "en conclusion", "en somme",
		"il est essentiel de", "il est crucial de", "il est primordial de", "il convient de noter", "n'en demeure pas moins", "il va sans dire", "à l'heure actuelle",
		"de nos jours", "dans un monde", "au fil du temps", "jouer un rôle clé", "un rôle crucial", "il est indéniable", "force est de reconnaître", nullptr };
	static const char* const EN_CONNECTORS[] = { "moreover", "furthermore", "additionally", "however", "therefore", "thus", "consequently", "in addition", "on the other hand",
		"as a result", "nevertheless", "nonetheless", nullptr };
	static const char* const EN_STOCK[] = { "it is important to note", "it is worth noting", "it should be noted", "in conclusion", "in summary", "to sum up", "in today's",
		"plays a crucial role", "a testament to", "delve into", "in the realm of", "it is essential to", "navigate the", "tapestry", nullptr };

	// Words which are not worth reporting when they come back
	static const char* const STOP[] = { "qu'elle", "qu'il", "qu'ils", "qu'elles", "qu'on", "n'était", "n'avait", "s'était", "d'une", "d'un", "l'autre", "c'était", "c'est", "comme", "cette", "avait", "était", "étaient", "avaient", "aussi", "alors", "après", "avant", "chaque", "encore",
		"entre", "leurs", "leur", "mêmes", "même", "notre", "nous", "ils", "elle", "elles", "pendant", "peut", "plus", "pour", "quand", "quel", "quelque", "sans", "sera", "ses",
		"seulement", "tous", "tout", "toute", "toutes", "très", "vers", "vous", "dont", "donc", "depuis", "fait", "faire", "sous", "sont", "être", "avoir", "autre", "autres",
		"that", "this", "with", "from", "have", "were", "there", "their", "would", "which", "about", "could", nullptr };

	// Occurrences of an expression (whole words) in a lower case line
	static void find_phrase(const std::string& low, const char* phrase, std::vector<std::pair<int, int>>& found)
	{
		const std::string p = lower(phrase);
		for (size_t pos = low.find(p); pos != std::string::npos; pos = low.find(p, pos + 1))
		{
			auto letter_at = [&](size_t i, bool before)
			{
				if (before)
				{
					if (i == 0) return false;
					size_t j = i - 1;
					while (j > 0 && ((unsigned char)low[j] & 0xC0) == 0x80) j--;
					int n;
					return is_letter(decode(low, j, &n));
				}
				if (i >= low.size()) return false;
				int n;
				return is_letter(decode(low, i, &n));
			};
			const bool last_letter = p.back() != ',' && p.back() != ' ';
			if (letter_at(pos, true) || (last_letter && letter_at(pos + p.size(), false)))
				continue;
			found.push_back({ (int)pos, (int)(pos + p.size()) });
		}
	}

	static double mean_of(const std::vector<double>& v)
	{
		if (v.empty()) return 0.0;
		double s = 0;
		for (double x : v) s += x;
		return s / v.size();
	}

	static double variation_of(const std::vector<double>& v)
	{
		if (v.size() < 2) return 0.0;
		const double m = mean_of(v);
		if (m <= 0.0) return 0.0;
		double s = 0;
		for (double x : v) s += (x - m) * (x - m);
		return std::sqrt(s / v.size()) / m;
	}

	//-------------------------------------------------------------------------
	// Function words, syllables, and the models used by the new measures
	//-------------------------------------------------------------------------

	struct FunctionEntry { const char* word; int cat; };

	// French list written for this analysis; English: the 70 words of Mosteller and Wallace (Chen et al. 2026, table 1)
	// Categories: 1 determiner, 2 preposition, 4 conjunction, 8 auxiliary, 16 pronoun, 32 other
	static const FunctionEntry FR_FUNCTION[] = {
		{ "le", 1 }, { "la", 1 }, { "les", 1 }, { "l'", 1 }, { "un", 1 }, { "une", 1 }, { "des", 1 }, { "du", 1 }, { "au", 1 }, { "aux", 1 }, { "ce", 1 }, { "cet", 1 }, { "cette", 1 }, { "ces", 1 },
		{ "mon", 1 }, { "ma", 1 }, { "mes", 1 }, { "ton", 1 }, { "ta", 1 }, { "tes", 1 }, { "son", 1 }, { "sa", 1 }, { "ses", 1 }, { "notre", 1 }, { "nos", 1 }, { "votre", 1 }, { "vos", 1 }, { "leur", 1 }, { "leurs", 1 },
		{ "de", 2 }, { "d'", 2 }, { "à", 2 }, { "dans", 2 }, { "sur", 2 }, { "sous", 2 }, { "par", 2 }, { "pour", 2 }, { "avec", 2 }, { "sans", 2 }, { "entre", 2 }, { "vers", 2 }, { "chez", 2 }, { "en", 2 }, { "contre", 2 },
		{ "depuis", 2 }, { "pendant", 2 }, { "avant", 2 }, { "après", 2 },
		{ "et", 4 }, { "ou", 4 }, { "mais", 4 }, { "donc", 4 }, { "or", 4 }, { "ni", 4 }, { "car", 4 }, { "que", 4 }, { "qu'", 4 }, { "qui", 4 }, { "dont", 4 }, { "où", 4 }, { "si", 4 }, { "comme", 4 },
		{ "quand", 4 }, { "lorsque", 4 }, { "puisque", 4 },
		{ "est", 8 }, { "sont", 8 }, { "était", 8 }, { "étaient", 8 }, { "été", 8 }, { "être", 8 }, { "a", 8 }, { "ont", 8 }, { "avait", 8 }, { "avaient", 8 }, { "avoir", 8 }, { "sera", 8 }, { "seront", 8 }, { "fut", 8 },
		{ "il", 16 }, { "elle", 16 }, { "ils", 16 }, { "elles", 16 }, { "on", 16 }, { "je", 16 }, { "j'", 16 }, { "tu", 16 }, { "nous", 16 }, { "vous", 16 }, { "me", 16 }, { "m'", 16 }, { "te", 16 }, { "t'", 16 },
		{ "se", 16 }, { "s'", 16 }, { "c'", 16 }, { "lui", 16 }, { "eux", 16 }, { "y", 16 },
		{ "ne", 32 }, { "n'", 32 }, { "pas", 32 }, { "plus", 32 }, { "très", 32 }, { "aussi", 32 }, { "tout", 32 }, { "bien", 32 }, { "même", 32 }, { "autre", 32 }, { "alors", 32 }, { nullptr, 0 } };
	static const FunctionEntry EN_FUNCTION[] = {
		{ "a", 1 }, { "an", 1 }, { "the", 1 }, { "this", 1 }, { "its", 1 }, { "his", 1 }, { "her", 1 }, { "their", 1 }, { "our", 1 }, { "my", 1 }, { "your", 1 },
		{ "at", 2 }, { "by", 2 }, { "for", 2 }, { "from", 2 }, { "in", 2 }, { "into", 2 }, { "of", 2 }, { "on", 2 }, { "to", 2 }, { "up", 2 }, { "upon", 2 }, { "with", 2 }, { "down", 2 },
		{ "and", 4 }, { "but", 4 }, { "or", 4 }, { "as", 4 }, { "if", 4 }, { "so", 4 }, { "than", 4 }, { "that", 4 }, { "then", 4 }, { "when", 4 }, { "which", 4 }, { "who", 4 }, { "what", 4 },
		{ "are", 8 }, { "be", 8 }, { "been", 8 }, { "do", 8 }, { "had", 8 }, { "has", 8 }, { "have", 8 }, { "is", 8 }, { "was", 8 }, { "were", 8 }, { "will", 8 }, { "would", 8 }, { "can", 8 }, { "may", 8 },
		{ "must", 8 }, { "shall", 8 }, { "should", 8 },
		{ "it", 16 }, { "one", 16 },
		{ "all", 32 }, { "also", 32 }, { "any", 32 }, { "even", 32 }, { "every", 32 }, { "more", 32 }, { "no", 32 }, { "not", 32 }, { "now", 32 }, { "only", 32 }, { "such", 32 }, { "some", 32 },
		{ "there", 32 }, { "things", 32 }, { nullptr, 0 } };

	struct FunctionTable
	{
		std::unordered_map<std::string, int> index;		// word -> position in the list
		std::vector<int> cat;
	};

	static FunctionTable make_function_table(bool french)
	{
		FunctionTable t;
		for (const FunctionEntry* e = french ? FR_FUNCTION : EN_FUNCTION; e->word; e++)
		{
			t.index[e->word] = (int)t.cat.size();
			t.cat.push_back(e->cat);
		}
		return t;
	}

	// Elided article or pronoun at the start of a word (l'homme, qu'il): the prefix with a straight apostrophe, and its length in bytes. 0 if none.
	static int elision_of(const std::string& w, std::string& prefix)
	{
		for (int n = 1; n <= 3 && n < (int)w.size(); n++)
		{
			if (w[n] == '\'')
			{
				prefix = w.substr(0, n + 1);
				return n + 1;
			}
			if (n + 3 <= (int)w.size() && (unsigned char)w[n] == 0xE2 && (unsigned char)w[n + 1] == 0x80 && (unsigned char)w[n + 2] == 0x99)		// U+2019
			{
				prefix = w.substr(0, n) + "'";
				return n + 3;
			}
		}
		return 0;
	}

	// Syllables of a word by counting groups of vowels, minus a silent final e
	static int syllables_of(const std::string& w)
	{
		int count = 0;
		bool in_group = false;
		unsigned int last = 0;
		for (size_t i = 0; i < w.size();)
		{
			int n;
			const unsigned int c = decode(w, i, &n);
			const bool vowel = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y' || c == 0xE0 || c == 0xE2 || c == 0xE4 || c == 0xE9 || c == 0xE8 || c == 0xEA || c == 0xEB
				|| c == 0xEE || c == 0xEF || c == 0xF4 || c == 0xF6 || c == 0xF9 || c == 0xFB || c == 0xFC || c == 0x153;
			if (vowel && !in_group)
				count++;
			in_group = vowel;
			last = c;
			i += n;
		}
		if (count > 1 && (last == 'e' || (w.size() > 4 && w.compare(w.size() - 2, 2, "es") == 0) || (w.size() > 5 && w.compare(w.size() - 3, 3, "ent") == 0)))
			count--;
		return count < 1 ? 1 : count;
	}

	// Skeleton of a sentence: function words are kept, the stretches of other words are one "_"
	struct SkeletonToken
	{
		std::string text;
		int start, end;
	};

	struct SkeletonSentence
	{
		int line;
		std::vector<SkeletonToken> tokens;
	};

	// Greedy LZ77 over words: estimated size in bits of the text coded as literals and (distance, length) pointers
	static double lz_bits(const std::vector<int>& ids, int vocabulary)
	{
		const double literal = std::log2((double)vocabulary + 1.0) + 1.0;
		std::unordered_map<unsigned long long, std::vector<int>> last;
		double bits = 0.0;
		const int n = (int)ids.size();
		int i = 0;
		while (i < n)
		{
			int best_len = 0, best_pos = 0;
			if (i + 1 < n)
			{
				auto it = last.find(((unsigned long long)ids[i] << 32) | (unsigned int)ids[i + 1]);
				if (it != last.end())
					for (int pos : it->second)
					{
						int len = 0;
						while (i + len < n && len < 64 && pos + len < i && ids[pos + len] == ids[i + len])
							len++;
						if (len > best_len)
						{
							best_len = len;
							best_pos = pos;
						}
					}
			}
			const int step = best_len >= 2 ? best_len : 1;
			bits += best_len >= 2 ? 1.0 + std::log2((double)(i - best_pos) + 1.0) + 2.0 * std::log2((double)best_len) + 1.0 : literal;
			for (int k = i; k < i + step && k + 1 < n; k++)
			{
				std::vector<int>& v = last[((unsigned long long)ids[k] << 32) | (unsigned int)ids[k + 1]];
				v.push_back(k);
				if (v.size() > 12)
					v.erase(v.begin());
			}
			i += step;
		}
		return bits;
	}

	//-------------------------------------------------------------------------
	// Analysis
	//-------------------------------------------------------------------------

	Report analyze(const std::vector<std::string>& lines, bool markdown, const std::string& language)
	{
		Report rep;
		Metrics& m = rep.metrics;
		const bool french = language.rfind("fr", 0) == 0;
		const char* const* connectors = french ? FR_CONNECTORS : EN_CONNECTORS;
		const char* const* stock = french ? FR_STOCK : EN_STOCK;
		std::unordered_set<std::string> stop;
		for (const char* const* s = STOP; *s; s++)
			stop.insert(*s);

		std::map<std::string, Observation> conn_obs, stock_obs;
		std::unordered_map<std::string, Observation> close_obs;
		std::unordered_map<std::string, std::vector<Passage>> phrase_pos;			// 4-word sequences
		std::vector<Observation> others;
		std::vector<double> sentence_lengths, paragraph_lengths;
		std::vector<std::string> all_words;
		int connector_count = 0, stock_count = 0;

		int punct[7] = { 0, 0, 0, 0, 0, 0, 0 };							// : ; ( dashes quotes ! ellipses
		struct Para { int line; std::unordered_set<std::string> content; };
		std::vector<Para> paras;
		std::map<std::string, int> opening_counts;
		int opening_total = 0, transition_paras = 0, construction_count = 0;
		std::map<std::string, Observation> construction_obs;
		const FunctionTable fw = make_function_table(french);
		struct ParaWords { int line; int sentences; std::vector<Word> words; };
		std::vector<ParaWords> para_words;
		std::vector<SkeletonSentence> skeletons;
		std::vector<double> sentence_class;								// 0 short, 1 medium, 2 long
		std::vector<Observation> style_breaks;
		int passive_count = 0;

		bool in_fence = false;
		std::vector<Word> words;
		for (size_t li = 0; li < lines.size(); li++)
		{
			const std::string& line = lines[li];
			size_t a = line.find_first_not_of(" \t");
			if (a == std::string::npos)
				continue;
			if (markdown && (line.compare(a, 3, "```") == 0 || line.compare(a, 3, "~~~") == 0))
			{
				in_fence = !in_fence;
				continue;
			}
			if (in_fence || (markdown && (line[a] == '#' || line[a] == '|' || line.compare(a, 3, "---") == 0)))
				continue;
			const int ln = (int)li + 1;
			words_of(line, words);
			if (words.empty())
				continue;
			m.paragraphs++;
			paragraph_lengths.push_back((double)words.size());
			m.words += (int)words.size();
			for (size_t p = 0; p < line.size(); p++)
			{
				const unsigned char c = (unsigned char)line[p];
				if (c == ':') punct[0]++;
				else if (c == ';') punct[1]++;
				else if (c == '(') punct[2]++;
				else if (c == '!') punct[5]++;
				else if (c == '"') punct[4]++;
				else if (c == 0xC2 && p + 1 < line.size() && (unsigned char)line[p + 1] == 0xAB) punct[4]++;
				else if (c == 0xE2 && p + 2 < line.size() && (unsigned char)line[p + 1] == 0x80)
				{
					const unsigned char d = (unsigned char)line[p + 2];
					if (d == 0x94 || d == 0x93) punct[3]++;
					else if (d == 0xA6) punct[6]++;
				}
			}
			paras.push_back({ ln, {} });
			para_words.push_back({ ln, 0, words });
			for (const Word& w : words)
				if (w.text.size() >= 5 && !stop.count(w.text))
					paras.back().content.insert(w.text);

			// Connectors and stock phrases
			const std::string low = lower(line);
			size_t lead = 0;
			while (lead < low.size() && (low[lead] == ' ' || low[lead] == '\t' || low[lead] == '"' || low[lead] == '*' || low[lead] == '_' || low[lead] == '>' || low[lead] == '-'))
				lead++;
			bool starts_with_transition = false;
			std::vector<std::pair<int, int>> found;
			for (const char* const* c = connectors; *c; c++)
			{
				found.clear();
				find_phrase(low, *c, found);
				if (found.empty())
					continue;
				Observation& o = conn_obs[*c];
				o.kind = Connector;
				o.text = *c;
				for (auto& f : found)
				{
					if (f.first == (int)lead) starts_with_transition = true;
					o.count++;
					connector_count++;
					if (o.passages.size() < 12)
						o.passages.push_back({ ln, f.first, f.second });
				}
			}
			for (const char* const* c = stock; *c; c++)
			{
				found.clear();
				find_phrase(low, *c, found);
				if (found.empty())
					continue;
				Observation& o = stock_obs[*c];
				o.kind = StockPhrase;
				o.text = *c;
				for (auto& f : found)
				{
					if (f.first == (int)lead) starts_with_transition = true;
					o.count++;
					stock_count++;
					if (o.passages.size() < 12)
						o.passages.push_back({ ln, f.first, f.second });
				}
			}

			if (starts_with_transition)
				transition_paras++;
			// "Il est important de..." and its variants
			{
				static const char* const fr_adj[] = { "important", "essentiel", "crucial", "primordial", "nécessaire", "indispensable", "fondamental", "utile", "intéressant", "à noter", nullptr };
				static const char* const fr_adv[] = { "également", "aussi", "souvent", "toujours", "tout", "très", nullptr };
				static const char* const en_adj[] = { "important", "essential", "crucial", "necessary", "worth", "vital", nullptr };
				const char* lead_in = french ? "il est " : "it is ";
				const char* const* adjs = french ? fr_adj : en_adj;
				const size_t lead_len = strlen(lead_in);
				for (size_t pos = low.find(lead_in); pos != std::string::npos; pos = low.find(lead_in, pos + 1))
				{
					if (pos > 0 && is_letter((unsigned char)low[pos - 1])) continue;
					std::vector<Word> next;
					words_of(low.substr(pos + lead_len, 60), next);
					size_t k = 0;
					if (french && !next.empty())
						for (const char* const* a = fr_adv; *a; a++) if (next[0].text == *a) { k = 1; break; }
					if (k >= next.size()) continue;
					bool is_adj = false;
					for (const char* const* a = adjs; *a; a++) if (next[k].text == *a) is_adj = true;
					if (!is_adj || k + 1 >= next.size()) continue;
					const std::string& tail = next[k + 1].text;
					if (!(tail == "de" || tail == "que" || tail == "d'" || tail == "to" || tail == "that" || tail == "noting")) continue;
					const int end = (int)(pos + lead_len) + next[k + 1].end;
					Observation& o = construction_obs[french ? "il est ... de / que" : "it is ... to / that"];
					o.kind = ConstructionFamily;
					o.text = french ? "Il est (également) important / essentiel / crucial de..." : "It is important / essential / crucial to...";
					o.count++;
					construction_count++;
					if (o.passages.size() < 12)
						o.passages.push_back({ ln, (int)pos, end });
				}
			}

			// Sentences: [start, end) in words
			size_t first_word = 0;
			std::string prev_opening;
			int same_opening = 0;
			for (size_t i = 0; i <= line.size(); i++)
			{
				const bool end_of_line = i == line.size();
				bool stop_here = end_of_line;
				if (!end_of_line && (line[i] == '.' || line[i] == '!' || line[i] == '?'))
					stop_here = (i + 1 >= line.size() || line[i + 1] == ' ' || (unsigned char)line[i + 1] == 0xC2 || line[i + 1] == '"');
				if (!end_of_line && (unsigned char)line[i] == 0xE2 && i + 2 < line.size() && (unsigned char)line[i + 1] == 0x80 && (unsigned char)line[i + 2] == 0xA6)
					stop_here = true;
				if (!stop_here)
					continue;
				// words of the sentence: those which start before i
				size_t last_word = first_word;
				while (last_word < words.size() && words[last_word].start < (int)i)
					last_word++;
				const size_t count = last_word - first_word;
				if (count >= 1)
				{
						m.sentences++;
					para_words.back().sentences++;
					sentence_lengths.push_back((double)count);
					sentence_class.push_back(count < 10 ? 0.0 : (count <= 20 ? 1.0 : 2.0));
					// Skeleton: the function words, and "_" for the stretches of the other words
					{
						SkeletonSentence sk;
						sk.line = ln;
						for (size_t k = first_word; k < last_word; k++)
						{
							const Word& w = words[k];
							if (fw.index.count(w.text))
							{
								sk.tokens.push_back({ w.text, w.start, w.end });
								continue;
							}
							std::string prefix;
							const int el = elision_of(w.text, prefix);
							int content_start = w.start;
							if (el && fw.index.count(prefix))
							{
								sk.tokens.push_back({ prefix, w.start, w.start + el });
								content_start = w.start + el;
							}
							if (!sk.tokens.empty() && sk.tokens.back().text == "_")
								sk.tokens.back().end = w.end;
							else
								sk.tokens.push_back({ "_", content_start, w.end });
						}
						if (sk.tokens.size() >= 6)
							skeletons.push_back(sk);
					}
					// Passive voice: an auxiliary then a participle
					{
						static const char* const fr_aux[] = { "est", "sont", "été", "était", "étaient", "sera", "seront", "fut", nullptr };
						static const char* const en_aux[] = { "is", "are", "was", "were", "been", "be", nullptr };
						const char* const* aux = french ? fr_aux : en_aux;
						for (size_t k = first_word; k + 1 < last_word; k++)
						{
							bool is_aux = false;
							for (const char* const* a = aux; *a; a++)
								if (words[k].text == *a) is_aux = true;
							if (!is_aux)
								continue;
							const std::string& nx = words[k + 1].text;
							const bool participle = french ? (nx.size() > 4 && (nx.compare(nx.size() - 2, 2, "\xC3\xA9") == 0 || nx.compare(nx.size() - 3, 3, "\xC3\xA9" "e") == 0 || nx.compare(nx.size() - 3, 3, "\xC3\xA9" "s") == 0 || nx.compare(nx.size() - 4, 4, "\xC3\xA9" "es") == 0))
								: (nx.size() > 4 && (nx.compare(nx.size() - 2, 2, "ed") == 0 || nx.compare(nx.size() - 2, 2, "en") == 0));
							if (participle)
							{
								passive_count++;
								break;
							}
						}
					}
					if (count > 40)
					{
						m.long_sentences++;
						Observation o;
						o.kind = LongSentence;
						o.text = std::to_string(count);
						o.count = (int)count;
						o.passages.push_back({ ln, words[first_word].start, words[last_word - 1].end });
						others.push_back(o);
					}
					// "A, B et C": two commas then and/or inside a short stretch
					int commas = 0;
					for (int p = words[first_word].start; p < words[last_word - 1].end; p++)
						if (line[p] == ',')
							commas++;
					if (commas >= 2)
					{
						const std::string seg = low.substr(words[first_word].start, words[last_word - 1].end - words[first_word].start);
						if (seg.find(" et ") != std::string::npos || seg.find(" ou ") != std::string::npos || seg.find(" and ") != std::string::npos)
							m.enumerations++;
					}
					// Same first word in a row
					const std::string opening = words[first_word].text;
					opening_counts[opening]++;
					opening_total++;
					if (opening == prev_opening && opening.size() > 1)
					{
						same_opening++;
						if (same_opening == 2)
						{
							Observation o;
							o.kind = SameOpening;
							o.text = opening;
							o.count = 3;
							o.passages.push_back({ ln, words[first_word].start, words[first_word].end });
							others.push_back(o);
						}
					}
					else
						same_opening = 0;
					prev_opening = opening;
				}
				first_word = last_word;
			}

			// Close repetitions: a word of 5 letters or more which comes back within 25 words
			for (size_t i = 0; i < words.size(); i++)
			{
				const std::string& w = words[i].text;
				if (w.size() < 5 || stop.count(w))
					continue;
				for (size_t j = i + 1; j < words.size() && j <= i + 25; j++)
					if (words[j].text == w)
					{
						Observation& o = close_obs[w];
						o.kind = CloseRepetition;
						o.text = w;
						o.count++;
						if (o.passages.size() < 12)
							o.passages.push_back({ ln, words[j].start, words[j].end });
						break;
					}
			}
			// Sequences of 4 words which come back
			for (size_t i = 0; i + 4 <= words.size(); i++)
			{
				std::string key = words[i].text;
				for (size_t k = 1; k < 4; k++)
					key += " " + words[i + k].text;
				auto& v = phrase_pos[key];
				if (v.size() < 12)
					v.push_back({ ln, words[i].start, words[i + 3].end });
				else
					v.push_back(v.back());
			}
			for (const Word& w : words)
				all_words.push_back(w.text);
		}

		// Measures
		m.mean_sentence = mean_of(sentence_lengths);
		m.sentence_variation = variation_of(sentence_lengths);
		m.paragraph_variation = variation_of(paragraph_lengths);
		if (m.words > 0)
		{
			m.connectors_per_1000 = connector_count * 1000.0 / m.words;
			m.stock_phrases_per_1000 = stock_count * 1000.0 / m.words;
		}
		// Lexical diversity: share of different words in windows of 100 words, averaged (the plain ratio falls with the length of the text)
		if (all_words.size() >= 100)
		{
			double total = 0;
			int windows = 0;
			for (size_t i = 0; i + 100 <= all_words.size(); i += 50)
			{
				std::unordered_set<std::string> distinct(all_words.begin() + i, all_words.begin() + i + 100);
				total += distinct.size() / 100.0;
				windows++;
			}
			m.lexical_diversity = windows ? total / windows : 0.0;
		}

		// ---- Vocabulary: diversity, entropy, Zipf, length of the words
		if (!all_words.empty())
		{
			std::unordered_map<std::string, int> freq;
			for (const std::string& w : all_words)
				freq[w]++;
			const double n = (double)all_words.size();
			int hapax = 0;
			double entropy = 0.0;
			std::vector<int> counts;
			for (const auto& kv : freq)
			{
				if (kv.second == 1)
					hapax++;
				const double p = kv.second / n;
				entropy -= p * std::log2(p);
				counts.push_back(kv.second);
			}
			m.type_token_ratio = freq.size() / n;
			m.hapax_ratio = (double)hapax / (double)freq.size();
			m.word_entropy = entropy;
			// Zipf: log(frequency) against log(rank) on the most frequent words
			std::sort(counts.begin(), counts.end(), std::greater<int>());
			const size_t ranks = std::min<size_t>(counts.size(), 500);
			if (ranks >= 20)
			{
				double sx = 0, sy = 0, sxx = 0, sxy = 0;
				for (size_t r = 0; r < ranks; r++)
				{
					const double x = std::log((double)r + 1.0), y = std::log((double)counts[r]);
					sx += x; sy += y; sxx += x * x; sxy += x * y;
				}
				const double den = ranks * sxx - sx * sx;
				m.zipf_slope = den != 0.0 ? (ranks * sxy - sx * sy) / den : 0.0;
			}
			// Length of the words, in letters
			std::vector<double> lengths;
			lengths.reserve(all_words.size());
			for (const std::string& w : all_words)
			{
				int letters = 0;
				for (unsigned char c : w)
					if ((c & 0xC0) != 0x80)
						letters++;
				lengths.push_back(letters);
			}
			m.mean_word_length = mean_of(lengths);
			m.word_length_variation = variation_of(lengths);
			// MTLD: how many words it takes for the ratio of different words to fall to 0.72 (mean of both directions)
			if (all_words.size() >= 100)
			{
				auto mtld_pass = [&](bool forward)
				{
					double factors = 0.0;
					std::unordered_set<std::string> seen;
					int tokens = 0;
					for (size_t i = 0; i < all_words.size(); i++)
					{
						const std::string& w = forward ? all_words[i] : all_words[all_words.size() - 1 - i];
						seen.insert(w);
						tokens++;
						if ((double)seen.size() / tokens <= 0.72)
						{
							factors += 1.0;
							seen.clear();
							tokens = 0;
						}
					}
					if (tokens > 0)
						factors += (1.0 - (double)seen.size() / tokens) / (1.0 - 0.72);
					return factors > 0.0 ? all_words.size() / factors : (double)all_words.size();
				};
				m.mtld = (mtld_pass(true) + mtld_pass(false)) / 2.0;
			}
		}
		if (opening_total > 0)
			for (const auto& kv : opening_counts)
			{
				const double p = (double)kv.second / opening_total;
				m.opening_entropy -= p * std::log2(p);
			}
		if (m.words > 0)
		{
			const double k = 1000.0 / m.words;
			m.colons = punct[0] * k; m.semicolons = punct[1] * k; m.parentheses = punct[2] * k;
			m.dashes = punct[3] * k; m.quotes = punct[4] * k; m.exclamations = punct[5] * k; m.ellipses = punct[6] * k;
		}
		m.constructions = construction_count;
		if (m.paragraphs > 0)
			m.transition_paragraphs = (double)transition_paras / m.paragraphs;

		// ---- Discourse: paragraphs which use almost the same words (Jaccard on the vocabulary), looked for among the next six paragraphs
		Observation redundant;
		redundant.kind = RedundantParagraphs;
		for (size_t i = 0; i < paras.size(); i++)
		{
			if (paras[i].content.size() < 8)
				continue;
			for (size_t j = i + 1; j < paras.size() && j <= i + 6; j++)
			{
				if (paras[j].content.size() < 8)
					continue;
				size_t inter = 0;
				for (const std::string& w : paras[i].content)
					inter += paras[j].content.count(w);
				const double jac = (double)inter / (double)(paras[i].content.size() + paras[j].content.size() - inter);
				if (jac >= 0.40)
				{
					m.redundant_pairs++;
					redundant.count++;
					if (redundant.passages.size() < 12)
					{
						redundant.passages.push_back({ paras[i].line, 0, 60 });
						redundant.passages.push_back({ paras[j].line, 0, 60 });
					}
				}
			}
		}
		redundant.text = std::to_string(redundant.count);

		// ---- Function words (Mosteller-Wallace, Burrows): shares by category, and how much the profile moves from one stretch of text to the next
		{
			const size_t K = fw.cat.size();
			std::vector<int> total_counts(K, 0);
			double cat_count[6] = { 0, 0, 0, 0, 0, 0 };
			auto count_word = [&](const std::string& w, std::vector<int>& counts)
			{
				auto hit = [&](const std::string& key)
				{
					auto f = fw.index.find(key);
					if (f == fw.index.end())
						return;
					counts[f->second]++;
					for (int b = 0; b < 6; b++)
						if (fw.cat[f->second] & (1 << b))
							cat_count[b]++;
				};
				std::string prefix;
				const int el = elision_of(w, prefix);
				if (el)
				{
					hit(prefix);
					hit(w.substr(el));
				}
				else
					hit(w);
			};
			struct Chunk { std::vector<int> counts; int words = 0; std::vector<int> paras; };
			std::vector<Chunk> chunks;
			Chunk cur;
			cur.counts.assign(K, 0);
			for (size_t pi = 0; pi < para_words.size(); pi++)
			{
				for (const Word& w : para_words[pi].words)
				{
					count_word(w.text, cur.counts);
					cur.words++;
				}
				cur.paras.push_back((int)pi);
				if (cur.words >= 200)
				{
					chunks.push_back(cur);
					cur = Chunk();
					cur.counts.assign(K, 0);
				}
			}
			if (cur.words >= 100 || chunks.empty())
				chunks.push_back(cur);
			else
			{
				Chunk& last = chunks.back();
				for (size_t i = 0; i < K; i++)
					last.counts[i] += cur.counts[i];
				last.words += cur.words;
				last.paras.insert(last.paras.end(), cur.paras.begin(), cur.paras.end());
			}
			if (m.words > 0)
			{
				const double k = 1000.0 / m.words;
				m.determiners = cat_count[0] * k; m.prepositions = cat_count[1] * k; m.conjunctions = cat_count[2] * k; m.auxiliaries = cat_count[3] * k; m.pronouns = cat_count[4] * k;
			}
			if (chunks.size() >= 4)
			{
				int words = 0;
				for (const Chunk& c : chunks)
				{
					words += c.words;
					for (size_t i = 0; i < K; i++)
						total_counts[i] += c.counts[i];
				}
				std::vector<double> centroid(K);
				for (size_t i = 0; i < K; i++)
					centroid[i] = (double)total_counts[i] / words;
				std::vector<double> ratios;
				double dist_sum = 0.0, exp_sum = 0.0;
				for (const Chunk& c : chunks)
				{
					double dist = 0.0, expected = 0.0;
					for (size_t i = 0; i < K; i++)
					{
						const double v = (double)c.counts[i] / c.words - centroid[i];
						dist += v * v;
						expected += centroid[i] * (1.0 - centroid[i]) / c.words;
					}
					dist_sum += dist;
					exp_sum += expected;
					ratios.push_back(expected > 0.0 ? dist / expected : 0.0);
				}
				m.function_word_overdispersion = exp_sum > 0.0 ? dist_sum / exp_sum : 0.0;
				// A stretch whose profile is far from the others: another voice, a pasted passage
				if (chunks.size() >= 5)
				{
					const double mean = mean_of(ratios);
					double var = 0.0;
					for (double r : ratios) var += (r - mean) * (r - mean);
					const double sd = std::sqrt(var / ratios.size());
					for (size_t ci = 0; ci < chunks.size(); ci++)
						if (ratios[ci] > mean + 2.5 * sd && ratios[ci] > 2.5)
						{
							Observation o;
							o.kind = StyleBreak;
							o.count = chunks[ci].words;
							o.text = std::to_string(chunks[ci].words);
							for (int pi : chunks[ci].paras)
								if (o.passages.size() < 12)
									o.passages.push_back({ para_words[pi].line, 0, para_words[pi].words.back().end });
							style_breaks.push_back(o);
							m.style_breaks++;
						}
				}
			}
		}

		// ---- Predictability: a trigram model which learns the text as it reads it (a text which repeats itself becomes easy to predict), then its compression
		if (all_words.size() >= 50)
		{
			std::unordered_map<std::string, int> ids;
			std::vector<int> seq;
			seq.reserve(all_words.size());
			for (const std::string& w : all_words)
				seq.push_back(ids.emplace(w, (int)ids.size()).first->second);
			std::unordered_map<unsigned long long, int> c2, c3, ctx2;
			std::vector<int> c1(ids.size(), 0), ctx1(ids.size(), 0), best_next(ids.size(), -1), best_count(ids.size(), 0);
			double surprisal = 0.0;
			int hits = 0, eligible = 0;
			const int N = (int)seq.size();
			for (int i = 0; i < N; i++)
			{
				const int w = seq[i];
				const int a = i >= 1 ? seq[i - 1] : -1, b = i >= 2 ? seq[i - 2] : -1;
				double p = 0.05 / (double)(ids.size() + 1), weight = 0.05;
				if (i >= 2)
				{
					const unsigned long long k2 = ((unsigned long long)(unsigned)b << 32) | (unsigned)a;
					auto cx = ctx2.find(k2);
					if (cx != ctx2.end() && cx->second > 0)
					{
						auto t = c3.find(k2 * 1000003ULL + (unsigned)w);
						p += 0.5 * (t != c3.end() ? t->second : 0) / (double)cx->second;
						weight += 0.5;
					}
				}
				if (i >= 1 && ctx1[a] > 0)
				{
					auto t = c2.find(((unsigned long long)(unsigned)a << 32) | (unsigned)w);
					p += 0.3 * (t != c2.end() ? t->second : 0) / (double)ctx1[a];
					weight += 0.3;
					if (ctx1[a] >= 2)
					{
						eligible++;
						if (best_next[a] == w)
							hits++;
					}
				}
				if (i > 0)
				{
					p += 0.15 * c1[w] / (double)i;
					weight += 0.15;
				}
				surprisal -= std::log2(p / weight);
				// Learn
				c1[w]++;
				if (i >= 1)
				{
					int& c = c2[((unsigned long long)(unsigned)a << 32) | (unsigned)w];
					c++;
					ctx1[a]++;
					if (c > best_count[a])
					{
						best_count[a] = c;
						best_next[a] = w;
					}
				}
				if (i >= 2)
				{
					const unsigned long long k2 = ((unsigned long long)(unsigned)b << 32) | (unsigned)a;
					c3[k2 * 1000003ULL + (unsigned)w]++;
					ctx2[k2]++;
				}
			}
			m.perplexity = std::pow(2.0, surprisal / N);
			m.predictable_share = eligible > 0 ? (double)hits / eligible : 0.0;
			const double literal_bits = N * (std::log2((double)ids.size() + 1.0) + 1.0);
			m.compression_ratio = literal_bits > 0.0 ? lz_bits(seq, (int)ids.size()) / literal_bits : 0.0;
		}

		// ---- Burstiness of the frequent content words: a human theme comes in bursts, a regular text spreads its words evenly
		{
			std::unordered_map<std::string, std::vector<int>> positions;
			for (size_t i = 0; i < all_words.size(); i++)
				if (all_words[i].size() >= 5 && !stop.count(all_words[i]))
					positions[all_words[i]].push_back((int)i);
			double total = 0.0;
			int used = 0;
			for (const auto& kv : positions)
			{
				if (kv.second.size() < 6)
					continue;
				std::vector<double> gaps;
				for (size_t i = 1; i < kv.second.size(); i++)
					gaps.push_back((double)(kv.second[i] - kv.second[i - 1]));
				const double mu = mean_of(gaps);
				double var = 0.0;
				for (double g : gaps) var += (g - mu) * (g - mu);
				const double sd = std::sqrt(var / gaps.size());
				if (mu + sd > 0.0)
				{
					total += (sd - mu) / (sd + mu);
					used++;
				}
			}
			m.burstiness_words = used;
			m.word_burstiness = used > 0 ? total / used : 0.0;
		}

		// ---- Readability (reading ease, fog), its spread between paragraphs, rhythm of the sentences, passive voice
		if (m.words > 0 && m.sentences > 0)
		{
			double syllables = 0.0, complex_words = 0.0;
			std::vector<double> reading_ease;
			for (const ParaWords& p : para_words)
			{
				double s = 0.0;
				for (const Word& w : p.words)
				{
					const int n = syllables_of(w.text);
					s += n;
					if (n >= 3) complex_words += 1.0;
				}
				syllables += s;
				if (p.words.size() >= 15 && p.sentences >= 1)
				{
					const double asl = (double)p.words.size() / p.sentences, asw = s / p.words.size();
					reading_ease.push_back(french ? 207.0 - 1.015 * asl - 73.6 * asw : 206.835 - 1.015 * asl - 84.6 * asw);
				}
			}
			const double asl = (double)m.words / m.sentences, asw = syllables / m.words;
			m.flesch = french ? 207.0 - 1.015 * asl - 73.6 * asw : 206.835 - 1.015 * asl - 84.6 * asw;
			m.fog = 0.4 * (asl + 100.0 * complex_words / m.words);
			if (reading_ease.size() >= 5)
			{
				const double mu = mean_of(reading_ease);
				double var = 0.0;
				for (double r : reading_ease) var += (r - mu) * (r - mu);
				m.readability_variation = std::sqrt(var / reading_ease.size());
			}
			m.passive_share = (double)passive_count / m.sentences;
		}
		if (sentence_class.size() >= 10)
		{
			double joint[3][3] = {};
			for (size_t i = 1; i < sentence_class.size(); i++)
				joint[(int)sentence_class[i - 1]][(int)sentence_class[i]] += 1.0;
			const double n = (double)sentence_class.size() - 1.0;
			for (auto& row : joint)
				for (double c : row)
					if (c > 0.0)
						m.rhythm_entropy -= (c / n) * std::log2(c / n);
		}

		// ---- Parallel structure: sentences built on the same skeleton (the function words, in the same order) as at least two others
		std::vector<Observation> parallels;
		if (!skeletons.empty() && m.sentences > 0)
		{
			std::unordered_map<std::string, std::vector<std::pair<int, std::pair<int, int>>>> windows;
			for (size_t si = 0; si < skeletons.size(); si++)
			{
				const std::vector<SkeletonToken>& t = skeletons[si].tokens;
				for (size_t j = 0; j + 6 <= t.size(); j++)
				{
					int functions = 0;
					std::string key;
					for (size_t k = j; k < j + 6; k++)
					{
						if (t[k].text != "_") functions++;
						key += (k > j ? " " : "") + t[k].text;
					}
					if (functions >= 3 && functions <= 4)
						windows[key].push_back({ (int)si, { t[j].start, t[j + 5].end } });
				}
			}
			std::set<int> in_parallel;
			for (const auto& kv : windows)
			{
				std::set<int> distinct;
				for (const auto& e : kv.second)
					distinct.insert(e.first);
				if (distinct.size() < 3)
					continue;
				in_parallel.insert(distinct.begin(), distinct.end());
				Observation o;
				o.kind = ParallelStructure;
				o.count = (int)distinct.size();
				for (size_t i = 0; i < kv.first.size(); i++)
				{
					if (kv.first[i] == '_')
						o.text += "\xE2\x80\xA6";
					else
						o.text += kv.first[i];
				}
				int last_sentence = -1;
				for (const auto& e : kv.second)
					if (e.first != last_sentence && o.passages.size() < 12)
					{
						o.passages.push_back({ skeletons[e.first].line, e.second.first, e.second.second });
						last_sentence = e.first;
					}
				parallels.push_back(o);
			}
			m.parallel_share = (double)in_parallel.size() / m.sentences;
		}

		// ---- Indication: points for each characteristic often seen in generated texts. The weights are a first guess to be tuned on corpora.
		{
			Score& sc = rep.score;
			auto ramp = [](double v, double zero_at, double full_at)
			{
				const double t = (v - zero_at) / (full_at - zero_at);
				return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
			};
			auto add = [&](const char* key, double value, double points, double max_points)
			{
				sc.contributions.push_back({ key, value, points, max_points });
				sc.value += points;
			};
			const double FW_ZERO = 1.15, FW_FULL = 0.70, PAR_ZERO = 0.20, PAR_FULL = 0.50, PRED_ZERO = 0.15, PRED_FULL = 0.35, COMP_ZERO = 0.78, COMP_FULL = 0.55, BURST_ZERO = -0.10, BURST_FULL = -0.35;
			const double per_1000 = m.words > 0 ? 1000.0 / m.words : 0.0;
			int close_total = 0;
			for (const auto& kv : close_obs) close_total += kv.second.count;
			int repeated_phrases = 0;
			for (const auto& kv : phrase_pos) if (kv.second.size() >= 3) repeated_phrases++;
			int opening_series = 0;
			for (const Observation& o : others) if (o.kind == SameOpening) opening_series++;
			const double enum_share = m.sentences > 0 ? (double)m.enumerations / m.sentences : 0.0;

			add("SCORE_SENTENCE_REGULARITY", m.sentence_variation, m.sentences >= 15 ? 14.0 * ramp(m.sentence_variation, 0.50, 0.28) : 0.0, 14.0);
			add("SCORE_PARAGRAPH_REGULARITY", m.paragraph_variation, m.paragraphs >= 8 ? 6.0 * ramp(m.paragraph_variation, 0.50, 0.25) : 0.0, 6.0);
			add("SCORE_CONNECTORS", m.connectors_per_1000, 9.0 * ramp(m.connectors_per_1000, 3.0, 10.0), 9.0);
			add("SCORE_STOCK_PHRASES", m.stock_phrases_per_1000, 12.0 * ramp(m.stock_phrases_per_1000, 0.3, 2.5), 12.0);
			add("SCORE_CONSTRUCTIONS", m.constructions * per_1000, 5.0 * ramp(m.constructions * per_1000, 0.5, 4.0), 5.0);
			add("SCORE_LEXICAL", m.lexical_diversity, m.words >= 200 && m.lexical_diversity > 0.0 ? 8.0 * ramp(m.lexical_diversity, 0.62, 0.48) : 0.0, 8.0);
			add("SCORE_REPETITIONS", (close_total + repeated_phrases) * per_1000, 6.0 * ramp((close_total + repeated_phrases) * per_1000, 3.0, 15.0), 6.0);
			add("SCORE_OPENINGS", opening_series * per_1000, 3.0 * ramp(opening_series * per_1000, 1.0, 5.0), 3.0);
			add("SCORE_ENUMERATIONS", enum_share, 3.0 * ramp(enum_share, 0.10, 0.35), 3.0);
			add("SCORE_TRANSITIONS", m.transition_paragraphs, m.paragraphs >= 8 ? 5.0 * ramp(m.transition_paragraphs, 0.15, 0.50) : 0.0, 5.0);
			add("SCORE_REDUNDANCY", m.redundant_pairs * per_1000, 5.0 * ramp(m.redundant_pairs * per_1000, 0.5, 4.0), 5.0);
			// From the articles on stylometry and statistical detection (Chen et al., Gehrmann et al., Jiang et al.)
			add("SCORE_FUNCTION_WORDS", m.function_word_overdispersion, m.function_word_overdispersion > 0.0 && m.words >= 800 ? 8.0 * ramp(m.function_word_overdispersion, FW_ZERO, FW_FULL) : 0.0, 8.0);
			add("SCORE_PARALLELISM", m.parallel_share, m.sentences >= 20 ? 6.0 * ramp(m.parallel_share, PAR_ZERO, PAR_FULL) : 0.0, 6.0);
			add("SCORE_PREDICTABILITY", m.predictable_share, m.words >= 300 ? 4.0 * ramp(m.predictable_share, PRED_ZERO, PRED_FULL) : 0.0, 4.0);
			add("SCORE_COMPRESSION", m.compression_ratio, m.words >= 300 ? 3.0 * ramp(m.compression_ratio, COMP_ZERO, COMP_FULL) : 0.0, 3.0);
			add("SCORE_BURSTINESS", m.word_burstiness, m.burstiness_words >= 8 ? 3.0 * ramp(m.word_burstiness, BURST_ZERO, BURST_FULL) : 0.0, 3.0);
			sc.value = std::min(100.0, sc.value);
			sc.reliability = m.words < 300 ? 0 : (m.words < 1000 ? 1 : 2);
		}
		// Observations, most frequent first
		auto push_sorted = [&](std::vector<Observation>& list, size_t keep, int min_count)
		{
			std::sort(list.begin(), list.end(), [](const Observation& a, const Observation& b) { return a.count > b.count; });
			for (size_t i = 0; i < list.size() && i < keep; i++)
				if (list[i].count >= min_count)
					rep.observations.push_back(list[i]);
		};
		std::vector<Observation> list;
		for (auto& kv : construction_obs) list.push_back(kv.second);
		push_sorted(list, 2, 2);
		if (redundant.count > 0)
			rep.observations.push_back(redundant);
		list.clear();
		for (auto& kv : stock_obs) list.push_back(kv.second);
		push_sorted(list, 12, 1);
		list.clear();
		for (auto& kv : conn_obs) list.push_back(kv.second);
		push_sorted(list, 12, 3);
		list.clear();
		for (auto& kv : close_obs) list.push_back(kv.second);
		push_sorted(list, 12, 4);
		list.clear();
		for (auto& kv : phrase_pos)
			if (kv.second.size() >= 3)
			{
				Observation o;
				o.kind = RepeatedPhrase;
				o.text = kv.first;
				o.count = (int)kv.second.size();
				o.passages.assign(kv.second.begin(), kv.second.begin() + std::min<size_t>(12, kv.second.size()));
				list.push_back(o);
			}
		push_sorted(list, 10, 3);
		// Several sentences with the same opening, then the longest sentences
		std::vector<Observation> openings, longs;
		for (Observation& o : others)
			(o.kind == SameOpening ? openings : longs).push_back(o);
		std::map<std::string, Observation> by_opening;
		for (Observation& o : openings)
		{
			Observation& g = by_opening[o.text];
			g.kind = SameOpening;
			g.text = o.text;
			g.count++;
			if (g.passages.size() < 12)
				g.passages.push_back(o.passages[0]);
		}
		list.clear();
		for (auto& kv : by_opening) list.push_back(kv.second);
		push_sorted(list, 6, 2);
		push_sorted(longs, 6, 41);
		list.clear();
		for (Observation& o : parallels) list.push_back(o);
		push_sorted(list, 4, 3);
		for (Observation& o : style_breaks)
			rep.observations.push_back(o);
		return rep;
	}
}
