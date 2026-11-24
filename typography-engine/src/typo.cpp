// typo - www.rebisway.com - contact@rebisway.com
// Copyright (c) 2026 Thomas Gourgnier
// SPDX-License-Identifier: MIT
//
// Licensed under the MIT license: see the LICENSE file in the folder of this component.

// typo : automatic typography driven by profiles (see typo.h)

// Index of this file:
// [SECTION] UTF-8
// [SECTION] Profile structure
// [SECTION] Profile parsing
// [SECTION] Rules on a segment of text
// [SECTION] Public API

#include "typo.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

//-----------------------------------------------------------------------------
// [SECTION] UTF-8
//-----------------------------------------------------------------------------

typedef std::u32string Text32;

// UTF-8 to code points; invalid bytes are kept as they are
static Text32 ToText32(const std::string& s)
{
    Text32 o;
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = (unsigned char)s[i];
        const int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        char32_t cp = len == 1 ? c : (c & (0xFF >> (len + 1)));
        for (int k = 1; k < len && i + k < s.size(); k++)
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        o += cp;
        i += len;
    }
    return o;
}

static void AppendUtf8(std::string& o, char32_t c)
{
    if (c < 0x80)
        o += (char)c;
    else if (c < 0x800)
    {
        o += (char)(0xC0 | (c >> 6));
        o += (char)(0x80 | (c & 0x3F));
    }
    else if (c < 0x10000)
    {
        o += (char)(0xE0 | (c >> 12));
        o += (char)(0x80 | ((c >> 6) & 0x3F));
        o += (char)(0x80 | (c & 0x3F));
    }
    else
    {
        o += (char)(0xF0 | (c >> 18));
        o += (char)(0x80 | ((c >> 12) & 0x3F));
        o += (char)(0x80 | ((c >> 6) & 0x3F));
        o += (char)(0x80 | (c & 0x3F));
    }
}

static std::string ToUtf8(const Text32& s)
{
    std::string o;
    for (char32_t c : s)
        AppendUtf8(o, c);
    return o;
}

static std::string ToUtf8(char32_t c)
{
    std::string o;
    AppendUtf8(o, c);
    return o;
}

//-----------------------------------------------------------------------------
// [SECTION] Profile structure
//-----------------------------------------------------------------------------

struct TypoSign
{
    char32_t    Sign;                   // The sign which takes a space before it: ; ! ? : %
    char32_t    Space;                  // The space put before it (U+202F, U+00A0)
    bool        Closers;                // May be followed by a closing sign: ) " » * _ ]
};

// A set of characters, with the two classes of letters and digits
struct TypoSet
{
    bool                    Letter;
    bool                    Digit;
    std::vector<char32_t>   Chars;

    TypoSet()                           { Letter = Digit = false; }
    bool Has(char32_t c) const;
};

struct TypoProfile
{
    std::string Name, Extends;
    // Quotation marks: the marks of the language, the spaces inside them, the alternative marks (curly quotes)
    char32_t    QuoteOpen, QuoteClose, GapOpen, GapClose, AltOpen, AltClose;
    TypoSet     OpenAfter;              // After these a straight quote opens a quotation (when a text is fixed)
    TypoSet     OpenAfterTyped;         // The same while typing
    char32_t    Apostrophe;             // Replaces ' between two letters, and ' when typed (0 = none)
    char32_t    Ellipsis;               // Replaces three dots (0 = none)
    char32_t    SpacedDash;             // Replaces -- between two spaces (0 = none)
    std::vector<TypoSign>   SpaceBefore;
    TypoSet     Prev;                   // What a sign may follow when a text is fixed (a space is put before the sign)
    TypoSet     PrevTyped;              // The same while typing
    TypoSet     Closers;
    std::vector<std::pair<char32_t, char32_t> > Inverted;   // ? -> an inverted mark put at the start of the sentence (Spanish: ¿ ¡)

    TypoProfile()
    {
        QuoteOpen = QuoteClose = GapOpen = GapClose = 0;
        AltOpen = 0x201C;
        AltClose = 0x201D;
        Apostrophe = Ellipsis = SpacedDash = 0;
    }
};

static const char32_t C_NBSP = 0x00A0, C_NNBSP = 0x202F;

