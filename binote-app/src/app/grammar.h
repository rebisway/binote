// Binote - grammar checker (French, English, Spanish): rules on a list of tokens, no model, no network
// The engine reads one paragraph at a time (tokens, sentences) and runs independent rules: each one has an identifier, a language, a category,
// a confidence, and tells what is wrong and how to write it. A rule never works on the raw text with an expression: it works on tokens.
// The messages are in the interface language: the application translates "GRAM_" + the identifier (see message_key).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#ifndef _GRAMMAR_H
#define _GRAMMAR_H

#include "lexicon.h"
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace grammar
{
	struct Issue
	{
		std::string rule;							// "fr.participle"
		int start = 0, end = 0;						// bytes in the paragraph
		std::string arg;							// inserted in the message (for example the correct form)
		std::vector<std::string> suggestions;		// texts which replace [start, end); an empty text deletes
		float confidence = 0.0f;
	};

	struct RuleInfo
	{
		const char* id;
		const char* language;						// "fr", "en", "es", "any"
		const char* category;						// spelling, agreement, conjugation, syntax, punctuation, expression...
		float confidence;
	};

	// Key of the translated message of a rule: "fr.participle" -> "GRAM_FR_PARTICIPLE"
	std::string message_key(const std::string& rule);

	class Engine
	{
	public:
		void set_language(const std::string& code);									// "fr", "fr-FR", "en"... (the first two letters)
		// Tells if a word exists (the spelling dictionary): rules use it to be sure of a correction. Without it, the rules which need it are silent.
		void set_dictionary(std::function<bool(const std::string&)> known) { _known = known; }
		void set_ignored(const std::set<std::string>& rules) { _ignored = rules; }
		// The lexicon of the language (genders, numbers, persons): the rules of agreement need it. Not owned.
		void set_lexicon(const lexicon::Lexicon* lexicon) { _lexicon = lexicon; }
		const std::string& language() const { return _language; }

		std::vector<Issue> check_paragraph(const std::string& text) const;
		static const std::vector<RuleInfo>& rules();

	private:
		std::string _language = "fr";
		std::function<bool(const std::string&)> _known;
		std::set<std::string> _ignored;
		const lexicon::Lexicon* _lexicon = nullptr;
	};
}

#endif
