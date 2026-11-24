// Binote - minimal JSON: what the Python side of the scripts sends (objects, arrays, strings, numbers, booleans, null)
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#ifndef _JSON_H
#define _JSON_H

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace json
{
	struct Value
	{
		enum Type { Null, Bool, Number, String, Array, Object };
		Type type = Null;
		bool boolean = false;
		double number = 0.0;
		bool integer = false;											// a number written without a decimal part
		std::string string;
		std::vector<Value> items;										// array
		std::vector<std::pair<std::string, Value>> members;				// object

		const Value& get(const char* key) const
		{
			static const Value none;
			for (const auto& m : members)
				if (m.first == key)
					return m.second;
			return none;
		}
		std::string str(const char* key, const std::string& fallback = "") const { const Value& v = get(key); return v.type == String ? v.string : fallback; }
		bool flag(const char* key, bool fallback = false) const { const Value& v = get(key); return v.type == Bool ? v.boolean : fallback; }
	};

	namespace detail
	{
		struct Parser
		{
			const std::string& s;
			size_t p = 0;
			explicit Parser(const std::string& text) : s(text) {}

			void skip() { while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++; }

			static void put_utf8(std::string& out, unsigned int c)
			{
				if (c < 0x80) out += (char)c;
				else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
				else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
				else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
			}

			unsigned int hex4()
			{
				unsigned int c = (unsigned int)std::strtoul(s.substr(p, 4).c_str(), nullptr, 16);
				p += 4;
				return c;
			}

			bool string(std::string& out)
			{
				if (p >= s.size() || s[p] != '"')
					return false;
				p++;
				out.clear();
				while (p < s.size() && s[p] != '"')
				{
					if (s[p] != '\\')
					{
						out += s[p++];
						continue;
					}
					p++;
					if (p >= s.size())
						return false;
					const char e = s[p++];
					switch (e)
					{
					case 'n': out += '\n'; break;
					case 't': out += '\t'; break;
					case 'r': out += '\r'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'u':
					{
						unsigned int c = hex4();
						if (c >= 0xD800 && c < 0xDC00 && s.compare(p, 2, "\\u") == 0)		// surrogate pair
						{
							p += 2;
							const unsigned int d = hex4();
							c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
						}
						put_utf8(out, c);
						break;
					}
					default: out += e;
					}
				}
				p++;
				return true;
			}

			bool value(Value& v)
			{
				skip();
				if (p >= s.size())
					return false;
				const char c = s[p];
				if (c == '"')
				{
					v.type = Value::String;
					return string(v.string);
				}
				if (c == '{')
				{
					v.type = Value::Object;
					p++;
					for (;;)
					{
						skip();
						if (p < s.size() && s[p] == '}') { p++; return true; }
						if (p < s.size() && s[p] == ',') { p++; skip(); }
						std::string key;
						if (!string(key))
							return false;
						skip();
						if (p >= s.size() || s[p] != ':')
							return false;
						p++;
						Value member;
						if (!value(member))
							return false;
						v.members.push_back({ key, member });
					}
				}
				if (c == '[')
				{
					v.type = Value::Array;
					p++;
					for (;;)
					{
						skip();
						if (p < s.size() && s[p] == ']') { p++; return true; }
						if (p < s.size() && s[p] == ',') p++;
						Value item;
						if (!value(item))
							return false;
						v.items.push_back(item);
					}
				}
				if (s.compare(p, 4, "true") == 0) { v.type = Value::Bool; v.boolean = true; p += 4; return true; }
				if (s.compare(p, 5, "false") == 0) { v.type = Value::Bool; v.boolean = false; p += 5; return true; }
				if (s.compare(p, 4, "null") == 0) { v.type = Value::Null; p += 4; return true; }
				char* end = nullptr;
				v.number = std::strtod(s.c_str() + p, &end);
				if (end == s.c_str() + p)
					return false;
				v.type = Value::Number;
				v.integer = s.find_first_of(".eE", p) >= (size_t)(end - s.c_str());
				p = (size_t)(end - s.c_str());
				return true;
			}
		};
	}

	inline bool parse(const std::string& text, Value& out)
	{
		detail::Parser parser(text);
		return parser.value(out);
	}

	// Text of a JSON string (with the quotes) for a UTF-8 text
	inline std::string quote(const std::string& text)
	{
		std::string o = "\"";
		for (unsigned char c : text)
		{
			switch (c)
			{
			case '"': o += "\\\""; break;
			case '\\': o += "\\\\"; break;
			case '\n': o += "\\n"; break;
			case '\r': o += "\\r"; break;
			case '\t': o += "\\t"; break;
			default:
				if (c < 0x20)
				{
					char b[8];
					snprintf(b, sizeof(b), "\\u%04x", c);
					o += b;
				}
				else
					o += (char)c;
			}
		}
		return o + "\"";
	}
}

#endif