// Latin letters (accented ones included): the other scripts are not classed as letters yet
static bool IsLetter(char32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) || c == 0x153 || c == 0x152;
}
static bool IsDigit(char32_t c)         { return c >= '0' && c <= '9'; }
static bool IsSpace(char32_t c)         { return c == ' ' || c == '\t' || c == C_NBSP || c == C_NNBSP; }

bool TypoSet::Has(char32_t c) const
{
    return (Letter && IsLetter(c)) || (Digit && IsDigit(c)) || std::find(Chars.begin(), Chars.end(), c) != Chars.end();
}

//-----------------------------------------------------------------------------
// [SECTION] Profile parsing
//-----------------------------------------------------------------------------
// Format: lines "key = value" in sections "[name]", "#" starts a comment. A value is a list of items separated by spaces:
// "U+202F" (a code point), "space", "tab", "letter", "digit" (classes), "none", or one character written as it is.

static std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        b--;
    return s.substr(a, b - a);
}

static std::vector<std::string> Split(const std::string& s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            i++;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t')
            j++;
        if (j > i)
            out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// One item of a value as a code point: "U+202F", "space", "tab", or a single character. False if it is none of these.
static bool ParseChar(const std::string& item, char32_t* out)
{
    if (item.size() > 2 && (item[0] == 'U' || item[0] == 'u') && item[1] == '+')
    {
        char* end = nullptr;
        const unsigned long v = strtoul(item.c_str() + 2, &end, 16);
        if (*end != 0 || v == 0 || v > 0x10FFFF)
            return false;
        *out = (char32_t)v;
        return true;
    }
    if (item == "space") { *out = ' '; return true; }
    if (item == "tab")   { *out = '\t'; return true; }
    const Text32 t = ToText32(item);
    if (t.size() != 1)
        return false;
    *out = t[0];
    return true;
}

// A value which is one character, or "none" (= 0)
static bool ParseOneChar(const std::string& value, char32_t* out, std::string* error, const std::string& key)
{
    const std::string v = Trim(value);
    if (v == "none" || v.empty())
    {
        *out = 0;
        return true;
    }
    if (!ParseChar(v, out))
    {
        *error = "bad character for " + key + ": " + v;
        return false;
    }
    return true;
}

static bool ParseSet(const std::string& value, TypoSet* out, std::string* error, const std::string& key)
{
    TypoSet set;
    for (const std::string& item : Split(value))
    {
        char32_t c;
        if (item == "none")
            continue;
        if (item == "letter")
            set.Letter = true;
        else if (item == "digit")
            set.Digit = true;
        else if (ParseChar(item, &c))
            set.Chars.push_back(c);
        else
        {
            *error = "bad item for " + key + ": " + item;
            return false;
        }
    }
    *out = set;
    return true;
}

TypoProfile* Typo::CreateProfile(const char* text, const TypoProfile* parent, std::string* error)
{
    std::string local_error;
    std::string& err = error ? *error : local_error;
    TypoProfile* p = parent ? new TypoProfile(*parent) : new TypoProfile();
    std::string section;
    bool inverted_reset = false;
    bool sign_section_reset = false;            // "space_before" of a profile replaces the table of its parent, it does not add to it
    const std::string source = text ? text : "";
    size_t pos = 0;
    int number = 0;
    while (pos <= source.size())
    {
        size_t eol = source.find('\n', pos);
        if (eol == std::string::npos)
            eol = source.size();
        std::string line = source.substr(pos, eol - pos);
        pos = eol + 1;
        number++;
        const size_t hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        line = Trim(line);
        if (line.empty())
            continue;
        if (line[0] == '[' && line.back() == ']')
        {
            section = Trim(line.substr(1, line.size() - 2));
            sign_section_reset = false;
            continue;
        }
        const size_t eq = line.find('=', 1);
        if (eq == std::string::npos)
        {
            err = "line " + std::to_string(number) + ": expected key = value";
            delete p;
            return nullptr;
        }
        const std::string key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        bool ok = true;
        if (section == "profile" && key == "name")
            p->Name = value;
        else if (section == "profile" && key == "extends")
            p->Extends = value;
        else if (section == "quotes" && key == "open")
            ok = ParseOneChar(value, &p->QuoteOpen, &err, key);
        else if (section == "quotes" && key == "close")
            ok = ParseOneChar(value, &p->QuoteClose, &err, key);
        else if (section == "quotes" && key == "gap_open")
            ok = ParseOneChar(value, &p->GapOpen, &err, key);
        else if (section == "quotes" && key == "gap_close")
            ok = ParseOneChar(value, &p->GapClose, &err, key);
        else if (section == "quotes" && key == "alt_open")
            ok = ParseOneChar(value, &p->AltOpen, &err, key);
        else if (section == "quotes" && key == "alt_close")
            ok = ParseOneChar(value, &p->AltClose, &err, key);
        else if (section == "quotes" && key == "open_after")
            ok = ParseSet(value, &p->OpenAfter, &err, key);
        else if (section == "quotes" && key == "open_after_typed")
            ok = ParseSet(value, &p->OpenAfterTyped, &err, key);
        else if (section == "apostrophe" && key == "char")
            ok = ParseOneChar(value, &p->Apostrophe, &err, key);
        else if (section == "ellipsis" && key == "char")
            ok = ParseOneChar(value, &p->Ellipsis, &err, key);
        else if (section == "dash" && key == "spaced_double_hyphen")
            ok = ParseOneChar(value, &p->SpacedDash, &err, key);
        else if (section == "context" && key == "prev")
            ok = ParseSet(value, &p->Prev, &err, key);
        else if (section == "context" && key == "prev_typed")
            ok = ParseSet(value, &p->PrevTyped, &err, key);
        else if (section == "context" && key == "closers")
            ok = ParseSet(value, &p->Closers, &err, key);
        else if (section == "inverted")
        {
            // "? = ¿": the sign which ends a sentence, the mark put at the start of it
            char32_t sign = 0, mark = 0;
            if (!ParseChar(key, &sign) || !ParseChar(value, &mark))
            {
                err = "bad inverted entry";
                ok = false;
            }
            else
            {
                if (!inverted_reset)
                {
                    p->Inverted.clear();
                    inverted_reset = true;
                }
                p->Inverted.push_back(std::make_pair(sign, mark));
            }
        }
        else if (section == "space_before")
        {
            // "; = U+202F closers": the sign, the space, and "closers" if a closing sign may follow
            if (!sign_section_reset)
            {
                p->SpaceBefore.clear();
                sign_section_reset = true;
            }
            char32_t sign = 0, space = 0;
            const std::vector<std::string> items = Split(value);
            if (!ParseChar(key, &sign) || items.empty() || !ParseChar(items[0], &space))
            {
                err = "line " + std::to_string(number) + ": bad space_before entry";
                ok = false;
            }
            else
            {
                TypoSign s;
                s.Sign = sign;
                s.Space = space;
                s.Closers = items.size() > 1 && items[1] == "closers";
                p->SpaceBefore.push_back(s);
            }
        }
        else
        {
            err = "line " + std::to_string(number) + ": unknown key [" + section + "] " + key;
            ok = false;
        }
        if (!ok)
        {
            if (err.compare(0, 4, "line") != 0)
                err = "line " + std::to_string(number) + ": " + err;
            delete p;
            return nullptr;
        }
    }
    return p;
}

void Typo::DestroyProfile(TypoProfile* profile)             { delete profile; }
const char* Typo::GetName(const TypoProfile* profile)       { return profile->Name.c_str(); }
const char* Typo::GetExtends(const TypoProfile* profile)    { return profile->Extends.c_str(); }

bool Typo::WatchesChar(const TypoProfile* profile, unsigned int c)
{
    if (c == '"')
        return profile->QuoteOpen != 0 || profile->AltOpen != 0;
    if (c == '\'')
        return profile->Apostrophe != 0;
    if (c == '.')
        return profile->Ellipsis != 0;
    for (const TypoSign& s : profile->SpaceBefore)
        if (s.Sign == c)
            return true;
    for (size_t i = 0; i < profile->Inverted.size(); i++)
        if (profile->Inverted[i].first == c)
            return true;
    return false;
}

//-----------------------------------------------------------------------------
// [SECTION] Rules on a segment of text
//-----------------------------------------------------------------------------

// Where the inverted mark of the sign at s[i] goes: the start of its sentence. npos when there is nothing to add (no inverted mark for this sign,
// one is already there, the sign is part of a group "?!" or of a word such as "a?b").
static size_t InvertedPos(const TypoProfile& p, const Text32& s, size_t i)
{
    char32_t mark = 0;
    for (size_t k = 0; k < p.Inverted.size(); k++)
        if (p.Inverted[k].first == s[i])
            mark = p.Inverted[k].second;
    if (!mark || i == 0)
        return (size_t)-1;
    const char32_t before = s[i - 1];
    if (IsSpace(before) || before == '?' || before == '!')
        return (size_t)-1;
    if (i + 1 < s.size() && !IsSpace(s[i + 1]) && !p.Closers.Has(s[i + 1]) && s[i + 1] != ',' && s[i + 1] != '.')
        return (size_t)-1;
    for (size_t j = i; j-- > 0;)
    {
        const char32_t c = s[j];
        for (size_t k = 0; k < p.Inverted.size(); k++)
            if (c == p.Inverted[k].second)
                return (size_t)-1;                      // already opened (by this mark, or by another one: "¡¿")
        const bool terminator = c == '.' || c == '!' || c == '?' || c == 0x2026;
        const bool opener = c == '(' || c == '[' || c == 0xAB || c == 0x201C;     // a quotation or a parenthesis starts a sentence of its own
        if (opener || (terminator && j + 1 < s.size() && IsSpace(s[j + 1])))
        {
            size_t start = j + 1;
            while (start < i && IsSpace(s[start]))
                start++;
            while (start < i && (s[start] == '(' || s[start] == '[' || s[start] == 0xAB || s[start] == 0x201C || s[start] == '"' || s[start] == 0x2014 || s[start] == 0x2013))
                start++;
            return start < i ? start : (size_t)-1;
        }
    }
    size_t start = 0;
    while (start < i && (IsSpace(s[start]) || s[start] == '(' || s[start] == '[' || s[start] == 0xAB || s[start] == 0x201C || s[start] == '"' || s[start] == 0x2014 || s[start] == 0x2013))
        start++;
    return start < i ? start : (size_t)-1;
}

// The rules of a profile on a piece of text which holds no code: ellipsis, apostrophe, dash, then the quotation marks, the spaces inside them
// and the spaces before the signs.
static Text32 FixSegment(const TypoProfile& p, const TypoOptions& options, const Text32& in)
{
    const size_t n = in.size();
    Text32 s;
    // Ellipsis, apostrophe, dash
    for (size_t i = 0; i < n; i++)
    {
        const char32_t c = in[i];
        if (p.Ellipsis && c == '.' && i + 2 < n && in[i + 1] == '.' && in[i + 2] == '.' && (i + 3 >= n || in[i + 3] != '.') && (i == 0 || in[i - 1] != '.'))
        {
            s += p.Ellipsis;
            i += 2;
        }
        else if (p.Apostrophe && c == '\'' && i > 0 && i + 1 < n && IsLetter(in[i - 1]) && IsLetter(in[i + 1]))
            s += p.Apostrophe;
        else if (p.SpacedDash && c == '-' && i > 0 && i + 2 < n && in[i + 1] == '-' && in[i - 1] < 0x80 && in[i + 2] < 0x80 && isspace((int)in[i - 1]) && isspace((int)in[i + 2]))
        {
            s += p.SpacedDash;
            i += 1;
        }
        else
            s += c;
    }
    // Straight quotes become the marks of the language, or the alternative ones, or stay as they are
    const bool local = p.QuoteOpen != 0 && options.QuoteStyle == TypoQuoteStyle_Local;
    const bool alt = p.AltOpen != 0 && options.QuoteStyle == TypoQuoteStyle_Curly;
    for (size_t i = 0; i < s.size() && (local || alt); i++)
        if (s[i] == '"')
        {
            const char32_t prev = i > 0 ? s[i - 1] : U' ';
            const bool open = i == 0 || p.OpenAfter.Has(prev);
            if (alt)
                s[i] = open ? p.AltOpen : p.AltClose;
            else
                s[i] = open ? p.QuoteOpen : p.QuoteClose;
        }
    // Spaces inside the quotation marks of the language
    Text32 t;
    if (p.QuoteOpen && (p.GapOpen || p.GapClose))
    {
        for (size_t i = 0; i < s.size(); i++)
        {
            const char32_t c = s[i];
            if (c == p.QuoteOpen)
            {
                t += c;
                size_t j = i + 1;
                while (j < s.size() && IsSpace(s[j]))
                    j++;
                if (j < s.size() && p.GapOpen)
                    t += p.GapOpen;
                else if (!p.GapOpen)
                    t.append(s, i + 1, j - i - 1);
                i = j - 1;
            }
            else if (p.GapClose && IsSpace(c))
            {
                size_t j = i;
                while (j < s.size() && IsSpace(s[j]))
                    j++;
                if (j < s.size() && s[j] == p.QuoteClose)
                {
                    t += p.GapClose;
                    t += p.QuoteClose;
                    i = j;
                }
                else
                {
                    t.append(s, i, j - i);
                    i = j - 1;
                }
            }
            else if (p.GapClose && c == p.QuoteClose)
            {
                if (t.empty() || t.back() != p.GapClose)
                    t += p.GapClose;
                t += c;
            }
            else
                t += c;
        }
    }
    else
        t = s;
    // The space before the signs: ; ! ? : ...
    Text32 o;
    for (size_t i = 0; i < t.size(); i++)
    {
        const char32_t c = t[i];
        const TypoSign* sign = nullptr;
        for (const TypoSign& candidate : p.SpaceBefore)
            if (candidate.Sign == c)
                sign = &candidate;
        if (sign)
        {
            size_t k = o.size();
            while (k > 0 && IsSpace(o[k - 1]))
                k--;
            const bool prev_ok = k > 0 && p.Prev.Has(o[k - 1]);
            const char32_t next = i + 1 < t.size() ? t[i + 1] : U' ';
            const bool next_ok = i + 1 >= t.size() || IsSpace(next) || (sign->Closers && p.Closers.Has(next));
            if (prev_ok && next_ok)
            {
                o.resize(k);
                o += sign->Space;
            }
        }
        o += c;
    }
    // The inverted marks at the start of the sentences
    if (!p.Inverted.empty())
        for (size_t i = 0; i < o.size(); i++)
        {
            const size_t at = InvertedPos(p, o, i);
            if (at == (size_t)-1)
                continue;
            char32_t mark = 0;
            for (size_t k = 0; k < p.Inverted.size(); k++)
                if (p.Inverted[k].first == o[i])
                    mark = p.Inverted[k].second;
            o.insert(o.begin() + at, mark);
            i++;
        }
    return o;
}

//-----------------------------------------------------------------------------
// [SECTION] Public API
//-----------------------------------------------------------------------------

std::string Typo::FixLine(const TypoProfile* profile, const TypoOptions& options, const std::string& line)
{
    bool blank = true;
    for (char c : line)
        if (c != ' ' && c != '\t')
            blank = false;
    if (blank)
        return line;
    if (!options.Markdown)
        return ToUtf8(FixSegment(*profile, options, ToText32(line)));
    // Code spans stay as they are
    std::string out, current;
    bool in_code = false;
    for (char c : line)
    {
        if (c == '`')
        {
            if (!in_code)
                out += ToUtf8(FixSegment(*profile, options, ToText32(current)));
            else
                out += current;
            out += '`';
            current.clear();
            in_code = !in_code;
        }
        else
            current += c;
    }
    out += in_code ? current : ToUtf8(FixSegment(*profile, options, ToText32(current)));
    return out;
}

std::string Typo::FixText(const TypoProfile* profile, const TypoOptions& options, const std::string& text)
{
    std::string out;
    bool in_fence = false;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        const size_t a = line.find_first_not_of(" \t");
        const bool fence = options.Markdown && a != std::string::npos && (line.compare(a, 3, "```") == 0 || line.compare(a, 3, "~~~") == 0);
        if (fence)
        {
            in_fence = !in_fence;
            out += line;
        }
        else
            out += in_fence ? line : FixLine(profile, options, line);
        if (eol < text.size())
            out += '\n';
        pos = eol + 1;
    }
    return out;
}

