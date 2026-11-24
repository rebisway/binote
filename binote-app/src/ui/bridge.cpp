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

// Binote - test bridge

#include "ui/bridge.h"
#include "common/util.h"
#include <imgui.h>

// Key of ImGui from its name in a test script (a, F5, Enter...)
static ImGuiKey key_from_name(const std::string& n)
{
	if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') return (ImGuiKey)(ImGuiKey_A + (n[0] - 'a'));
	if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return (ImGuiKey)(ImGuiKey_0 + (n[0] - '0'));
	if (n == "enter") return ImGuiKey_Enter;
	if (n == "backspace") return ImGuiKey_Backspace;
	if (n == "delete") return ImGuiKey_Delete;
	if (n == "tab") return ImGuiKey_Tab;
	if (n == "escape") return ImGuiKey_Escape;
	if (n == "left") return ImGuiKey_LeftArrow;
	if (n == "right") return ImGuiKey_RightArrow;
	if (n == "up") return ImGuiKey_UpArrow;
	if (n == "down") return ImGuiKey_DownArrow;
	if (n == "home") return ImGuiKey_Home;
	if (n == "end") return ImGuiKey_End;
	if (n == "pageup") return ImGuiKey_PageUp;
	if (n == "pagedown") return ImGuiKey_PageDown;
	if (n == "space") return ImGuiKey_Space;
	if (n == "minus") return ImGuiKey_Minus;
	if (n == "equal") return ImGuiKey_Equal;
	if (n == "kpadd") return ImGuiKey_KeypadAdd;
	if (n == "kpsub") return ImGuiKey_KeypadSubtract;
	if (n.size() >= 2 && n[0] == 'f' && std::isdigit((unsigned char)n[1]))
		return (ImGuiKey)(ImGuiKey_F1 + (std::atoi(n.c_str() + 1) - 1));
	return ImGuiKey_None;
}

// Splits a line of a test script on spaces
static std::vector<std::string> words(const std::string& s)
{
	std::vector<std::string> w;
	size_t i = 0;
	while (i < s.size())
	{
		while (i < s.size() && s[i] == ' ') i++;
		size_t j = i;
		while (j < s.size() && s[j] != ' ') j++;
		if (j > i)
			w.push_back(s.substr(i, j - i));
		i = j;
	}
	return w;
}

