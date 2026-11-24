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

// Binote - version control

#include "vcs/vcs.h"
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>
#else
#	include <cstdlib>
#endif

namespace fs = std::filesystem;

namespace vcs
{
#ifdef _WIN32
	// UTF-8 to UTF-16 for CreateProcess
	static std::wstring widen(const std::string& s)
	{
		if (s.empty())
			return L"";
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w((size_t)n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
		return w;
	}

	// Quotes an argument of a command line the way CommandLineToArgvW reads it
	static std::wstring quote_arg(const std::wstring& a)
	{
		if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos)
			return a;
		std::wstring o = L"\"";
		size_t slashes = 0;
		for (wchar_t c : a)
		{
			if (c == L'\\')
				slashes++;
			else if (c == L'"')
			{
				o.append(slashes * 2 + 1, L'\\');
				o += L'"';
				slashes = 0;
			}
			else
			{
				o.append(slashes, L'\\');
				slashes = 0;
				o += c;
			}
		}
		o.append(slashes * 2, L'\\');
		return o + L"\"";
	}
#endif

	// True when the output of a command is valid UTF-8 (else it is read as the system code page)
	static bool valid_utf8(const std::string& s)
	{
		for (size_t i = 0; i < s.size();)
		{
			const unsigned char c = (unsigned char)s[i];
			const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
			if (n == 0 || i + (size_t)n > s.size())
				return false;
			for (int k = 1; k < n; k++)
				if (((unsigned char)s[i + (size_t)k] & 0xC0) != 0x80)
					return false;
			i += (size_t)n;
		}
		return true;
	}

