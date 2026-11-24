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

# Binote script: count the words. It only reads the text: the result goes to the log and to a message box, the text is not changed.
# This script shows its own dialog box with tkinter: use_properties() returns False and run() asks the question.


class Textscript:
    def __init__(self):
        self.en_us = {'description': 'Count the words', 'scope': 'Count the words of', 'choices': ['Selection', 'Paragraph', 'Section', 'File', 'Project'], 'result': '%d words (%s)'}
        self.fr_fr = {'description': 'Compter les mots', 'scope': 'Compter les mots de', 'choices': ['Sélection', 'Paragraphe', 'Section', 'Fichier', 'Projet'], 'result': '%d mots (%s)'}
        self.es_es = {'description': 'Contar las palabras', 'scope': 'Contar las palabras de', 'choices': ['Selección', 'Párrafo', 'Sección', 'Archivo', 'Proyecto'], 'result': '%d palabras (%s)'}
        self.Language = {'en-us': self.en_us, 'fr-fr': self.fr_fr, 'es-es': self.es_es}

    def set_language(self, value):
        self.lang = self.Language.get(value, self.en_us)

    def get_description(self):
        return self.lang['description']

    def get_category(self):
        return 'analysis'

    def set_core(self, value):
        self.core = value

    # the script displays its own dialog box (tkinter): no properties dialog box
    def use_properties(self):
        return False

    def get_properties_count(self):
        return 0

    def get_property_description(self, index):
        return ''

    def get_property(self, index):
        return None

    def set_property(self, index, val):
        pass

    # dialog box: the index of the choice, -1 if cancelled
    def ask(self):
        import tkinter as tk
        from tkinter import ttk
        window = tk.Tk()
        window.title(self.lang['description'])
        window.attributes('-topmost', True)
        choice = tk.IntVar(value=3)
        state = {'ok': False}
        tk.Label(window, text=self.lang['scope']).pack(padx=12, pady=(12, 4), anchor='w')
        for i, name in enumerate(self.lang['choices']):
            tk.Radiobutton(window, text=name, variable=choice, value=i).pack(padx=12, anchor='w')

        def ok():
            state['ok'] = True
            window.destroy()
        ttk.Button(window, text='OK', command=ok).pack(pady=12)
        window.protocol('WM_DELETE_WINDOW', window.destroy)
        window.mainloop()
        return choice.get() if state['ok'] else -1

    def run(self):
        index = self.ask()
        if index < 0:
            return
        scope = ('selection', 'paragraph', 'section', 'file', 'project')[index]
        words = len(self.core.text_of(scope).split())
        text = self.lang['result'] % (words, self.lang['choices'][index])
        self.core.log(text)
        self.core.message(text)