// Reads a test script: one command per line, queued as actions played one per frame
bool Bridge::load(const std::string& path)
{
	std::string text;
	if (!util::read_text(path, text))
		return false;
	for (std::string line : util::split_lines(text))
	{
		while (!line.empty() && (line.back() == ' ' || line.back() == '\r'))
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		const size_t sp = line.find(' ');
		const std::string cmd = line.substr(0, sp), rest = sp == std::string::npos ? "" : line.substr(sp + 1);
		const std::vector<std::string> w = words(rest);
		auto num = [&](size_t i) { return i < w.size() ? (float)std::atof(w[i].c_str()) : 0.0f; };

		if (cmd == "wait")
			for (int k = 0; k < std::atoi(rest.c_str()); k++)
				_actions.push_back({ nullptr, nullptr });
		else if (cmd == "move")
		{
			const float x = num(0), y = num(1);
			_actions.push_back({ [this, x, y](ImGuiIO&) { _mouse_scripted = true; _mouse_x = x; _mouse_y = y; }, nullptr });
		}
		else if (cmd == "click")
		{
			const float x = num(0), y = num(1);
			const int count = w.size() > 2 ? std::atoi(w[2].c_str()) : 1;
			for (int k = 0; k < count; k++)
			{
				_actions.push_back({ [this, x, y](ImGuiIO& io) { _mouse_scripted = true; _mouse_x = x; _mouse_y = y; io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, true); }, nullptr });
				_actions.push_back({ [](ImGuiIO& io) { io.AddMouseButtonEvent(0, false); }, nullptr });
			}
			_actions.push_back({ nullptr, nullptr });
		}
		else if (cmd == "rclick")
		{
			const float x = num(0), y = num(1);
			_actions.push_back({ [this, x, y](ImGuiIO& io) { _mouse_scripted = true; _mouse_x = x; _mouse_y = y; io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(1, true); }, nullptr });
			_actions.push_back({ [](ImGuiIO& io) { io.AddMouseButtonEvent(1, false); }, nullptr });
			_actions.push_back({ nullptr, nullptr });
		}
		else if (cmd == "drag")
		{
			const float x1 = num(0), y1 = num(1), x2 = num(2), y2 = num(3);
			_actions.push_back({ [this, x1, y1](ImGuiIO& io) { _mouse_scripted = true; _mouse_x = x1; _mouse_y = y1; io.AddMousePosEvent(x1, y1); io.AddMouseButtonEvent(0, true); }, nullptr });
			_actions.push_back({ [this, x2, y2](ImGuiIO& io) { _mouse_x = x2; _mouse_y = y2; io.AddMousePosEvent(x2, y2); }, nullptr });
			_actions.push_back({ [](ImGuiIO& io) { io.AddMouseButtonEvent(0, false); }, nullptr });
			_actions.push_back({ nullptr, nullptr });
		}
		else if (cmd == "type")
			_actions.push_back({ [rest](ImGuiIO& io) { io.AddInputCharactersUTF8(rest.c_str()); }, nullptr });
		else if (cmd == "key" && !w.empty())
		{
			const ImGuiKey key = key_from_name(w[0]);
			bool ctrl = false, shift = false, alt = false;
			for (size_t k = 1; k < w.size(); k++)
			{
				ctrl |= w[k] == "ctrl";
				shift |= w[k] == "shift";
				alt |= w[k] == "alt";
			}
			_actions.push_back({ [=](ImGuiIO& io) {
				if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
				if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
				if (alt) io.AddKeyEvent(ImGuiMod_Alt, true);
				io.AddKeyEvent(key, true);
			}, nullptr });
			_actions.push_back({ [=](ImGuiIO& io) {
				io.AddKeyEvent(key, false);
				if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
				if (shift) io.AddKeyEvent(ImGuiMod_Shift, false);
				if (alt) io.AddKeyEvent(ImGuiMod_Alt, false);
			}, nullptr });
		}
		else if (cmd == "wheel")
		{
			const float dy = num(0);
			_actions.push_back({ [dy](ImGuiIO& io) { io.AddMouseWheelEvent(0.0f, dy); }, nullptr });
		}
		else if (cmd == "shot")
			_actions.push_back({ nullptr, [this, rest]() { if (screenshot) screenshot(rest); } });
		else if (cmd == "log")
			_actions.push_back({ nullptr, [rest]() { printf("%s\n", rest.c_str()); fflush(stdout); } });
		else if (cmd == "quit")
			_actions.push_back({ nullptr, [this]() { _quit = true; } });
		else
		{
			// Command of the application; the words keep their case, a path may hold spaces: the rest of the line after the command is split on spaces
			// except when it is quoted ("a b")
			std::vector<std::string> args{ cmd };
			size_t i = 0;
			while (i < rest.size())
			{
				while (i < rest.size() && rest[i] == ' ') i++;
				if (i >= rest.size()) break;
				if (rest[i] == '"')
				{
					size_t j = rest.find('"', i + 1);
					if (j == std::string::npos) j = rest.size();
					args.push_back(rest.substr(i + 1, j - i - 1));
					i = j + 1;
				}
				else
				{
					size_t j = rest.find(' ', i);
					if (j == std::string::npos) j = rest.size();
					args.push_back(rest.substr(i, j - i));
					i = j;
				}
			}
			_actions.push_back({ [this, args](ImGuiIO&) { if (application && !application(args)) printf("unknown command: %s\n", args[0].c_str()); }, nullptr });
		}
	}
	_started = true;
	return true;
}

// Before the frame: takes the next action and injects its input events
void Bridge::pre_frame(ImGuiIO& io)
{
	_current = Action{ nullptr, nullptr };
	if (!_actions.empty())
	{
		_current = _actions.front();
		_actions.pop_front();
	}
	if (_mouse_scripted)
		io.AddMousePosEvent(_mouse_x, _mouse_y);
	if (_current.pre)
		_current.pre(io);
}

// After the frame: runs the end of the action (screenshot, check)
void Bridge::post_frame()
{
	if (_current.post)
		_current.post();
}
