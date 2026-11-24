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

// Binote - entry point: window, fonts, main loop

#include "ui/app.h"
#include "common/util.h"
#include "ui/bridge.h"
#include "common/logger.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <stdio.h>
#include <string>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")
#endif
#include <GL/gl.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

// Reads the framebuffer and writes a PNG (test bridge)
static void save_screenshot(GLFWwindow* window, const std::string& path)
{
	int w, h;
	glfwGetFramebufferSize(window, &w, &h);
	std::vector<unsigned char> pixels((size_t)w * h * 4);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	stbi_flip_vertically_on_write(1);
	stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4);
}

// The file of a font choice: a full path, or a file of the fonts folder; the default when it does not exist
static std::string resolve_font(const std::string& choice, const char* fallback)
{
	const std::string dir = util::combine(util::application_path(), "fonts");
	if (!choice.empty())
	{
		if (util::exists(choice))
			return choice;
		if (util::exists(util::combine(dir, choice)))
			return util::combine(dir, choice);
	}
	return util::combine(dir, fallback);
}

// Fonts for Chinese, Japanese and Korean, merged behind the others (the atlas is dynamic: a glyph is built when first drawn).
// The choice, or the ones of the system when it is empty; "none" turns them off.
static std::vector<std::string> cjk_fonts(const std::string& choice)
{
	std::vector<std::string> files;
	if (choice == "none")
		return files;
	if (!choice.empty())
	{
		const std::string file = resolve_font(choice, "");
		if (util::exists(file) && file != util::combine(util::combine(util::application_path(), "fonts"), ""))
			files.push_back(file);
		return files;
	}
	static const char* const candidates[] = {
#ifdef _WIN32
		"C:/Windows/Fonts/YuGothR.ttc", "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/simsun.ttc",
#else
		"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
#endif
		nullptr };
	for (const char* const* c = candidates; *c; c++)
		if (util::exists(*c))
			files.push_back(*c);
	return files;
}

// Bold, italic... of a font, when the files sit beside it with the usual names (Name-Regular.ttf, Name-Bold.ttf, Name-Oblique.ttf...)
static std::string font_variant(const std::string& regular, const char* const* suffixes)
{
	const size_t dot = regular.find_last_of('.');
	std::string stem = regular.substr(0, dot);
	const std::string ext = regular.substr(dot);
	for (const char* tail : { "-Regular", "-Medium", "Regular", "" })
	{
		const size_t n = strlen(tail);
		if (n == 0 || (stem.size() >= n && stem.compare(stem.size() - n, n, tail) == 0))
		{
			const std::string base = stem.substr(0, stem.size() - n);
			for (const char* const* s = suffixes; *s; s++)
				if (util::exists(base + *s + ext))
					return base + *s + ext;
		}
	}
	return "";
}

