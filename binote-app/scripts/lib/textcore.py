# Binote - www.rebisway.com - contact@rebisway.com
# Copyright (C) 2026 Thomas Gourgnier
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Binote - core of the scripts. A script is a file with a class Textscript; Binote gives it a Textcore object (set_core).
#
# The script describes its parameters (properties), Binote shows them in a dialog box, then calls run().
# The script reads the text with the attributes of the core (selection, paragraph, section, file, files) and writes
# with its methods (replace_selection, replace_file, transform...). The changes are applied by Binote in one step
# for the document of the editor (Ctrl+Z undoes them).

import os
import sys
import json
import io
import importlib.util
import traceback
import contextlib

# Embedded Python does not find the Tcl/Tk libraries (init.tcl): locate them from the _tkinter module,
# so a script can display its own dialog box with tkinter.
_spec = importlib.util.find_spec('_tkinter')
if _spec is not None and _spec.origin:
    _root = os.path.dirname(os.path.dirname(_spec.origin))
    for _var, _dir in (('TCL_LIBRARY', 'tcl8.6'), ('TK_LIBRARY', 'tk8.6')):
        _path = os.path.join(_root, 'tcl', _dir)
        if os.path.isdir(_path):
            os.environ[_var] = _path

# the parts of the text a script can work on: the selection, the paragraph of the caret, its section (from a heading to the next heading
# of the same level or above), the current file, every file of the project
SCOPES = ['selection', 'paragraph', 'section', 'file', 'project']

# texts of the scope choice, the same for all the scripts (get_property returns [selected index] + scope_choices(language))
SCOPE_CHOICES = {
    'en-us': ['Selection', 'Paragraph', 'Section', 'File', 'Project'],
    'fr-fr': ['Sélection', 'Paragraphe', 'Section', 'Fichier', 'Projet'],
    'es-es': ['Selección', 'Párrafo', 'Sección', 'Archivo', 'Proyecto'],
}
SCOPE_LABEL = {'en-us': 'Apply to', 'fr-fr': 'Appliquer à', 'es-es': 'Aplicar a'}


def scope_choices(language):
    return SCOPE_CHOICES.get(language, SCOPE_CHOICES['en-us'])


def scope_label(language):
    return SCOPE_LABEL.get(language, SCOPE_LABEL['en-us'])


# menus of the Scripts menu: a script returns the key from get_category()
CATEGORIES = {
    'text': {'en-us': 'Text', 'fr-fr': 'Texte', 'es-es': 'Texto'},
    'typography': {'en-us': 'Typography', 'fr-fr': 'Typographie', 'es-es': 'Tipografía'},
    'structure': {'en-us': 'Structure', 'fr-fr': 'Structure', 'es-es': 'Estructura'},
    'analysis': {'en-us': 'Analysis', 'fr-fr': 'Analyse', 'es-es': 'Análisis'},
    'examples': {'en-us': 'Examples', 'fr-fr': 'Exemples', 'es-es': 'Ejemplos'},
}


