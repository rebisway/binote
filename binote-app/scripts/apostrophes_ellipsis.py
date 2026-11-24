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

# Binote script: simple typography: typographic apostrophe (l'homme -> l’homme), ellipsis (... -> …), dialogue dash ("- " at the start of a line -> "— ").
import re
import textcore


class Textscript:
    def __init__(self):
        self.scope = 3
        self.apostrophe = True
        self.ellipsis = True
        self.dash = False

        self.en_us = {'description': 'Apostrophes, ellipsis, dialogue dash', 'apostrophe': 'Typographic apostrophe ’', 'ellipsis': 'Ellipsis …', 'dash': 'Dialogue dash — at the start of a line'}
        self.fr_fr = {'description': 'Apostrophes, points de suspension, tirets', 'apostrophe': 'Apostrophe typographique ’', 'ellipsis': 'Points de suspension …', 'dash': 'Tiret de dialogue — en début de ligne'}
        self.es_es = {'description': 'Apóstrofos, puntos suspensivos, guiones', 'apostrophe': 'Apóstrofo tipográfico ’', 'ellipsis': 'Puntos suspensivos …', 'dash': 'Raya de diálogo — al principio de la línea'}
        self.Language = {'en-us': self.en_us, 'fr-fr': self.fr_fr, 'es-es': self.es_es}

    def set_language(self, value):
        self.language = value
        self.lang = self.Language.get(value, self.en_us)

    def get_description(self):
        return self.lang['description']

    def get_category(self):
        return 'typography'

    def set_core(self, value):
        self.core = value

    def get_properties_count(self):
        return 4

    def get_property_key(self, index):
        return ('scope', 'apostrophe', 'ellipsis', 'dash')[index]

    def get_property_description(self, index):
        return textcore.scope_label(self.language) if index == 0 else self.lang[('scope', 'apostrophe', 'ellipsis', 'dash')[index]]

    def get_property(self, index):
        if index == 0:
            return [str(self.scope)] + textcore.scope_choices(self.language)
        return (None, self.apostrophe, self.ellipsis, self.dash)[index]

    def set_property(self, index, val):
        if index == 0:
            self.scope = int(val)
        elif index == 1:
            self.apostrophe = bool(val)
        elif index == 2:
            self.ellipsis = bool(val)
        else:
            self.dash = bool(val)

    def segment(self, s):
        if self.apostrophe:
            s = re.sub(r"(?<=\w)'(?=\w)", '\u2019', s)
        if self.ellipsis:
            s = re.sub(r'\.{3,}', '\u2026', s)
        return s

    def convert(self, text):
        out, in_code = [], False
        for line in text.split('\n'):
            if line.lstrip().startswith('```'):
                in_code = not in_code
                out.append(line)
                continue
            if in_code:
                out.append(line)
                continue
            parts = line.split('`')		# the code between backticks is left as it is
            line = '`'.join(self.segment(p) if i % 2 == 0 else p for i, p in enumerate(parts))
            if self.dash:
                line = re.sub(r'^(\s*)[-\u2013] ', '\\1\u2014 ', line)
            out.append(line)
        return '\n'.join(out)

    def run(self):
        self.core.transform(self.convert, textcore.SCOPES[self.scope])
