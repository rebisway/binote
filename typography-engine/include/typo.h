// typo - www.rebisway.com - contact@rebisway.com
// Copyright (c) 2026 Thomas Gourgnier
// SPDX-License-Identifier: MIT
//
// Licensed under the MIT license: see the LICENSE file in the folder of this component.

// typo : automatic typography driven by profiles, one profile per language (quotation marks, spaces before the signs, apostrophe, ellipsis, dashes).
// No dependencies but the standard library. Strings are UTF-8, offsets are byte offsets.
//
// Two ways to use it, which give the same result:
//   - while typing: OnTyped() receives the character which was just typed and the text of the line up to the caret, and tells what to replace;
//   - on a text: FixLine() and FixText() apply every rule to a line or to a whole text (a paragraph, a chapter).

// Index of this file:
// [SECTION] Profile
// [SECTION] Options and results
// [SECTION] Functions

#pragma once
#include <string>

//-----------------------------------------------------------------------------
// [SECTION] Profile
//-----------------------------------------------------------------------------

struct TypoProfile;                     // The rules of a language, loaded from text (see profiles/*.typo and the README). Opaque.

//-----------------------------------------------------------------------------
// [SECTION] Options and results
//-----------------------------------------------------------------------------

enum TypoQuoteStyle
{
    TypoQuoteStyle_Local    = 0,        // The quotation marks of the language (guillemets with their spaces in French)
    TypoQuoteStyle_Straight = 1,        // The straight quotes stay as they are
    TypoQuoteStyle_Curly    = 2         // The alternative marks of the profile (curly quotes by default)
};

struct TypoOptions
{
    int         QuoteStyle;             // A TypoQuoteStyle
    bool        Markdown;               // Code spans and fenced code blocks are left alone

    TypoOptions()                       { QuoteStyle = TypoQuoteStyle_Local; Markdown = false; }
};

// What to do after a character was typed: replace [Start, Start + Length) of the line by Text.
struct TypoEdit
{
    bool        Valid;                  // False: nothing to do
    int         Start;                  // Byte offset in the line
    int         Length;                 // Bytes to replace
    std::string Text;
    int         CaretExtra;             // Bytes to skip after the replacement to find the caret (a typed space which stays after the replaced text)

    TypoEdit()                          { Valid = false; Start = Length = CaretExtra = 0; }
};

//-----------------------------------------------------------------------------
// [SECTION] Functions
//-----------------------------------------------------------------------------

namespace Typo
{
    // Parses a profile. The text is in the format of profiles/*.typo. Returns NULL and fills error if it cannot be read.
    // Free it with DestroyProfile. 'parent' (may be NULL) is the profile whose settings this one starts from ("extends").
    TypoProfile*        CreateProfile(const char* text, const TypoProfile* parent, std::string* error);
    void                DestroyProfile(TypoProfile* profile);
    const char*         GetName(const TypoProfile* profile);
    const char*         GetExtends(const TypoProfile* profile);     // Code of the parent profile, or ""

    // The typographic character at the start of a code point string, for hosts which show the rules (spaces are named in the profile as U+XXXX).
    // True when the profile has something to do while typing the character c (a quotation mark, an apostrophe, a sign with a space before it...).
    bool                WatchesChar(const TypoProfile* profile, unsigned int c);

    // The character just typed (UTF-8) and the line up to the caret, typed character included.
    TypoEdit            OnTyped(const TypoProfile* profile, const TypoOptions& options, const std::string& typed, const std::string& before);

    // Rules applied to a line, a text. Code spans and fenced code blocks are left alone when options.Markdown is set.
    std::string         FixLine(const TypoProfile* profile, const TypoOptions& options, const std::string& line);
    std::string         FixText(const TypoProfile* profile, const TypoOptions& options, const std::string& text);

    // The opposite: plain characters. Every quotation mark becomes ", every apostrophe or single quote becomes ', the ellipsis becomes three dots,
    // the typographic spaces before the signs and inside the quotation marks of the profile and the inverted marks are removed.
    std::string         SimplifyLine(const TypoProfile* profile, const TypoOptions& options, const std::string& line);
    std::string         SimplifyText(const TypoProfile* profile, const TypoOptions& options, const std::string& text);
}
