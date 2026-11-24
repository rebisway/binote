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

# Binote script: blank lines between the paragraphs: keep one, remove them all, or add one between two paragraphs.
import re
import textcore


class Textscript:
    def __init__(self):
        self.scope = 3
        self.mode = 0

        self.en_us = {'description': 'Blank lines', 'mode': 'Blank lines', 'modes': ['One blank line at most', 'Remove all the blank lines', 'One blank line between the paragraphs']}
        self.fr_fr = {'description': 'Lignes vides', 'mode': 'Lignes vides', 'modes': ['Une ligne vide au plus', 'Supprimer toutes les lignes vides', 'Une ligne vide entre les paragraphes']}
        self.es_es = {'description': 'Líneas vacías', 'mode': 'Líneas vacías', 'modes': ['Una línea vacía como máximo', 'Quitar todas las líneas vacías', 'Una línea vacía entre los párrafos']}
        self.Language = {'en-us': self.en_us, 'fr-fr': self.fr_fr, 'es-es': self.es_es}

    def set_language(self, value):
        self.language = value
        self.lang = self.Language.get(value, self.en_us)

    def get_description(self):
        return self.lang['description']

    def get_category(self):
        return 'structure'

    def set_core(self, value):
        self.core = value

    def get_properties_count(self):
        return 2

    def get_property_key(self, index):
        return ('scope', 'mode')[index]

    def get_property_description(self, index):
        return textcore.scope_label(self.language) if index == 0 else self.lang['mode']

    def get_property(self, index):
        if index == 0:
            return [str(self.scope)] + textcore.scope_choices(self.language)
        return [str(self.mode)] + self.lang['modes']

    def set_property(self, index, val):
        if index == 0:
            self.scope = int(val)
        else:
            self.mode = int(val)

    def convert(self, text):
        if self.mode == 0:
            return re.sub(r'\n[ \t]*(\n[ \t]*)+', '\n\n', text)
        lines = [line for line in text.split('\n') if line.strip()]
        return ('\n\n' if self.mode == 2 else '\n').join(lines)

    def run(self):
        self.core.transform(self.convert, textcore.SCOPES[self.scope])
