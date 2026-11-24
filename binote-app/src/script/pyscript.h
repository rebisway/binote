// Binote - Python scripts
// A script is a file with a class Textscript. Its properties (parameters) are edited in a dialog box of the application, or the script
// shows its own dialog box with tkinter. run() reads the text through the Textcore object (scripts/lib/textcore.py) and asks for changes.
// The part of the text to work on (selection, paragraph, section, file, project) is a property of the script.
// Python is loaded when it is first needed (stable ABI 3.12: any Python >= 3.12 installed on the computer is used).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#ifndef _PYSCRIPT_H
#define _PYSCRIPT_H

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace pyscript
{
	struct Info
	{
		std::string path;
		std::string name;							// file name without extension
		std::string description;					// translated by the script
		std::string category;
		std::string error;							// the script could not be loaded
		bool user = false;							// in the folder of the user: it can be edited (the delivered ones are read only)
	};

	// Folders of the scripts: delivered with the application, then the ones of the user
	std::string user_folder();
	std::vector<std::string> folders();
	// Scripts of the folders (a script of the user replaces a delivered one with the same name). Python loads each one to read its description.
	std::vector<Info> list(const std::string& language);

	// A change asked by a script
	struct Command
	{
		std::string op;								// selection, paragraph, section, file, in, log
		std::string name;							// in: name of the file
		std::string text;
	};

	struct Result
	{
		bool ok = false;
		std::string error;							// traceback
		std::string output;							// what the script printed
		std::vector<Command> commands;
	};

	class Session
	{
	public:
		// Load the script and its properties (the values of the last run are restored)
		bool begin(const std::string& path, const std::string& language, std::string& error);
		bool has_dialog() const { return _use_properties && !_props.empty(); }
		const std::string& title() const { return _title; }
		// Dialog box of the properties, draw it each frame while it returns 0. 1 = Ok, 2 = Cancel.
		// With preview_text a third button returns 3 (the host runs the script to show what it would do); extra draws something below the properties.
		int draw(const char* ok_text, const char* cancel_text, const char* preview_text = nullptr, const std::function<void()>& extra = nullptr);
		// Send the properties to the script and run it. context: JSON (see App::script_context)
		Result run(const std::string& context);
		// Save the properties
		void end();

	private:
		typedef std::variant<bool, int, float, std::string, std::vector<std::string>> Value;		// list: first item = selected index, then the choices
		struct Property
		{
			std::string key, name;
			Value value;
			int file = 0;							// 0 not a file, 1 open, 2 save
			std::string filter_name, filter_pattern;
			bool multiline = false;
		};
		std::string _path, _module, _title;
		bool _use_properties = true;
		bool _opened = false;
		std::vector<Property> _props, _edit;

		void apply(int index);
	};

	// Compile a source without running it. Empty message if it is correct; line and column (from 1) of the error, 0 if unknown.
	std::string check_syntax(const std::string& source, const std::string& name, int& line, int& column);

	// Grammar engine (the grammar-engine folder, in Python): true when it can run. known tells if a word is spelled correctly (may be empty).
	bool grammar_available(const std::function<bool(const std::string&)>& known);
	// Issues of one paragraph, as JSON (see grammar_engine/bridge.py; offsets in UTF-8 bytes); empty text on failure
	std::string grammar_check(const std::string& language, const std::string& ui, const std::string& ignored, const std::string& text);

	// False if no Python could be loaded (the reason is in last_error)
	bool available();
	std::string version();							// "3.12.7", empty if Python is not loaded
	std::string last_error();
	void finalize();
}

#endif
