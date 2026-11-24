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

# Binote script: remove the repeated spaces and the spaces at the end of the lines.
# A script is a class Textscript. Binote shows its properties in a dialog box, then calls run().
import re


class Textscript:
    def __init__(self):
        self.scope = 3		# index of the part of the text to work on: selection, paragraph, section, file, project
        self.trim = True

        self.en_us = {'description': 'Fix spaces', 'scope': 'Apply to', 'trim': 'Remove the spaces at the end of the lines',
            'choices': ['Selection', 'Paragraph', 'Section', 'File', 'Project']}
        self.fr_fr = {'description': 'Corriger les espaces', 'scope': 'Appliquer à', 'trim': 'Supprimer les espaces en fin de ligne',
            'choices': ['Sélection', 'Paragraphe', 'Section', 'Fichier', 'Projet']}
        self.es_es = {'description': 'Corregir los espacios', 'scope': 'Aplicar a', 'trim': 'Quitar los espacios al final de las líneas',
            'choices': ['Selección', 'Párrafo', 'Sección', 'Archivo', 'Proyecto']}
        self.Language = {'en-us': self.en_us, 'fr-fr': self.fr_fr, 'es-es': self.es_es}

    def set_language(self, value):
        self.lang = self.Language.get(value, self.en_us)	# unknown language: English

    def get_description(self):
        return self.lang['description']

    def get_category(self):
        return 'typography'

    # set the reference to the core of Binote (text, selection, changes), do not modify
    def set_core(self, value):
        self.core = value

    def get_properties_count(self):
        return 2

    # name used to save the value: it does not change with the language
    def get_property_key(self, index):
        return ('scope', 'trim')[index]

    def get_property_description(self, index):
        return self.lang[('scope', 'trim')[index]]

    # a bool is a check box, a list is a choice (the first item is the selected index, the others are the choices)
    def get_property(self, index):
        if index == 0:
            return [str(self.scope)] + self.lang['choices']
        return self.trim

    def set_property(self, index, val):
        if index == 0:
            self.scope = int(val)
        else:
            self.trim = bool(val)

    def fix(self, text):
        lines = []
        for line in text.split('\n'):
            indent = re.match(r'[ \t]*', line).group(0)
            body = re.sub(r'[ \t]{2,}', ' ', line[len(indent):])
            lines.append(indent + (body.rstrip() if self.trim else body))
        return '\n'.join(lines)

    def run(self):
        self.core.transform(self.fix, ('selection', 'paragraph', 'section', 'file', 'project')[self.scope])