class Textcore:
    def __init__(self):
        self.script = None
        self.language = 'en-us'			# language of the interface (code of a language file)
        self.text_language = 'fr'			# language of the text of the project
        self.markdown = False
        self.selection = ''				# selected text, '' if nothing is selected
        self.paragraph = ''				# line of the caret
        self.section = ''					# lines of the section of the caret
        self.file = ''					# text of the current file
        self.file_name = ''
        self.files = {}					# project: name of the file -> text, in the order of the project
        self.project_name = ''
        self.commands = []

    def set_script(self, script):
        self.script = script

    def set_language(self, value):
        self.language = value

    def set_context(self, text):
        data = json.loads(text)
        self.text_language = data.get('text_language', 'fr')
        self.markdown = bool(data.get('markdown', False))
        self.selection = data.get('selection', '')
        self.paragraph = data.get('paragraph', '')
        self.section = data.get('section', '')
        self.file = data.get('file', '')
        self.file_name = data.get('file_name', '')
        self.files = data.get('files', {})
        self.project_name = data.get('project_name', '')
        self.commands = []

    # ---- changes: they are applied after run() returns

    def replace_selection(self, text):
        self.commands.append({'op': 'selection', 'text': str(text)})

    def replace_paragraph(self, text):
        self.commands.append({'op': 'paragraph', 'text': str(text)})

    def replace_section(self, text):
        self.commands.append({'op': 'section', 'text': str(text)})

    def replace_file(self, text):
        self.commands.append({'op': 'file', 'text': str(text)})

    # another file of the project (written in its destination)
    def replace_in(self, name, text):
        self.commands.append({'op': 'in', 'name': str(name), 'text': str(text)})

    # apply a function (text -> text) to the part of the text which the user chose: a scope of SCOPES
    def transform(self, function, scope):
        if scope == 'selection':
            self.replace_selection(function(self.selection))
        elif scope == 'paragraph':
            self.replace_paragraph(function(self.paragraph))
        elif scope == 'section':
            self.replace_section(function(self.section))
        elif scope == 'project':
            for name, text in self.files.items():
                changed = function(text)
                if changed != text:
                    self.replace_in(name, changed)
        else:
            self.replace_file(function(self.file))

    # the text of a scope, for the scripts which only read
    def text_of(self, scope):
        if scope == 'selection':
            return self.selection
        if scope == 'paragraph':
            return self.paragraph
        if scope == 'section':
            return self.section
        if scope == 'project':
            return '\n\n'.join(self.files.values())
        return self.file

    # write a line in the log of the application
    def log(self, text):
        self.commands.append({'op': 'log', 'text': str(text)})

    # display a message box (tkinter), the script goes on if the box cannot be displayed
    def message(self, text, title=''):
        try:
            import tkinter
            from tkinter import messagebox
            window = tkinter.Tk()
            window.withdraw()
            window.attributes('-topmost', True)
            messagebox.showinfo(title if title else self.script.get_description(), str(text), parent=window)
            window.destroy()
        except Exception:
            self.log(text)

    # file dialogs (tkinter): the path which is chosen, '' if the dialog is cancelled or can not be displayed.
    # filters: list of (name, pattern), for example [('Text', '*.txt'), ('All files', '*.*')]
    def open_file(self, title='', filters=None, initial=''):
        return self._file_dialog(False, title, filters, initial, '')

    def save_file(self, title='', filters=None, initial='', extension=''):
        return self._file_dialog(True, title, filters, initial, extension)

    def _file_dialog(self, save, title, filters, initial, extension):
        try:
            import tkinter
            from tkinter import filedialog
            window = tkinter.Tk()
            window.withdraw()
            window.attributes('-topmost', True)
            options = {'parent': window, 'title': title if title else self.script.get_description()}
            if filters:
                options['filetypes'] = [(name, pattern.replace(';', ' ')) for name, pattern in filters]
            if initial:
                folder, name = os.path.split(initial)
                if folder and os.path.isdir(folder):
                    options['initialdir'] = folder
                if name:
                    options['initialfile'] = name
            if save:
                if extension:
                    options['defaultextension'] = extension
                path = filedialog.asksaveasfilename(**options)
            else:
                path = filedialog.askopenfilename(**options)
            window.destroy()
            return path if path else ''
        except Exception:
            return ''

    # ---- what Binote asks the script, with defaults for the optional methods

    def get_description(self):
        return self.script.get_description()

    def get_category(self):
        name = self.script.get_category()
        names = CATEGORIES.get(name)
        if names is None:
            return name
        return names.get(self.language, names['en-us'])

    # false if the script manages its own dialog box (tkinter): run() shows it
    def use_properties(self):
        if hasattr(self.script, 'use_properties'):
            return self.script.use_properties()
        return True

    # name used to save the value of a property: it does not change with the language or the order (the index by default)
    def get_property_key(self, index):
        if hasattr(self.script, 'get_property_key'):
            return self.script.get_property_key(index)
        return str(index)

    # a text property can be a file name: ('open' or 'save', 'Name of the filter', '*.ext;*.ext2')
    def get_property_file(self, index):
        if hasattr(self.script, 'get_property_file'):
            return self.script.get_property_file(index)
        return None

    # a text property can have several lines
    def get_property_multiline(self, index):
        if hasattr(self.script, 'get_property_multiline'):
            return self.script.get_property_multiline(index)
        return False


# ---- functions called by Binote. They return JSON texts (the interface between C++ and Python stays simple).

_core = None


def _load(path, language):
    spec = importlib.util.spec_from_file_location('binote_script_' + os.path.splitext(os.path.basename(path))[0], path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    script = module.Textscript()
    core = Textcore()
    core.set_script(script)
    script.set_core(core)
    script.set_language(language)
    core.set_language(language)
    return core


def _result(ok, error='', **extra):
    data = {'ok': ok, 'error': error}
    data.update(extra)
    return json.dumps(data)


# one entry per script of the folders: description and category (the language is the one of the interface)
def list_scripts(folders, language):
    found = []
    for folder in json.loads(folders):
        if not os.path.isdir(folder):
            continue
        if folder not in sys.path:
            sys.path.append(folder)
        for name in sorted(os.listdir(folder)):
            path = os.path.join(folder, name)
            if not name.endswith('.py') or name.startswith('_') or not os.path.isfile(path):
                continue
            item = {'path': path, 'name': name[:-3], 'description': name[:-3], 'category': '', 'error': ''}
            try:
                core = _load(path, language)
                item['description'] = core.get_description()
                item['category'] = core.get_category()
            except BaseException:
                item['error'] = traceback.format_exc()
            found.append(item)
    return json.dumps(found)


# load a script and describe its properties
def begin(path, language):
    global _core
    try:
        _core = _load(path, language)
        properties = []
        for i in range(_core.script.get_properties_count()):
            properties.append({'key': _core.get_property_key(i), 'name': _core.script.get_property_description(i), 'value': _core.script.get_property(i),
                               'file': _core.get_property_file(i), 'multiline': bool(_core.get_property_multiline(i))})
        return _result(True, description=_core.get_description(), use_properties=bool(_core.use_properties()), properties=properties)
    except BaseException:
        return _result(False, traceback.format_exc())


def set_property(index, value):
    try:
        _core.script.set_property(index, value)
        return _result(True)
    except BaseException:
        return _result(False, traceback.format_exc())


# run the script on the context (JSON); returns what it printed and the changes it asks for
def run(context):
    out = io.StringIO()
    try:
        _core.set_context(context)
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            _core.script.run()
        return _result(True, output=out.getvalue(), commands=_core.commands)
    except BaseException:
        return _result(False, traceback.format_exc(), output=out.getvalue(), commands=[])


# check the syntax of a source without running it
def check_syntax(source, name):
    try:
        compile(source, name, 'exec')
        return _result(True)
    except SyntaxError as e:
        return _result(False, str(e.msg), line=e.lineno or 0, column=e.offset or 0)
    except BaseException:
        return _result(False, traceback.format_exc(), line=0, column=0)
