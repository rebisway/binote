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

// Binote - writing style analysis: indications, never a verdict
// Measures of the regularity and of the vocabulary of a text, and passages which show frequent stereotyped forms (connectors, introductory
// phrases, artificial conclusions) and repetitions. Offline, no score of "written by an AI": none of these indications proves anything,
// a technical or legal text can show all of them.

#pragma once
#ifndef _STYLE_H
#define _STYLE_H

#include <string>
#include <vector>

namespace style
{
	struct Passage
	{
		int line = 0;								// 1-based
		int start = 0, end = 0;						// bytes in the line
	};

	enum ObservationKind
	{
		Connector,									// text = the connector, count = occurrences
		StockPhrase,								// introductory phrase, transition, artificial conclusion
		CloseRepetition,							// text = word repeated close to itself, count = number of times
		RepeatedPhrase,								// text = a sequence of words which comes back, count = occurrences
		SameOpening,								// consecutive sentences with the same first word
		LongSentence,								// text = number of words
		EnumerationHabit,							// "A, B et C" in many sentences
		RedundantParagraphs,						// two paragraphs which use almost the same words
		ConstructionFamily,							// "Il est important de..." and its variants
		ParallelStructure,							// text = skeleton of the sentences (function words kept, the others "..."), count = sentences built the same way
		StyleBreak,									// a stretch of text whose function words differ from the rest of the text, count = words
	};

	struct Observation
	{
		int kind = Connector;
		std::string text;
		int count = 0;
		std::vector<Passage> passages;				// the first ones only
	};

	struct Metrics
	{
		int words = 0, sentences = 0, paragraphs = 0;
		double mean_sentence = 0.0;					// words
		double sentence_variation = 0.0;			// standard deviation / mean: low = very regular sentences
		double paragraph_variation = 0.0;			// same for the length of the paragraphs
		double lexical_diversity = 0.0;				// share of different words in windows of 100 words (0..1)
		double connectors_per_1000 = 0.0;
		double stock_phrases_per_1000 = 0.0;
		int long_sentences = 0;						// more than 40 words
		int enumerations = 0;							// sentences with a "A, B et C"
		// Vocabulary
		double type_token_ratio = 0.0;				// different words / words
		double hapax_ratio = 0.0;					// words which appear once / different words
		double mtld = 0.0;							// lexical diversity which depends less on the length
		double word_entropy = 0.0;					// Shannon entropy of the words, in bits
		double opening_entropy = 0.0;				// same for the first word of the sentences
		double zipf_slope = 0.0;					// slope of log(frequency) against log(rank), about -1 in natural texts
		double mean_word_length = 0.0;				// letters
		double word_length_variation = 0.0;
		// Punctuation, per 1000 words
		double colons = 0.0, semicolons = 0.0, parentheses = 0.0, dashes = 0.0, quotes = 0.0, exclamations = 0.0, ellipses = 0.0;
		// Discourse
		double transition_paragraphs = 0.0;			// share of the paragraphs which start with a connector or a stock phrase
		int redundant_pairs = 0;						// pairs of paragraphs with almost the same vocabulary
		int constructions = 0;						// "Il est important de..." forms
		// Function words (stylometry: Mosteller-Wallace, Burrows), per 1000 words
		double determiners = 0.0, prepositions = 0.0, conjunctions = 0.0, auxiliaries = 0.0, pronouns = 0.0;
		double function_word_overdispersion = 0.0;	// variance of the function words between stretches of text / the variance of chance: close to 1 = uniform style
		int style_breaks = 0;							// stretches whose function words depart from the rest
		// Predictability (n-gram model built on the text itself, as GLTR does with a language model)
		double perplexity = 0.0;						// of an adaptive trigram model, lower = more predictable
		double predictable_share = 0.0;				// words which are the most frequent follower of the previous word
		double compression_ratio = 0.0;				// of the words with an LZ77 scheme (1 = no repetition)
		double word_burstiness = 0.0;					// of the frequent content words, -1 regular .. +1 in bursts
		int burstiness_words = 0;
		double parallel_share = 0.0;					// sentences built on the same skeleton as others
		// Readability and rhythm
		double flesch = 0.0;							// reading ease (French: Kandel-Moles)
		double fog = 0.0;								// Gunning fog index
		double readability_variation = 0.0;			// of the reading ease of the paragraphs
		double rhythm_entropy = 0.0;					// of the transitions short/medium/long sentence, in bits
		double passive_share = 0.0;					// sentences with an auxiliary and a participle
	};

	// An indication, not a verdict: how many of the characteristics often seen in generated texts the text shows (0..100), with the part of each
	struct Contribution
	{
		std::string key;							// name of the characteristic (translated by the interface)
		double value = 0.0;							// measured value
		double points = 0.0;
		double max_points = 0.0;
	};

	struct Score
	{
		double value = 0.0;							// 0..100
		int reliability = 0;						// 0 not enough text (< 300 words), 1 low (< 1000 words), 2 usual
		std::vector<Contribution> contributions;
	};

	struct Report
	{
		Metrics metrics;
		Score score;
		std::vector<Observation> observations;		// most frequent first
	};

	// lines: the text, one string per line (a paragraph is a non-empty line). language: "fr", "en"... (the lists of expressions depend on it).
	Report analyze(const std::vector<std::string>& lines, bool markdown, const std::string& language);
}

#endif
