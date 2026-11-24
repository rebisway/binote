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

// Binote - interface languages

#include "common/i18n.h"
#include "common/lang.h"
#include "common/logger.h"
#include "common/util.h"
#include <algorithm>
#include <map>
#include <cstdarg>
#include <cstdio>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace i18n
{
	static std::map<std::string, std::string> cache;			// text (with its id) -> translation, std::map keeps the pointers valid

	// The folder of the language files: beside the executable, else up the tree (the executable may run from a build folder of the sources)
	static std::string lang_folder()
	{
		static std::string found;
		if (!found.empty())
			return found;
		const std::string app = util::application_path();
		const std::string candidates[] = { util::combine(app, "lang"), util::combine(app, "../lang"), util::combine(app, "../../lang"), util::combine(app, "../../binote-app/lang"),
			util::combine(app, "../../../binote-app/lang"), util::combine(app, "../binote-app/lang") };
		for (const std::string& c : candidates)
			if (util::exists(util::combine(c, "fr-fr.txt")))
				return found = c;
		return found = candidates[0];
	}

	// Language of the system, when Binote has a file for it
	static std::string system_language()
	{
		std::string code = "en-us";
#ifdef _WIN32
		wchar_t name[LOCALE_NAME_MAX_LENGTH];
		if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
		{
			std::string n;
			for (wchar_t* p = name; *p; p++)
				n += (char)std::tolower(*p);
			code = n;
		}
#else
		const char* lang = std::getenv("LANG");
		if (lang && std::strlen(lang) >= 5)
		{
			code = std::string(lang, 5);
			std::replace(code.begin(), code.end(), '_', '-');
			std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		}
#endif
		return code;
	}

	// The languages available (code, name written in that language), from the files lang/*.txt
	std::vector<std::pair<std::string, std::string>> languages()
	{
		std::vector<std::pair<std::string, std::string>> result;
		try
		{
			for (const std::string& code : Lang::get_langs(lang_folder()))
			{
				std::string name = Lang::language_name(code, util::combine(lang_folder(), code + ".txt"));
				result.push_back({ code, name.empty() ? code : name });
			}
		}
		catch (...) { /* no lang folder */ }
		std::sort(result.begin(), result.end());
		return result;
	}

	// Loads a language; when it is missing, another country of the same language, else French
	void load(const std::string& requested)
	{
		cache.clear();
		const auto available = languages();
		auto has = [&](const std::string& c) { return std::any_of(available.begin(), available.end(), [&](const auto& l) { return l.first == c; }); };
		std::string code = requested.empty() ? system_language() : requested;
		if (!has(code))
		{
			// "fr-ca" -> "fr-fr": same language, other country
			std::string prefix = code.substr(0, 2);
			code = "fr-fr";
			for (const auto& l : available)
				if (l.first.rfind(prefix, 0) == 0)
					code = l.first;
			if (!has(code))
				code = "fr-fr";
		}
		Logger::log("Language " + code + " from " + lang_folder());
		try { Lang::load(code, util::combine(lang_folder(), code + ".txt")); } catch (...) { }
	}

	std::string current()	{ return Lang::getName(); }

	// Translation of a key; a suffix "##id" is kept as it is (ImGui identifier)
	std::string tr(const std::string& text)
	{
		const size_t id = text.find("##");
		if (id == std::string::npos)
			return Lang::t(text);
		if (id == 0)
			return text;
		return Lang::t(text.substr(0, id)) + text.substr(id);
	}

	// Translation of a key as a C string; the answers are cached
	const char* tr(const char* text)
	{
		auto it = cache.find(text);
		if (it == cache.end())
			it = cache.insert({ text, tr(std::string(text)) }).first;
		return it->second.c_str();
	}

	// printf-like formatting
	std::string fmt(const char* format, ...)
	{
		char buf[2048];
		va_list args;
		va_start(args, format);
		vsnprintf(buf, sizeof(buf), format, args);
		va_end(args);
		return buf;
	}
}
