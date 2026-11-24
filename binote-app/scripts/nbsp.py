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

# Binote script: French typography, no-break spaces: before ; ! ? (narrow), before : (normal), inside « » (narrow), before % (narrow).
# The spaces which are missing are added where it is safe (not in an address, not in a time, not in code).
import re
import textcore

NBSP = ' '
NNBSP = ' '
SPACES = '[   ]'


class Textscript:
    def __init__(self):
        self.scope = 3
        self.narrow = True
        self.percent = True

        self.en_us = {'description': 'No-break spaces (French)', 'narrow': 'Narrow no-break space (U+202F) before ; ! ? and in « »', 'percent': 'Space before %'}
        self.fr_fr = {'description': 'Espaces insécables', 'narrow': 'Espace insécable fine (U+202F) avant ; ! ? et dans « »', 'percent': 'Espace avant %'}
        self.es_es = {'description': 'Espacios sin salto (francés)', 'narrow': 'Espacio fino sin salto (U+202F) antes de ; ! ? y en « »', 'percent': 'Espacio antes de %'}
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
        return 3

    def get_property_key(self, index):
        return ('scope', 'narrow', 'percent')[index]

    def get_property_description(self, index):
        return textcore.scope_label(self.language) if index == 0 else self.lang[('scope', 'narrow', 'percent')[index]]

    def get_property(self, index):
        if index == 0:
            return [str(self.scope)] + textcore.scope_choices(self.language)
        return self.narrow if index == 1 else self.percent

    def set_property(self, index, val):
        if index == 0:
            self.scope = int(val)
        elif index == 1:
            self.narrow = bool(val)
        else:
            self.percent = bool(val)

    def segment(self, s):
        thin = NNBSP if self.narrow else NBSP
        # ; ! ? : the spaces before are replaced, a missing one is added when a space or the end of the line follows
        s = re.sub(SPACES + r'+([;!?])', thin + r'\1', s)
        s = re.sub(r'(?<=[\w)»…])([;!?])(?=\s|$|["”»)])', thin + r'\1', s)
        s = re.sub(SPACES + r'+:(?=\s|$)', NBSP + ':', s)
        s = re.sub(r'(?<=[\w)»]):(?=\s|$)', NBSP + ':', s)
        s = re.sub('«' + SPACES + '*', '«' + thin, s)
        s = re.sub(SPACES + '*»', thin + '»', s)
        if self.percent:
            s = re.sub(r'(\d)' + SPACES + '?%', r'\1' + thin + '%', s)
        return s

    def convert(self, text):
        out, in_code = [], False
        for line in text.split('\n'):
            if line.lstrip().startswith('```'):
                in_code = not in_code
                out.append(line)
            elif in_code:
                out.append(line)
            else:
                # the code between backticks is left as it is
                parts = line.split('`')
                out.append('`'.join(self.segment(p) if i % 2 == 0 else p for i, p in enumerate(parts)))
        return '\n'.join(out)

    def run(self):
        if not self.core.text_language.startswith('fr'):
            self.core.log('The text is not French (%s): nothing done' % self.core.text_language)
            return
        self.core.transform(self.convert, textcore.SCOPES[self.scope])
