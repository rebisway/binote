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

# Binote script: turn lines into a bulleted or a numbered list (or take the list marks off). Each non-empty line is an item;
# a blank line ends the list, the numbering starts again after it.
import re
import textcore

MARK = re.compile(r'^(\s*)(?:[-*+•]|\d+[.)])\s+')


class Textscript:
    def __init__(self):
        self.scope = 3
        self.kind = 0
        self.skip_headings = True

        self.en_us = {'description': 'Bulleted or numbered list', 'kind': 'List', 'kinds': ['Bulleted list (- )', 'Numbered list (1. )', 'Remove the list marks'], 'headings': 'Leave the headings (#) alone'}
        self.fr_fr = {'description': 'Liste à puces ou numérotée', 'kind': 'Liste', 'kinds': ['Liste à puces (- )', 'Liste numérotée (1. )', 'Retirer les marques de liste'], 'headings': 'Ne pas toucher aux titres (#)'}
        self.es_es = {'description': 'Lista con viñetas o numerada', 'kind': 'Lista', 'kinds': ['Lista con viñetas (- )', 'Lista numerada (1. )', 'Quitar las marcas de lista'], 'headings': 'No tocar los títulos (#)'}
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
        return 3

    def get_property_key(self, index):
        return ('scope', 'kind', 'headings')[index]

    def get_property_description(self, index):
        return textcore.scope_label(self.language) if index == 0 else self.lang[('scope', 'kind', 'headings')[index]]

    def get_property(self, index):
        if index == 0:
            return [str(self.scope)] + textcore.scope_choices(self.language)
        if index == 1:
            return [str(self.kind)] + self.lang['kinds']
        return self.skip_headings

    def set_property(self, index, val):
        if index == 0:
            self.scope = int(val)
        elif index == 1:
            self.kind = int(val)
        else:
            self.skip_headings = bool(val)

    def convert(self, text):
        out, number = [], 0
        for line in text.split('\n'):
            if not line.strip():
                number = 0
                out.append(line)
                continue
            if self.skip_headings and re.match(r'\s{0,3}#{1,6}\s', line):
                out.append(line)
                continue
            body = MARK.sub(r'\1', line)
            if self.kind == 2:
                out.append(body)
                continue
            indent = re.match(r'\s*', body).group(0)
            body = body[len(indent):]
            number += 1
            out.append(indent + ('- ' if self.kind == 0 else '%d. ' % number) + body)
        return '\n'.join(out)

    def run(self):
        self.core.transform(self.convert, textcore.SCOPES[self.scope])
