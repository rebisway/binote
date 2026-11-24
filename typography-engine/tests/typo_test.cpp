// typo - www.rebisway.com - contact@rebisway.com
// Copyright (c) 2026 Thomas Gourgnier
// SPDX-License-Identifier: MIT
//
// Licensed under the MIT license: see the LICENSE file in the folder of this component.

// Tests of typo: the cases of tests/cases/<profile>.txt, and a few checks of the profiles themselves.
//
//   typo_test <folder of the profiles> <folder of the cases>
//
// A case is one line of a file of cases. Escapes: \uXXXX for a code point, \\ for a backslash. Lines starting with # and blank lines are ignored.
//   fix:   input => expected          a line fixed as plain text, local quotation marks
//   fixmd: input => expected          the same as Markdown (code spans are left alone)
//   fix1:  input => expected          the same with the straight quotes kept
//   fix2:  input => expected          the same with the curly quotes
//   simp:  input => expected          the opposite: plain quotes, apostrophes, dots and spaces
//   type:  typed text => expected     the text typed one character at a time: each character goes through OnTyped, the edits are applied
//   type2: typed text => expected     the same with the curly quotes
// A file of cases begins with "profile: <name>" (the profile it is run with).

#include "typo.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0, g_checks = 0;

static std::string Slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void AppendUtf8(std::string& o, unsigned c)
{
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
}

// " " and "\\" in a case
static std::string Unescape(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'u' && i + 5 < s.size() + 0)
        {
            AppendUtf8(o, (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16));
            i += 5;
        }
        else if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\\')
        {
            o += '\\';
            i++;
        }
        else
            o += s[i];
    }
    return o;
}

// The same text with the characters which are hard to see written as \uXXXX, for the messages
static std::string Show(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xA0) { o += "\\u00a0"; i += 2; }
        else if (c == 0xE2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0xAF) { o += "\\u202f"; i += 3; }
        else { o += (char)c; i++; }
    }
    return o;
}

static void Check(bool ok, const std::string& what, const std::string& got, const std::string& expected)
{
    g_checks++;
    if (ok)
        return;
    g_failures++;
    printf("FAIL %s\n   got      [%s]\n   expected [%s]\n", what.c_str(), Show(got).c_str(), Show(expected).c_str());
}

// Types a text one character at a time in an empty line, as the editor does: the character is inserted at the caret, OnTyped is asked, its edit applied
static std::string TypeText(const TypoProfile* profile, const TypoOptions& options, const std::string& text)
{
    std::string line;
    size_t caret = 0;
    for (size_t i = 0; i < text.size();)
    {
        const unsigned char c = (unsigned char)text[i];
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        const std::string typed = text.substr(i, n);
        i += n;
        line.insert(caret, typed);
        caret += typed.size();
        const TypoEdit e = Typo::OnTyped(profile, options, typed, line.substr(0, caret));
        if (e.Valid)
        {
            line.replace((size_t)e.Start, (size_t)e.Length, e.Text);
            caret = (size_t)e.Start + e.Text.size() + (size_t)e.CaretExtra;
        }
    }
    return line;
}