TypoEdit Typo::OnTyped(const TypoProfile* profile, const TypoOptions& options, const std::string& typed, const std::string& before)
{
    const TypoProfile& p = *profile;
    TypoEdit r;
    if (typed.size() != 1 || before.empty())
        return r;
    const char c = typed[0];
    // The last four code points of 'before' with their byte positions: 0 is the character just typed, 1 the one before it...
    struct Cp { char32_t cp; int pos; };
    std::vector<Cp> cps;
    for (int i = (int)before.size(); i > 0 && cps.size() < 4;)
    {
        int j = i - 1;
        while (j > 0 && ((unsigned char)before[j] & 0xC0) == 0x80)
            j--;
        cps.push_back({ ToText32(before.substr(j, i - j))[0], j });
        i = j;
    }
    const int end = (int)before.size();
    auto at = [&](size_t k) -> Cp { return k < cps.size() ? cps[k] : Cp{ 0, -1 }; };
    const Cp prev = at(1);

    if (!p.Inverted.empty() && (c == '?' || c == '!'))
    {
        const Text32 line = ToText32(before);
        const size_t at = InvertedPos(p, line, line.size() - 1);
        if (at != (size_t)-1)
        {
            char32_t mark = 0;
            for (size_t k = 0; k < p.Inverted.size(); k++)
                if (p.Inverted[k].first == (char32_t)c)
                    mark = p.Inverted[k].second;
            const int start = (int)ToUtf8(line.substr(0, at)).size();
            r.Valid = true;
            r.Start = start;
            r.Length = end - start;
            r.Text = ToUtf8(mark) + before.substr(start);
        }
        return r;
    }
    if (c == '"')
    {
        if (options.QuoteStyle == TypoQuoteStyle_Straight)
            return r;
        const bool open = prev.pos < 0 || p.OpenAfterTyped.Has(prev.cp);
        if (options.QuoteStyle == TypoQuoteStyle_Curly)
        {
            if (!p.AltOpen)
                return r;
            r.Valid = true;
            r.Start = end - 1;
            r.Length = 1;
            r.Text = ToUtf8(open ? p.AltOpen : p.AltClose);
            return r;
        }
        if (!p.QuoteOpen)
            return r;
        r.Valid = true;
        if (open)
        {
            r.Start = end - 1;
            r.Length = 1;
            r.Text = ToUtf8(p.QuoteOpen) + (p.GapOpen ? ToUtf8(p.GapOpen) : std::string());
        }
        else if (p.GapClose && (prev.cp == ' ' || prev.cp == C_NBSP || prev.cp == C_NNBSP))
        {
            r.Start = prev.pos;
            r.Length = end - prev.pos;
            r.Text = ToUtf8(p.GapClose) + ToUtf8(p.QuoteClose);
        }
        else
        {
            r.Start = end - 1;
            r.Length = 1;
            r.Text = (p.GapClose ? ToUtf8(p.GapClose) : std::string()) + ToUtf8(p.QuoteClose);
        }
        return r;
    }
    if (c == '\'')
    {
        if (!p.Apostrophe)
            return r;
        r.Valid = true;
        r.Start = end - 1;
        r.Length = 1;
        r.Text = ToUtf8(p.Apostrophe);
        return r;
    }
    if (c == '.')
    {
        if (p.Ellipsis && end >= 3 && before.compare(end - 3, 3, "...") == 0 && (end < 4 || before[end - 4] != '.'))
        {
            r.Valid = true;
            r.Start = end - 3;
            r.Length = 3;
            r.Text = ToUtf8(p.Ellipsis);
        }
        return r;
    }
    if (c == ' ')
    {
        // A space typed after "x;" "x!" "x?" "x:" : the space goes before the sign
        const Cp sign_cp = at(1), x = at(2);
        if (sign_cp.pos < 0 || x.pos < 0)
            return r;
        for (const TypoSign& s : p.SpaceBefore)
        {
            if (s.Sign != sign_cp.cp)
                continue;
            const std::string text = ToUtf8(s.Space) + ToUtf8(s.Sign);
            const int sign_bytes = (int)ToUtf8(s.Sign).size();
            if (p.PrevTyped.Has(x.cp))
            {
                r.Valid = true;
                r.Start = sign_cp.pos;
                r.Length = sign_bytes;
                r.Text = text;
                r.CaretExtra = 1;               // The typed space stays after the replaced text: the caret goes after it
            }
            else if (x.cp == ' ')
            {
                r.Valid = true;
                r.Start = x.pos;
                r.Length = 1 + sign_bytes;
                r.Text = text;
                r.CaretExtra = 1;
            }
            break;
        }
    }
    return r;
}
