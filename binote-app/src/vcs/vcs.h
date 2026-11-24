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

// Binote - version control of the folder of a project: Git and Subversion, through their command line programs

#pragma once
#ifndef _VCS_H
#define _VCS_H

#include <string>
#include <vector>

namespace vcs
{
	enum Kind { None, Git, Svn };

	struct Info
	{
		Kind kind = None;
		std::string root;							// folder which holds .git or .svn
		std::string branch;							// Git only
		bool program_found = true;					// false if git / svn cannot be started
	};

	struct Change
	{
		std::string status;							// M modified, A added, D deleted, ? not under control...
		std::string path;							// relative to the folder given to status()
	};

	// Look for the repository in the folder and the ones above it
	Info detect(const std::string& folder);

	// Run a program in a folder, its output (stdout and stderr) in output. False if it fails or cannot start.
	bool run(const std::string& folder, const std::vector<std::string>& args, std::string& output);

	std::vector<Change> status(const Info& info, const std::string& folder, std::string& output);
	// Add what is new in the folder then commit it
	bool commit(const Info& info, const std::string& folder, const std::string& message, std::string& output);
	bool update(const Info& info, const std::string& folder, std::string& output);			// pull / svn update
	bool push(const Info& info, const std::string& folder, std::string& output);			// Git only
	bool log(const Info& info, const std::string& folder, const std::string& file, std::string& output);
	bool diff(const Info& info, const std::string& folder, const std::string& file, std::string& output);
	bool init(const std::string& folder, std::string& output);								// git init
}

#endif