// Fonts of the interface and of the editors. DejaVu is merged in each of them: it only adds the glyphs the chosen font lacks
// (narrow no-break space, arrows, box drawing, the icons of the toolbar).
static Fonts load_fonts(const util::Settings& s)
{
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();
	static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x2000, 0x206F, 0x20AC, 0x20AC, 0x2116, 0x2116, 0x2190, 0x21FF, 0x2200, 0x22FF, 0x2300, 0x23FF, 0x2500, 0x257F, 0x2580, 0x259F, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0x2700, 0x27BF, 0 };
	const std::string dir = util::combine(util::application_path(), "fonts");
	const std::string dejavu = util::combine(dir, "DejaVuSans.ttf"), dejavu_mono = util::combine(dir, "DejaVuSansMono.ttf");

	const std::vector<std::string> cjk_list = cjk_fonts(s.cjk_font);
	auto add = [&](const std::string& file, float px, const std::string& fallback_glyphs) -> ImFont* {
		if (file.empty() || !util::exists(file))
			return nullptr;
		ImFontConfig cfg;
		cfg.OversampleH = 2;
		ImFont* font = io.Fonts->AddFontFromFileTTF(file.c_str(), px, &cfg, ranges);
		if (font && util::exists(fallback_glyphs) && fallback_glyphs != file)
		{
			ImFontConfig merge;
			merge.MergeMode = true;
			merge.OversampleH = 2;
			io.Fonts->AddFontFromFileTTF(fallback_glyphs.c_str(), px * 0.95f, &merge, ranges);
		}
		if (font)
			for (const std::string& cjk : cjk_list)
			{
				ImFontConfig merge;
				merge.MergeMode = true;
				io.Fonts->AddFontFromFileTTF(cjk.c_str(), px, &merge);
			}
		if (!font)
			Logger::error("Cannot load font: " + file);
		return font;
	};

	static const char* const bold_names[] = { "-Bold", "Bold", "b", "bd", nullptr };
	static const char* const italic_names[] = { "-Italic", "-Oblique", "Italic", "i", nullptr };
	static const char* const bold_italic_names[] = { "-BoldItalic", "-BoldOblique", "BoldItalic", "z", "bi", nullptr };

	const float size = s.font_size;
	Fonts f;
	f.ui = add(resolve_font(s.ui_font, "Roboto-Medium.ttf"), s.ui_font_size, dejavu);
	const std::string text = resolve_font(s.editor_font, "DejaVuSans.ttf");
	const std::string bold = font_variant(text, bold_names);
	f.regular = add(text, size, dejavu);
	f.bold = add(bold.empty() ? util::combine(dir, "DejaVuSans-Bold.ttf") : bold, size, dejavu);
	f.italic = add(font_variant(text, italic_names).empty() ? util::combine(dir, "DejaVuSans-Oblique.ttf") : font_variant(text, italic_names), size, dejavu);
	f.bold_italic = add(font_variant(text, bold_italic_names).empty() ? util::combine(dir, "DejaVuSans-BoldOblique.ttf") : font_variant(text, bold_italic_names), size, dejavu);
	f.mono = add(resolve_font(s.mono_font, "DejaVuSansMono.ttf"), size * 0.92f, dejavu_mono);
	const std::string heading = bold.empty() ? util::combine(dir, "DejaVuSans-Bold.ttf") : bold;
	f.heading[0] = add(heading, size * 1.7f, dejavu);
	f.heading[1] = add(heading, size * 1.45f, dejavu);
	f.heading[2] = add(heading, size * 1.25f, dejavu);
	if (!s.charmap_font.empty())
	{
		// The table of characters shows the font as it is (no fallback glyphs), with the blocks it displays
		static const ImWchar charmap_ranges[] = { 0x0020, 0x052F, 0x1E00, 0x1EFF, 0x2000, 0x2BFF, 0xFEFF, 0xFEFF, 0 };
		const std::string file = resolve_font(s.charmap_font, "DejaVuSans.ttf");
		ImFontConfig cfg;
		cfg.OversampleH = 2;
		if (util::exists(file))
			f.charmap = io.Fonts->AddFontFromFileTTF(file.c_str(), 22.0f, &cfg, charmap_ranges);
		if (!f.charmap)
			Logger::error("Cannot load font: " + file);
	}
	if (!f.ui)
		f.ui = f.regular;
	if (f.ui)
		io.FontDefault = f.ui;
	io.Fonts->Build();
	return f;
}


