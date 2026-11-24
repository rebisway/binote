// Binote - interface languages (OpenPostPro's Lang: lang/<code>.txt files made of KEY=value lines)`n// The code only knows English identifiers (MENU_FILE, BTN_SAVE...); every language, French included, has its file in the lang folder.`n// A key missing from a file is shown as the key itself.`n// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#ifndef _I18N_H
#define _I18N_H

#include <string>
#include <vector>

namespace i18n
{
	// Load a language ("fr-fr", "en-us"...). Empty code: the language of the system if a file exists for it, else French.
	void load(const std::string& code);
	std::string current();
	// Codes of the available languages, with their names
	std::vector<std::pair<std::string, std::string>> languages();
	// Translation of a text; a "##id" suffix (ImGui id) is kept. The pointer stays valid until the language changes.
	const char* tr(const char* text);
	std::string tr(const std::string& text);
	// printf-like with a translated format: i18n::fmt(TR("MSG_FILE_NOT_FOUND"), path.c_str())
	std::string fmt(const char* format, ...);
}

// A text of the interface
#define TR(text) i18n::tr(text)

#endif
