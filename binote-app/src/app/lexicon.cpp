// Binote - lexicon of the grammar checker
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "lexicon.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace lexicon
{
	bool Lexicon::load(const std::string& path)
	{
		_forms = _lemmas = _analyses = 0;
		std::ifstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
		if (!f)
			return false;
		const std::streamsize size = f.tellg();
		if (size < 36)
			return false;
		_data.resize((size_t)size);
		f.seekg(0);
		f.read(_data.data(), size);
		if (memcmp(_data.data(), "BNLX", 4) != 0)
			return false;
		const char* p = _data.data() + 4;
		auto u32 = [&]() { uint32_t v; memcpy(&v, p, 4); p += 4; return v; };
		const uint32_t version = u32();
		const uint32_t forms = u32(), lemmas = u32(), analyses = u32();
		u32();
		const uint32_t lemma_blob = u32(), form_blob = u32();
		if (version != 1)
			return false;
		// The tables are read in place: the data is aligned on 4 bytes by the layout (the header is 36 bytes)
		_lemma_offsets = (const uint32_t*)p;			p += 4 * ((size_t)lemmas + 1);
		_lemma_blob = p;								p += lemma_blob;
		const size_t pad1 = (4 - ((size_t)(p - _data.data()) % 4)) % 4;
		(void)pad1;
		_form_offsets = (const uint32_t*)p;				p += 4 * ((size_t)forms + 1);
		_form_blob = p;									p += form_blob;
		_first = (const uint32_t*)p;					p += 4 * ((size_t)forms + 1);
		_records = (const unsigned char*)p;
		if ((size_t)(p - _data.data()) + 8 * (size_t)analyses > _data.size())
			return false;
		_forms = forms;
		_lemmas = lemmas;
		_analyses = analyses;

		// Index by lemma, to find the other forms of a word
		_form_of.assign(analyses, 0);
		for (uint32_t i = 0; i < forms; i++)
			for (uint32_t a = _first[i]; a < _first[i + 1]; a++)
				_form_of[a] = i;
		_lemma_start.assign((size_t)lemmas + 2, 0);
		for (uint32_t a = 0; a < analyses; a++)
		{
			uint32_t lemma;
			memcpy(&lemma, _records + 8 * (size_t)a, 4);
			_lemma_start[(size_t)lemma + 2]++;
		}
		for (size_t i = 2; i < _lemma_start.size(); i++)
			_lemma_start[i] += _lemma_start[i - 1];
		_by_lemma.assign(analyses, 0);
		for (uint32_t a = 0; a < analyses; a++)
		{
			uint32_t lemma;
			memcpy(&lemma, _records + 8 * (size_t)a, 4);
			_by_lemma[_lemma_start[(size_t)lemma + 1]++] = a;
		}
		return true;
	}

	bool Lexicon::find(const std::string& word, uint32_t* form) const
	{
		uint32_t lo = 0, hi = _forms;
		while (lo < hi)
		{
			const uint32_t mid = (lo + hi) / 2;
			const char* s = _form_blob + _form_offsets[mid];
			const size_t len = _form_offsets[mid + 1] - _form_offsets[mid];
			const int c = memcmp(s, word.data(), std::min(len, word.size()));
			const int cmp = c != 0 ? c : (len < word.size() ? -1 : (len > word.size() ? 1 : 0));
			if (cmp == 0)
			{
				*form = mid;
				return true;
			}
			if (cmp < 0)
				lo = mid + 1;
			else
				hi = mid;
		}
		return false;
	}

	Analysis Lexicon::read(uint32_t index) const
	{
		const unsigned char* r = _records + 8 * (size_t)index;
		Analysis a;
		memcpy(&a.lemma, r, 4);
		uint16_t pos;
		memcpy(&pos, r + 4, 2);
		a.pos = pos;
		a.gender = r[6] & 3;
		a.number = (r[6] >> 2) & 3;
		a.person = (r[6] >> 4) & 3;
		a.verb_form = r[7];
		a.index = index;
		return a;
	}

	void Lexicon::analyses(const std::string& word, std::vector<Analysis>& out) const
	{
		out.clear();
		uint32_t form;
		if (!find(word, &form))
		{
			// Lower case of the Latin letters (the first letter of a sentence)
			std::string low;
			for (size_t i = 0; i < word.size(); i++)
			{
				const unsigned char c = (unsigned char)word[i];
				if (c >= 'A' && c <= 'Z')
					low += (char)(c + 32);
				else if (c == 0xC3 && i + 1 < word.size() && (unsigned char)word[i + 1] >= 0x80 && (unsigned char)word[i + 1] <= 0x9E && (unsigned char)word[i + 1] != 0x97)
				{
					low += (char)c;
					low += (char)((unsigned char)word[++i] + 0x20);
				}
				else
					low += (char)c;
			}
			if (low == word || !find(low, &form))
				return;
		}
		for (uint32_t a = _first[form]; a < _first[form + 1]; a++)
			out.push_back(read(a));
	}

	std::string Lexicon::lemma(const Analysis& a) const
	{
		return std::string(_lemma_blob + _lemma_offsets[a.lemma], _lemma_offsets[a.lemma + 1] - _lemma_offsets[a.lemma]);
	}

	std::string Lexicon::inflect(const Analysis& model, int gender, int number, int person, int verb_form) const
	{
		std::string best;
		int best_score = -1;
		for (uint32_t k = _lemma_start[(size_t)model.lemma]; k < _lemma_start[(size_t)model.lemma + 1]; k++)
		{
			const Analysis a = read(_by_lemma[k]);
			if ((a.pos & model.pos) == 0)
				continue;
			// A feature of the model which is asked as 0 is not looked at; a form which is invariable or epicene fits any value
			auto fits = [](int wanted, int has) { return wanted == 0 || has == wanted || has == 3; };
			if (!fits(gender, a.gender) || !fits(number, a.number) || (person && a.person != person) || (verb_form && a.verb_form != verb_form))
				continue;
			// The most exact form wins: the one which is not epicene or invariable
			const int score = (a.gender == gender ? 2 : 0) + (a.number == number ? 2 : 0);
			if (score > best_score)
			{
				best_score = score;
				const uint32_t form = _form_of[_by_lemma[k]];
				best.assign(_form_blob + _form_offsets[form], _form_offsets[form + 1] - _form_offsets[form]);
			}
		}
		return best;
	}
}