// Position of the input method windows (Chinese, Japanese, Korean typing): ImGui gives the place of the caret, the system draws the candidates there
static void set_ime_data(ImGuiContext*, ImGuiViewport* viewport, ImGuiPlatformImeData* data)
{
#ifdef _WIN32
	HWND hwnd = (HWND)viewport->PlatformHandleRaw;
	if (!hwnd)
		return;
	if (HIMC himc = ImmGetContext(hwnd))
	{
		if (data->WantVisible)
		{
			COMPOSITIONFORM composition = {};
			composition.dwStyle = CFS_FORCE_POSITION;
			composition.ptCurrentPos.x = (LONG)data->InputPos.x;
			composition.ptCurrentPos.y = (LONG)data->InputPos.y;
			ImmSetCompositionWindow(himc, &composition);
			CANDIDATEFORM candidate = {};
			candidate.dwStyle = CFS_CANDIDATEPOS;
			candidate.ptCurrentPos.x = (LONG)data->InputPos.x;
			candidate.ptCurrentPos.y = (LONG)(data->InputPos.y + data->InputLineHeight);
			ImmSetCandidateWindow(himc, &candidate);
		}
		ImmReleaseContext(hwnd, himc);
	}
#else
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
	// GLFW 3.4 (X11 and Wayland): the candidate window follows the caret. With 3.3 the system places it by itself.
	GLFWwindow* window = (GLFWwindow*)viewport->PlatformHandle;
	if (window && data->WantVisible)
		glfwSetPreeditCursorRectangle(window, (int)data->InputPos.x, (int)data->InputPos.y, 1, (int)data->InputLineHeight);
#else
	(void)viewport; (void)data;
#endif
#endif
}
// Window, fonts (with fallback and symbols), the application and the main loop
int main(int argc, char** argv)
{
	std::string open_path, script_path;
	int win_w = 0, win_h = 0;
	for (int n = 1; n < argc; n++)
	{
		const std::string a = argv[n];
		if (a == "--script" && n + 1 < argc) script_path = argv[++n];
		else if (a == "--width" && n + 1 < argc) win_w = std::atoi(argv[++n]);
		else if (a == "--height" && n + 1 < argc) win_h = std::atoi(argv[++n]);
		else open_path = a;
	}

	Logger::start(util::combine(util::data_path(), "log.txt"));
	if (!glfwInit())
		return 1;
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	util::Settings initial;
	initial.load();
	GLFWwindow* window = glfwCreateWindow(win_w > 0 ? win_w : initial.window_w, win_h > 0 ? win_h : initial.window_h, "Binote", nullptr, nullptr);
	if (!window)
		return 1;
	// Position and state of the last session (kept only if the position is on a screen)
	if (initial.window_x > -30000)
	{
		bool visible = false;
		int count = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&count);
		for (int i = 0; i < count; i++)
		{
			int mx, my, mw, mh;
			glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
			if (initial.window_x + 100 > mx && initial.window_x < mx + mw - 100 && initial.window_y >= my - 10 && initial.window_y < my + mh - 100)
				visible = true;
		}
		if (visible)
			glfwSetWindowPos(window, initial.window_x, initial.window_y);
	}
	if (initial.maximized && win_w == 0 && win_h == 0)
		glfwMaximizeWindow(window);

	// Icon of the window (on Windows the one of the executable comes from binote.rc)
	{
		std::vector<unsigned char*> pixels;
		std::vector<GLFWimage> images;
		for (int size : { 16, 32, 48, 256 })
		{
			int w, h, n;
			const std::string file = util::combine(util::combine(util::application_path(), "icons"), "binote-" + std::to_string(size) + ".png");
			unsigned char* data = stbi_load(file.c_str(), &w, &h, &n, 4);
			if (data)
			{
				pixels.push_back(data);
				images.push_back({ w, h, data });
			}
		}
		if (!images.empty())
			glfwSetWindowIcon(window, (int)images.size(), images.data());
		for (unsigned char* p : pixels)
			stbi_image_free(p);
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	const std::string ini = util::combine(util::data_path(), "imgui.ini");
	static std::string ini_path = ini;
	io.IniFilename = ini_path.c_str();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui::GetPlatformIO().Platform_SetImeDataFn = set_ime_data;
	ImGui_ImplOpenGL3_Init("#version 130");

	{
		App app(window);
		app.set_fonts(load_fonts(app.settings()));
		app.font_size_changed();

		Bridge bridge;
		const bool scripted = !script_path.empty();
		if (scripted)
		{
			bridge.screenshot = [window](const std::string& p) { save_screenshot(window, p); };
			bridge.application = [&app](const std::vector<std::string>& a) { return app.command(a); };
			if (!bridge.load(script_path))
			{
				printf("script not found: %s\n", script_path.c_str());
				return 1;
			}
		}

		bool first = true;
		while (!glfwWindowShouldClose(window) && !(scripted && bridge.quit_requested()))
		{
			glfwPollEvents();
			if (app.font_size_changed())
			{
				app.set_fonts(load_fonts(app.settings()));
			}
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			if (scripted)
				bridge.pre_frame(io);
			ImGui::NewFrame();
			if (first)
			{
				first = false;
				if (!open_path.empty())
				{
					app.open_from_system(open_path);
				}
				else if (!scripted && app.settings().reopen_last && !app.settings().recents.empty())
				{
					const util::Recent r = app.settings().recents[0];
					if (util::is_directory(r.source)) app.command({ "openfolder", r.source });
					else if (util::exists(r.source)) app.command({ "open", r.source, r.destination });
				}
			}
			app.frame();
			if (!scripted && !glfwGetWindowAttrib(window, GLFW_MAXIMIZED) && !glfwGetWindowAttrib(window, GLFW_ICONIFIED))
			{
				glfwGetWindowPos(window, &app.settings().window_x, &app.settings().window_y);
				glfwGetWindowSize(window, &app.settings().window_w, &app.settings().window_h);
			}
			ImGui::Render();
			int fb_w, fb_h;
			glfwGetFramebufferSize(window, &fb_w, &fb_h);
			glViewport(0, 0, fb_w, fb_h);
			glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			if (scripted)
			{
				bridge.post_frame();
				if (bridge.finished())
					break;
			}
			glfwSwapBuffers(window);

			if (app.quit_requested())
				glfwSetWindowShouldClose(window, GLFW_TRUE);		// "Save" or "Don't save" was chosen in the box shown when quitting
			else if (glfwWindowShouldClose(window) && !scripted && !app.can_close())
				glfwSetWindowShouldClose(window, GLFW_FALSE);
		}
		if (!scripted)
		{
			app.settings().maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
			app.save_state();
		}
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
	Logger::stop();
	return 0;
}
