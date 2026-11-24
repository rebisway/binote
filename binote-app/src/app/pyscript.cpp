// Binote - Python scripts
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "pyscript.h"
#include "json.h"
#include "util.h"
#include "../common/dialog.h"
#include "../common/logger.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <imgui.h>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>
#endif

// Stable ABI: one build works with any Python >= 3.12. _DEBUG is hidden from Python.h (it would ask for the debug libraries).
#ifdef _DEBUG
#	undef _DEBUG
#	define BINOTE_RESTORE_DEBUG
#endif
#ifndef Py_LIMITED_API
#	define Py_LIMITED_API 0x030C0000
#endif
#include <Python.h>
#ifdef BINOTE_RESTORE_DEBUG
#	define _DEBUG
#endif

namespace fs = std::filesystem;

namespace pyscript
{
	static std::string g_error;
	static bool g_initialized = false;
	static bool g_failed = false;
	static PyObject* g_textcore = nullptr;

	//-------------------------------------------------------------------------
	// Folders
	//-------------------------------------------------------------------------

	std::string user_folder()
	{
		const std::string folder = util::combine(util::data_path(), "scripts");
		std::error_code ec;
		fs::create_directories(fs::u8path(folder), ec);
		return folder;
	}

	// The folder delivered with the application: beside the executable, else up the tree (the executable may run from a build folder)
	static std::string delivered_folder()
	{
		const std::string app = util::application_path();
		const std::string candidates[] = { util::combine(app, "scripts"), util::combine(app, "../scripts"), util::combine(app, "../../binote-app/scripts"), util::combine(app, "../binote-app/scripts") };
		for (const std::string& c : candidates)
			if (util::exists(util::combine(util::combine(c, "lib"), "textcore.py")))
				return c;
		return "";
	}

	std::vector<std::string> folders()
	{
		std::vector<std::string> result;
		const std::string delivered = delivered_folder();
		if (!delivered.empty())
			result.push_back(delivered);
		result.push_back(user_folder());
		return result;
	}

