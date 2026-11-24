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

// Binote - test bridge: drives the running application from a script, like a user (mouse, keys, screenshots)
// Script: one command per line, '#' starts a comment.
//   wait N | move X Y | click X Y [count] | drag X1 Y1 X2 Y2 | type TEXT | key NAME [ctrl] [shift] [alt] | wheel DY
//   shot FILE.png | log TEXT | quit
// Any other command goes to the application (App::command): open, openfolder, save, doc, typo, toc, theme, set, status, dump...

#pragma once
#ifndef _BRIDGE_H
#define _BRIDGE_H

#include <deque>
#include <functional>
#include <string>
#include <vector>

struct ImGuiIO;

class Bridge
{
public:
	std::function<void(const std::string&)> screenshot;
	std::function<bool(const std::vector<std::string>&)> application;

	bool load(const std::string& path);
	// Before ImGui::NewFrame, after the platform backend read the real mouse
	void pre_frame(ImGuiIO& io);
	// After the frame was drawn, before the buffers are swapped
	void post_frame();
	bool finished() const { return _actions.empty() && _started; }
	bool quit_requested() const { return _quit; }

private:
	struct Action
	{
		std::function<void(ImGuiIO&)> pre;
		std::function<void()> post;
	};
	std::deque<Action> _actions;
	Action _current;
	bool _started = false;
	bool _mouse_scripted = false;
	float _mouse_x = 0.0f, _mouse_y = 0.0f;
	bool _quit = false;
};

#endif