	// Runs a command in a folder without a window and collects its output; false when it cannot start or fails
	bool run(const std::string& folder, const std::vector<std::string>& args, std::string& output)
	{
		output.clear();
#ifdef _WIN32
		std::wstring cmd;
		for (const std::string& a : args)
			cmd += (cmd.empty() ? L"" : L" ") + quote_arg(widen(a));
		SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
		HANDLE read_end = nullptr, write_end = nullptr;
		if (!CreatePipe(&read_end, &write_end, &sa, 0))
			return false;
		SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = write_end;
		si.hStdError = write_end;
		si.hStdInput = INVALID_HANDLE_VALUE;
		PROCESS_INFORMATION pi = {};
		const std::wstring dir = widen(folder);
		std::vector<wchar_t> buf(cmd.begin(), cmd.end());
		buf.push_back(0);
		// GIT_TERMINAL_PROMPT=0: never wait for a password in a window which does not exist
		SetEnvironmentVariableW(L"GIT_TERMINAL_PROMPT", L"0");
		// Subversion writes in the Windows code page (it turns names into "?" under the C locale): converted below
		const bool is_svn = !args.empty() && args[0] == "svn";
		SetEnvironmentVariableW(L"LC_ALL", is_svn ? nullptr : L"C.UTF-8");
		const BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi);
		CloseHandle(write_end);
		if (!ok)
		{
			CloseHandle(read_end);
			output = "Cannot start " + (args.empty() ? std::string() : args[0]);
			return false;
		}
		char chunk[4096];
		DWORD n;
		while (ReadFile(read_end, chunk, sizeof(chunk), &n, nullptr) && n > 0)
			output.append(chunk, n);
		CloseHandle(read_end);
		if (is_svn && !valid_utf8(output))
		{
			const int wn = MultiByteToWideChar(CP_ACP, 0, output.data(), (int)output.size(), nullptr, 0);
			std::wstring w((size_t)wn, L' ');
			MultiByteToWideChar(CP_ACP, 0, output.data(), (int)output.size(), &w[0], wn);
			const int un = WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
			output.assign((size_t)un, ' ');
			WideCharToMultiByte(CP_UTF8, 0, w.data(), wn, &output[0], un, nullptr, nullptr);
		}
		WaitForSingleObject(pi.hProcess, 120000);
		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return code == 0;
#else
		std::string cmd = "cd '" + folder + "' && ";
		for (const std::string& a : args)
		{
			std::string q = "'";
			for (char c : a)
				q += c == '\'' ? std::string("'\\''") : std::string(1, c);
			cmd += q + "' ";
		}
		cmd += "2>&1";
		FILE* p = popen(cmd.c_str(), "r");
		if (!p)
			return false;
		char chunk[4096];
		size_t n;
		while ((n = fread(chunk, 1, sizeof(chunk), p)) > 0)
			output.append(chunk, n);
		return pclose(p) == 0;
#endif
	}

	// Which system manages the folder (Git, Subversion, none), found by looking up the folders, and its root
	Info detect(const std::string& folder)
	{
		Info info;
		std::error_code ec;
		fs::path p = fs::weakly_canonical(fs::u8path(folder), ec);
		for (; !p.empty(); p = p.parent_path())
		{
			if (fs::exists(p / ".git", ec))
			{
				info.kind = Git;
				info.root = p.u8string();
				break;
			}
			if (fs::exists(p / ".svn", ec))
			{
				info.kind = Svn;
				info.root = p.u8string();
				break;
			}
			if (p == p.parent_path())
				break;
		}
		if (info.kind == Git)
		{
			std::string out;
			if (!run(info.root, { "git", "rev-parse", "--abbrev-ref", "HEAD" }, out))
				info.program_found = out.find("Cannot start") == std::string::npos;
			while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
				out.pop_back();
			info.branch = out;
		}
		return info;
	}

	// Changed files with their state (modified, added, deleted, untracked...)
	std::vector<Change> status(const Info& info, const std::string& folder, std::string& output)
	{
		std::vector<Change> list;
		std::string out;
		const bool ok = info.kind == Git ? run(folder, { "git", "status", "--porcelain", "-uall", "--", "." }, out) : run(folder, { "svn", "status", "--non-interactive", "." }, out);
		output = out;
		if (!ok)
			return list;
		size_t pos = 0;
		while (pos < out.size())
		{
			size_t eol = out.find('\n', pos);
			if (eol == std::string::npos)
				eol = out.size();
			std::string line = out.substr(pos, eol - pos);
			pos = eol + 1;
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.size() < 4)
				continue;
			Change c;
			if (info.kind == Git)
			{
				c.status = line.substr(0, 2);
				c.path = line.substr(3);
			}
			else
			{
				// Seven status columns (item, properties, lock, history, switched, lock owner, conflict), a space, then the path
				if (line.size() < 9)
					continue;
				c.status = line.substr(0, 1);
				if (c.status == " ")
					c.status = line.substr(1, 1);
				c.path = line.substr(8);
			}
			while (!c.status.empty() && c.status.back() == ' ')
				c.status.pop_back();
			if (c.status.empty())
				c.status = "?";
			if (c.path.size() > 1 && c.path[0] == '"')
				c.path = c.path.substr(1, c.path.size() - 2);
			list.push_back(c);
		}
		return list;
	}

	// Adds the changes of the folder and commits them with the message (git add -A, svn add)
	bool commit(const Info& info, const std::string& folder, const std::string& message, std::string& output)
	{
		std::string out;
		if (info.kind == Git)
		{
			if (!run(folder, { "git", "add", "-A", "--", "." }, out))
			{
				output = out;
				return false;
			}
			const bool ok = run(folder, { "git", "commit", "-m", message, "--", "." }, output);
			return ok;
		}
		if (info.kind == Svn)
		{
			run(folder, { "svn", "add", "--force", "--depth", "infinity", "--non-interactive", "." }, out);
			return run(folder, { "svn", "commit", "--non-interactive", "-m", message, "." }, output);
		}
		output = "No repository";
		return false;
	}

	// git pull --ff-only, svn update
	bool update(const Info& info, const std::string& folder, std::string& output)
	{
		if (info.kind == Git)
			return run(folder, { "git", "pull", "--ff-only" }, output);
		if (info.kind == Svn)
			return run(folder, { "svn", "update", "--non-interactive" }, output);
		output = "No repository";
		return false;
	}

	// git push; Subversion has nothing to push (false with a message)
	bool push(const Info& info, const std::string& folder, std::string& output)
	{
		if (info.kind == Git)
			return run(folder, { "git", "push" }, output);
		output = "Nothing to push";
		return false;
	}

	// History of a file, or of the project when no file is given
	bool log(const Info& info, const std::string& folder, const std::string& file, std::string& output)
	{
		if (info.kind == Git)
			return run(folder, { "git", "log", "-n", "30", "--date=short", "--format=%h  %ad  %an  %s", "--", file.empty() ? "." : file }, output);
		if (info.kind == Svn)
			return run(folder, { "svn", "log", "--non-interactive", "-l", "30", file.empty() ? "." : file }, output);
		output = "No repository";
		return false;
	}

	// Differences of a file against the last version
	bool diff(const Info& info, const std::string& folder, const std::string& file, std::string& output)
	{
		if (info.kind == Git)
			return run(folder, { "git", "diff", "--no-color", "--", file.empty() ? "." : file }, output);
		if (info.kind == Svn)
			return run(folder, { "svn", "diff", "--non-interactive", file.empty() ? "." : file }, output);
		output = "No repository";
		return false;
	}

	// Creates a Git repository in the folder
	bool init(const std::string& folder, std::string& output)
	{
		return run(folder, { "git", "init" }, output);
	}
}