	//-------------------------------------------------------------------------
	// Python
	//-------------------------------------------------------------------------

#ifdef _WIN32
	// The folder of a Python 3.12 or later: registry of the user then of the machine, else the PATH
	static std::wstring find_python_folder()
	{
		static const wchar_t* versions[] = { L"3.15", L"3.14", L"3.13", L"3.12" };
		static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
		for (const wchar_t* v : versions)
			for (HKEY root : roots)
			{
				const std::wstring key = std::wstring(L"SOFTWARE\\Python\\PythonCore\\") + v + L"\\InstallPath";
				wchar_t buf[MAX_PATH] = {};
				DWORD size = sizeof(buf) - sizeof(wchar_t);
				if (RegGetValueW(root, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS && buf[0])
				{
					fs::path p(buf);
					std::error_code ec;
					if (fs::exists(p / L"python3.dll", ec))
						return p.wstring();
				}
			}
		wchar_t found[MAX_PATH] = {};
		if (SearchPathW(nullptr, L"python3.dll", nullptr, MAX_PATH, found, nullptr))
			return fs::path(found).parent_path().wstring();
		return L"";
	}
#endif

	static bool fail(const std::string& message)
	{
		g_error = message;
		Logger::warning(message);
		return false;
	}

	static bool initialize()
	{
		if (g_initialized)
			return true;
		if (g_failed)
			return false;
		g_failed = true;
		const std::string delivered = delivered_folder();
		if (delivered.empty())
			return fail("Scripts folder not found (lib/textcore.py)");
#ifdef _WIN32
		const std::wstring folder = find_python_folder();
		if (folder.empty())
			return fail("Python 3.12 or later was not found");
		SetDllDirectoryW(folder.c_str());
		_wputenv_s(L"PYTHONHOME", folder.c_str());
		_wputenv_s(L"PYTHONUTF8", L"1");
		// python3.dll is loaded when the first Python function is called (delay load): check it can be
		if (!LoadLibraryW((fs::path(folder) / L"python3.dll").c_str()))
			return fail("python3.dll cannot be loaded");
#endif
		Py_Initialize();
		if (!Py_IsInitialized())
			return fail("Python failed to initialize");
		// The module of the core, and the folders where the scripts can import their own modules
		PyObject* path = PySys_GetObject("path");			// borrowed
		if (path)
			for (const std::string& dir : { util::combine(delivered, "lib"), user_folder(), delivered })
			{
				PyObject* s = PyUnicode_FromString(dir.c_str());
				PyList_Insert(path, 0, s);
				Py_XDECREF(s);
			}
		g_textcore = PyImport_ImportModule("textcore");
		if (!g_textcore)
		{
			PyErr_Clear();
			return fail("textcore.py cannot be loaded");
		}
		g_initialized = true;
		g_failed = false;
		Logger::log(std::string("Python ") + Py_GetVersion());
		return true;
	}

	bool available()
	{
		return initialize();
	}

	std::string version()
	{
		if (!initialize())
			return "";
		std::string v = Py_GetVersion();
		return v.substr(0, v.find(' '));
	}

	std::string last_error()
	{
		return g_error;
	}

	void finalize()
	{
		if (g_initialized)
		{
			Py_CLEAR(g_textcore);
			Py_Finalize();
		}
		g_initialized = false;
	}

	// Call a function of textcore.py which returns a text
	template <typename... Args>
	static std::string call(const char* function, const char* format, Args... args)
	{
		PyObject* r = PyObject_CallMethod(g_textcore, function, format, args...);
		if (!r)
		{
			PyErr_Clear();
			return "";
		}
		Py_ssize_t n = 0;
		const char* s = PyUnicode_AsUTF8AndSize(r, &n);
		std::string text = s ? std::string(s, (size_t)n) : std::string();
		Py_DECREF(r);
		return text;
	}

	std::vector<Info> list(const std::string& language)
	{
		std::vector<Info> scripts;
		if (!initialize())
			return scripts;
		std::string folders_json = "[";
		for (const std::string& f : folders())
			folders_json += std::string(folders_json.size() > 1 ? "," : "") + json::quote(f);
		folders_json += "]";
		json::Value root;
		if (!json::parse(call("list_scripts", "ss", folders_json.c_str(), language.c_str()), root))
			return scripts;
		const std::string user = user_folder();
		for (const json::Value& item : root.items)
		{
			Info info;
			info.path = item.str("path");
			info.name = item.str("name");
			info.description = item.str("description");
			info.category = item.str("category");
			info.error = item.str("error");
			info.user = info.path.compare(0, user.size(), user) == 0;
			// A script of the user replaces a delivered one with the same name
			auto same = std::find_if(scripts.begin(), scripts.end(), [&](const Info& i) { return i.name == info.name; });
			if (same != scripts.end())
				*same = info;
			else
				scripts.push_back(info);
		}
		std::sort(scripts.begin(), scripts.end(), [](const Info& a, const Info& b) { return a.category != b.category ? a.category < b.category : a.description < b.description; });
		return scripts;
	}

	std::string check_syntax(const std::string& source, const std::string& name, int& line, int& column)
	{
		line = column = 0;
		if (!initialize())
			return g_error;
		json::Value root;
		if (!json::parse(call("check_syntax", "ss", source.c_str(), name.c_str()), root))
			return "The check failed";
		if (root.flag("ok"))
			return "";
		line = (int)root.get("line").number;
		column = (int)root.get("column").number;
		const std::string error = root.str("error");
		return error.empty() ? "Syntax error" : error;
	}

	//-------------------------------------------------------------------------
	// Values of the properties kept from one run to the next (scripts.ini: module.key=value)
	//-------------------------------------------------------------------------

	static std::string settings_path()
	{
		return util::combine(util::data_path(), "scripts.ini");
	}

	static std::string escape_lines(const std::string& text)
	{
		std::string result;
		for (char c : text)
		{
			if (c == '\\')
				result += "\\\\";
			else if (c == '\n')
				result += "\\n";
			else if (c != '\r')
				result += c;
		}
		return result;
	}

	static std::string unescape_lines(const std::string& text)
	{
		std::string result;
		for (size_t i = 0; i < text.size(); i++)
		{
			if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == 'n' || text[i + 1] == '\\'))
			{
				result += text[i + 1] == 'n' ? '\n' : '\\';
				i++;
			}
			else
				result += text[i];
		}
		return result;
	}

	static std::map<std::string, std::string> read_settings()
	{
		std::map<std::string, std::string> values;
		std::ifstream f(fs::u8path(settings_path()), std::ios::binary);
		std::string line;
		while (std::getline(f, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			const size_t eq = line.find('=');
			if (eq != std::string::npos && eq > 0)
				values[line.substr(0, eq)] = unescape_lines(line.substr(eq + 1));
		}
		return values;
	}

	//-------------------------------------------------------------------------
	// Session: a script, its properties and its dialog box
	//-------------------------------------------------------------------------

	bool Session::begin(const std::string& path, const std::string& language, std::string& error)
	{
		_props.clear();
		_opened = false;
		if (!initialize())
		{
			error = g_error;
			return false;
		}
		_path = path;
		_module = fs::u8path(path).stem().u8string();
		json::Value root;
		if (!json::parse(call("begin", "ss", path.c_str(), language.c_str()), root))
		{
			error = "The script could not be loaded";
			return false;
		}
		if (!root.flag("ok"))
		{
			error = root.str("error");
			return false;
		}
		_title = root.str("description", _module);
		_use_properties = root.flag("use_properties", true);
		const auto saved = read_settings();
		for (const json::Value& p : root.get("properties").items)
		{
			Property prop;
			prop.key = p.str("key");
			prop.name = p.str("name");
			prop.multiline = p.flag("multiline");
			const json::Value& file = p.get("file");
			if (file.type == json::Value::Array && !file.items.empty())
			{
				prop.file = file.items[0].string == "save" ? 2 : 1;
				if (file.items.size() >= 3)
				{
					prop.filter_name = file.items[1].string;
					prop.filter_pattern = file.items[2].string;
				}
			}
			const json::Value& v = p.get("value");
			const auto it = saved.find(_module + "." + prop.key);
			const std::string* kept = it != saved.end() ? &it->second : nullptr;
			if (v.type == json::Value::Bool)
				prop.value = kept ? (*kept == "1" || *kept == "true") : v.boolean;
			else if (v.type == json::Value::Number && v.integer)
				prop.value = kept ? std::atoi(kept->c_str()) : (int)v.number;
			else if (v.type == json::Value::Number)
				prop.value = kept ? (float)std::atof(kept->c_str()) : (float)v.number;
			else if (v.type == json::Value::String)
				prop.value = kept ? *kept : v.string;
			else if (v.type == json::Value::Array)
			{
				// first item: index of the selected choice, the others are the choices
				std::vector<std::string> values;
				for (const json::Value& item : v.items)
					values.push_back(item.type == json::Value::String ? item.string : std::to_string((int)item.number));
				if (values.empty())
					values.push_back("0");
				if (kept)
					values[0] = *kept;
				prop.value = values;
			}
			else
				continue;
			_props.push_back(prop);
			apply((int)_props.size() - 1);
		}
		return true;
	}

	void Session::apply(int index)
	{
		const Value& v = _props[index].value;
		PyObject* value = nullptr;
		if (std::holds_alternative<bool>(v))
			value = PyLong_FromLong(std::get<bool>(v) ? 1 : 0);
		else if (std::holds_alternative<int>(v))
			value = PyLong_FromLong(std::get<int>(v));
		else if (std::holds_alternative<float>(v))
			value = PyFloat_FromDouble(std::get<float>(v));
		else if (std::holds_alternative<std::string>(v))
			value = PyUnicode_FromString(std::get<std::string>(v).c_str());
		else
			value = PyUnicode_FromString(std::get<std::vector<std::string>>(v)[0].c_str());
		json::Value root;
		if (json::parse(call("set_property", "iO", index, value), root) && !root.flag("ok"))
			Logger::error("Script " + _module + ": " + root.str("error"));
		Py_XDECREF(value);
	}

	void Session::end()
	{
		auto values = read_settings();
		for (const Property& p : _props)
		{
			const std::string key = _module + "." + p.key;
			if (std::holds_alternative<bool>(p.value))
				values[key] = std::get<bool>(p.value) ? "1" : "0";
			else if (std::holds_alternative<int>(p.value))
				values[key] = std::to_string(std::get<int>(p.value));
			else if (std::holds_alternative<float>(p.value))
				values[key] = std::to_string(std::get<float>(p.value));
			else if (std::holds_alternative<std::string>(p.value))
				values[key] = std::get<std::string>(p.value);
			else
				values[key] = std::get<std::vector<std::string>>(p.value)[0];
		}
		std::ofstream f(fs::u8path(settings_path()), std::ios::binary);
		for (const auto& kv : values)
			f << kv.first << "=" << escape_lines(kv.second) << "\n";
	}

	Result Session::run(const std::string& context)
	{
		Result result;
		for (int i = 0; i < (int)_props.size(); i++)
			apply(i);
		json::Value root;
		if (!json::parse(call("run", "s", context.c_str()), root))
		{
			result.error = "The script could not be run";
			return result;
		}
		result.ok = root.flag("ok");
		result.error = root.str("error");
		result.output = root.str("output");
		for (const json::Value& c : root.get("commands").items)
			result.commands.push_back({ c.str("op"), c.str("name"), c.str("text") });
		return result;
	}

	int Session::draw(const char* ok_text, const char* cancel_text, const char* preview_text, const std::function<void()>& extra)
	{
		int result = 0;
		const std::string popup = _title + "###script_properties";
		if (!_opened)
		{
			_edit = _props;
			ImGui::OpenPopup(popup.c_str());
			_opened = true;
		}
		ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
		if (!ImGui::BeginPopupModal(popup.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
			return result;
		if (ImGui::BeginTable("##properties", 2, ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("##c0", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("##c1", ImGuiTableColumnFlags_WidthStretch);
			for (int i = 0; i < (int)_edit.size(); i++)
			{
				Property& p = _edit[i];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(p.name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);
				const std::string id = "##property" + std::to_string(i);
				if (std::holds_alternative<bool>(p.value))
				{
					bool value = std::get<bool>(p.value);
					if (ImGui::Checkbox(id.c_str(), &value))
						p.value = value;
				}
				else if (std::holds_alternative<int>(p.value))
				{
					int value = std::get<int>(p.value);
					if (ImGui::InputInt(id.c_str(), &value))
						p.value = value;
				}
				else if (std::holds_alternative<float>(p.value))
				{
					float value = std::get<float>(p.value);
					if (ImGui::InputFloat(id.c_str(), &value))
						p.value = value;
				}
				else if (std::holds_alternative<std::string>(p.value))
				{
					std::string value = std::get<std::string>(p.value);
					if (p.multiline)
					{
						std::vector<char> buffer(4096, 0);
						snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
						if (ImGui::InputTextMultiline(id.c_str(), buffer.data(), buffer.size(), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4.0f + ImGui::GetStyle().FramePadding.y * 2.0f)))
							p.value = std::string(buffer.data());
					}
					else
					{
						std::vector<char> buffer(1024, 0);
						snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
						const float button = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
						if (p.file)
							ImGui::SetNextItemWidth(-(button + ImGui::GetStyle().ItemSpacing.x));
						if (ImGui::InputText(id.c_str(), buffer.data(), buffer.size()))
							p.value = std::string(buffer.data());
						if (p.file)
						{
							ImGui::SameLine();
							if (ImGui::Button(("...##file" + std::to_string(i)).c_str()))
							{
								std::string path = std::string(buffer.data());
								const bool chosen = p.file == 2 ? dialog::save_file_dialog(path, path, p.filter_name, p.filter_pattern) : dialog::open_file_dialog(path, path, p.filter_name, p.filter_pattern);
								if (chosen)
									p.value = path;
							}
						}
					}
				}
				else
				{
					std::vector<std::string> values = std::get<std::vector<std::string>>(p.value);
					std::string choices;
					for (size_t j = 1; j < values.size(); j++)
					{
						choices += values[j];
						choices += '\0';
					}
					int current = std::atoi(values[0].c_str());
					if (ImGui::Combo(id.c_str(), &current, choices.c_str()))
					{
						values[0] = std::to_string(current);
						p.value = values;
					}
				}
			}
			ImGui::EndTable();
		}
		if (extra)
			extra();
		ImGui::Spacing();
		const int buttons = preview_text ? 3 : 2;
		float width = std::max(ImGui::CalcTextSize(ok_text).x, ImGui::CalcTextSize(cancel_text).x);
		if (preview_text)
			width = std::max(width, ImGui::CalcTextSize(preview_text).x);
		width += ImGui::GetStyle().FramePadding.x * 4.0f;
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - (buttons * width + (buttons - 1) * ImGui::GetStyle().ItemSpacing.x)) * 0.5f);
		if (preview_text)
		{
			if (ImGui::Button(preview_text, ImVec2(width, 0.0f)))
			{
				_props = _edit;
				result = 3;
			}
			ImGui::SameLine();
		}
		if (ImGui::Button(ok_text, ImVec2(width, 0.0f)))
		{
			_props = _edit;
			result = 1;
		}
		ImGui::SameLine();
		if (ImGui::Button(cancel_text, ImVec2(width, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			result = 2;
		if (result == 1 || result == 2)
		{
			ImGui::CloseCurrentPopup();
			_opened = false;
		}
		ImGui::EndPopup();
		return result;
	}
}
