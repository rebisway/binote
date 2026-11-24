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

// Binote - interface languages (lang/<code>.txt files made of KEY=value lines)
// The code only knows English identifiers (MENU_FILE, BTN_SAVE...); every language, French included, has its file in the lang folder.
// A key missing from a file is shown as the key itself.

#pragma once
#ifndef _I18N_H
#define _I18N_H

#include <string>
#include <utility>
#include <vector>

namespace i18n
{
	// The languages available (code, name written in that language)
	std::vector<std::pair<std::string, std::string>> languages();
	// Loads a language by its code; an empty code is the language of the system
	void load(const std::string& requested);
	// Code of the language in use
	std::string current();
	// Translation of a key
	std::string tr(const std::string& text);
	const char* tr(const char* text);
	// printf-like with a translated format: i18n::fmt(TR("MSG_FILE_NOT_FOUND"), path.c_str())
	std::string fmt(const char* format, ...);
}

// A text of the interface
#define TR(text) i18n::tr(text)

#endif
