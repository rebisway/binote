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

// Binote - lexicon of the grammar checker: every inflected form of a language with its analyses, as Universal Dependencies features
// (part of speech, gender, number, person, verb form). The file is made by tools/build_lexicon.py from the lexicon of Grammalecte (MPL 2.0).
// Layout (little endian): "BNLX", version, forms, lemmas, analyses, 0, size of the lemma blob, size of the form blob, lemma offsets, lemma blob,
// form offsets, form blob, index of the first analysis of each form, analyses (8 bytes: lemma u32, part of speech mask u16, features u8, verb form u8).

#pragma once
#ifndef _LEXICON_H
#define _LEXICON_H

#include <cstdint>
#include <string>
#include <vector>

namespace lexicon
{
	enum Pos { Noun = 1, Adj = 2, Verb = 4, Aux = 8, Det = 16, Pron = 32, Adp = 64, Adv = 128, Cconj = 256, Sconj = 512, Propn = 1024, Intj = 2048, Other = 4096 };
	enum Gender { NoGender = 0, Masculine = 1, Feminine = 2, EpiceneGender = 3 };
	enum Number { NoNumber = 0, Singular = 1, Plural = 2, Invariable = 3 };
	enum VerbForm { NoForm = 0, Present = 1, Imperfect = 2, PastSimple = 3, Future = 4, Conditional = 5, SubjunctivePresent = 6, SubjunctiveImperfect = 7, Imperative = 8, Infinitive = 9, PresentParticiple = 10, PastParticiple = 11 };

	struct Analysis
	{
		uint32_t lemma = 0;
		unsigned pos = 0;						// mask of Pos: "nom adj" is Noun | Adj
		int gender = 0, number = 0, person = 0, verb_form = 0;
		uint32_t index = 0;						// position in the file, to find the form again
	};

	class Lexicon
	{
	public:
		bool load(const std::string& path);
		bool loaded() const { return _forms > 0; }
		// Analyses of a word as written, else in lower case. Empty if the word is unknown.
		void analyses(const std::string& word, std::vector<Analysis>& out) const;
		std::string lemma(const Analysis& a) const;
		// The form of the same lemma and part of speech with other features (0 = keep any); empty if there is none
		std::string inflect(const Analysis& model, int gender, int number, int person, int verb_form) const;

	private:
		std::vector<char> _data;
		uint32_t _forms = 0, _lemmas = 0, _analyses = 0;
		const uint32_t* _lemma_offsets = nullptr;
		const char* _lemma_blob = nullptr;
		const uint32_t* _form_offsets = nullptr;
		const char* _form_blob = nullptr;
		const uint32_t* _first = nullptr;
		const unsigned char* _records = nullptr;
		std::vector<uint32_t> _lemma_start, _by_lemma, _form_of;

		bool find(const std::string& word, uint32_t* form) const;
		Analysis read(uint32_t index) const;
	};
}

#endif
