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

# Binote script: join the lines of a paragraph cut by hard line breaks (blank lines separate the paragraphs).


class Textscript:
    def __init__(self):
        self.scope = 3

        self.en_us = {'description': 'Join the cut lines', 'scope': 'Apply to', 'choices': ['Selection', 'Paragraph', 'Section', 'File', 'Project']}
        self.fr_fr = {'description': 'Recoller les lignes coupées', 'scope': 'Appliquer à', 'choices': ['Sélection', 'Paragraphe', 'Section', 'Fichier', 'Projet']}
        self.es_es = {'description': 'Unir las líneas cortadas', 'scope': 'Aplicar a', 'choices': ['Selección', 'Párrafo', 'Sección', 'Archivo', 'Proyecto']}
        self.Language = {'en-us': self.en_us, 'fr-fr': self.fr_fr, 'es-es': self.es_es}

    def set_language(self, value):
        self.lang = self.Language.get(value, self.en_us)

    def get_description(self):
        return self.lang['description']

    def get_category(self):
        return 'structure'

    def set_core(self, value):
        self.core = value

    def get_properties_count(self):
        return 1

    def get_property_key(self, index):
        return 'scope'

    def get_property_description(self, index):
        return self.lang['scope']

    def get_property(self, index):
        return [str(self.scope)] + self.lang['choices']

    def set_property(self, index, val):
        self.scope = int(val)

    def join(self, text):
        paragraphs, current = [], []
        for line in text.split('\n'):
            if line.strip():
                current.append(line.strip())
            else:
                if current:
                    paragraphs.append(' '.join(current))
                    current = []
                paragraphs.append('')
        if current:
            paragraphs.append(' '.join(current))
        return '\n'.join(paragraphs)

    def run(self):
        self.core.transform(self.join, ('selection', 'paragraph', 'section', 'file', 'project')[self.scope])
