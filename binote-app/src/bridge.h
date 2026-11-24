// Binote - test bridge: drives the running application from a script, like a user (mouse, keys, screenshots)
// Script: one command per line, '#' starts a comment.
//   wait N | move X Y | click X Y [count] | drag X1 Y1 X2 Y2 | type TEXT | key NAME [ctrl] [shift] [alt] | wheel DY
//   shot FILE.png | log TEXT | quit
// Any other command goes to the application (App::command): open, openfolder, save, doc, typo, toc, theme, set, status, dump...
// SPDX-License-Identifier: AGPL-3.0-or-later

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