static void RunCases(const std::string& profile_dir, const std::string& file)
{
    std::ifstream f(file);
    std::string line, name;
    TypoProfile* profile = nullptr;
    int number = 0;
    while (std::getline(f, line))
    {
        number++;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        if (line.compare(0, 8, "profile:") == 0)
        {
            name = line.substr(8);
            name.erase(0, name.find_first_not_of(' '));
            std::string error;
            TypoProfile* parent = nullptr;
            // "extends" is looked up in the same folder
            std::string text = Slurp(profile_dir + "/" + name + ".typo");
            const size_t at = text.find("extends");
            if (at != std::string::npos)
            {
                std::string code = text.substr(text.find('=', at) + 1);
                code = code.substr(0, code.find('\n'));
                code.erase(0, code.find_first_not_of(" \t"));
                while (!code.empty() && (code.back() == ' ' || code.back() == '\r'))
                    code.pop_back();
                parent = Typo::CreateProfile(Slurp(profile_dir + "/" + code + ".typo").c_str(), nullptr, &error);
            }
            profile = Typo::CreateProfile(text.c_str(), parent, &error);
            if (!profile)
            {
                printf("FAIL profile %s: %s\n", name.c_str(), error.c_str());
                g_failures++;
                return;
            }
            continue;
        }
        const size_t colon = line.find(':'), arrow = line.find(" => ");
        if (colon == std::string::npos || arrow == std::string::npos || !profile)
        {
            printf("FAIL %s:%d: bad case\n", file.c_str(), number);
            g_failures++;
            continue;
        }
        const std::string kind = line.substr(0, colon);
        std::string input = Unescape(line.substr(colon + 1, arrow - colon - 1));
        input.erase(0, input.find_first_not_of(' '));
        const std::string expected = Unescape(line.substr(arrow + 4));
        TypoOptions options;
        std::string got;
        if (kind == "fix" || kind == "fix1" || kind == "fix2" || kind == "fixmd")
        {
            options.QuoteStyle = kind == "fix1" ? TypoQuoteStyle_Straight : kind == "fix2" ? TypoQuoteStyle_Curly : TypoQuoteStyle_Local;
            options.Markdown = kind == "fixmd";
            got = Typo::FixLine(profile, options, input);
            // Fixing is idempotent: a fixed line does not change any more
            Check(Typo::FixLine(profile, options, got) == got, name + " idempotent " + file + ":" + std::to_string(number), Typo::FixLine(profile, options, got), got);
        }
        else if (kind == "simp" || kind == "simpmd")
        {
            options.Markdown = kind == "simpmd";
            got = Typo::SimplifyLine(profile, options, input);
        }
        else if (kind == "type" || kind == "type2")
        {
            options.QuoteStyle = kind == "type2" ? TypoQuoteStyle_Curly : TypoQuoteStyle_Local;
            got = TypeText(profile, options, input);
        }
        else
        {
            printf("FAIL %s:%d: unknown kind %s\n", file.c_str(), number, kind.c_str());
            g_failures++;
            continue;
        }
        Check(got == expected, name + " " + kind + " " + file + ":" + std::to_string(number), got, expected);
    }
}

// The profile format itself
static void TestProfiles()
{
    std::string error;
    TypoProfile* p = Typo::CreateProfile("[profile]\nname = T\n[quotes]\nopen = «\nclose = »\n[space_before]\n; = U+202F\n", nullptr, &error);
    Check(p != nullptr, "a small profile loads", error, "");
    if (p)
    {
        Check(std::string(Typo::GetName(p)) == "T", "name", Typo::GetName(p), "T");
        Check(Typo::WatchesChar(p, ';') && Typo::WatchesChar(p, '"') && !Typo::WatchesChar(p, '!'), "WatchesChar", "", "");
        // a profile which extends another one starts from its settings
        TypoProfile* child = Typo::CreateProfile("[profile]\nname = C\nextends = T\n[space_before]\n! = U+00A0\n", p, &error);
        Check(child && Typo::WatchesChar(child, '!') && !Typo::WatchesChar(child, ';') && Typo::WatchesChar(child, '"'), "extends: the table of signs is replaced, the rest is inherited", "", "");
        Typo::DestroyProfile(child);
        Typo::DestroyProfile(p);
    }
    TypoProfile* bad = Typo::CreateProfile("[quotes]\nopen = U+ZZ\n", nullptr, &error);
    Check(bad == nullptr && error.find("line 2") == 0, "an error names the line", error, "line 2: ...");
    bad = Typo::CreateProfile("[quotes]\nunknown_key = 1\n", nullptr, &error);
    Check(bad == nullptr && error.find("unknown key") != std::string::npos, "an unknown key is an error", error, "unknown key");
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printf("usage: typo_test <folder of the profiles> <folder of the cases>\n");
        return 2;
    }
    TestProfiles();
    for (const char* name : { "fr", "generic", "en", "es", "de", "it", "pt", "pl" })
        RunCases(argv[1], std::string(argv[2]) + "/" + name + ".txt");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
