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

# Binote script: replace the quotation marks "like this" (straight or curly) by French guillemets « like this »,
# with a narrow no-break space, a no-break space, or no space inside. The guillemets already in the text get the same spacing.
import re
import textcore

NBSP = ' '
NNBSP = ' '


class Textscript:
    def __init__(self):
        self.scope = 3
        self.space = 0		# 0 narrow no-break space, 1 no-break space, 2 no space

        self.en_us = {'description': 'Quotation marks to guillemets « »', 'space': 'Space inside the guillemets',
            'spaces': ['Narrow no-break space', 'No-break space', 'No space']}
        self.fr_fr = {'description': 'Guillemets "..." en « ... »', 'space': 'Espace dans les guillemets',
            'spaces': ['Espace insécable fine', 'Espace insécable', 'Sans espace']}
        self.es_es = {'description': 'Comillas "..." a « ... »', 'space': 'Espacio dentro de las comillas',
            'spaces': ['Espacio fino sin salto', 'Espacio sin salto', 'Sin espacio']}
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
        return 2

    def get_property_key(self, index):
        return ('scope', 'space')[index]

    def get_property_description(self, index):
        return textcore.scope_label(self.language) if index == 0 else self.lang['space']

    def get_property(self, index):
        if index == 0:
            return [str(self.scope)] + textcore.scope_choices(self.language)
        return [str(self.space)] + self.lang['spaces']

    def set_property(self, index, val):
        if index == 0:
            self.scope = int(val)
        else:
            self.space = int(val)

    def convert(self, text):
        space = (NNBSP, NBSP, '')[self.space]
        out, in_code, opened = [], False, False
        for line in text.split('\n'):
            if line.lstrip().startswith('```'):
                in_code = not in_code
                out.append(line)
                continue
            if in_code:
                out.append(line)
                continue
            if not line.strip():
                opened = False		# a quotation does not go over a paragraph
                out.append(line)
                continue
            chars = []
            for c in line:
                if c == '"':
                    chars.append('»' if opened else '«')
                    opened = not opened
                elif c == '“':
                    chars.append('«')
                    opened = True
                elif c == '”':
                    chars.append('»')
                    opened = False
                else:
                    chars.append(c)
            out.append(''.join(chars))
        text = '\n'.join(out)
        # the same spacing for every guillemet, the ones which were already there too
        text = re.sub('«[   ]*', '«' + space, text)
        text = re.sub('[   ]*»', space + '»', text)
        return text

    def run(self):
        self.core.transform(self.convert, textcore.SCOPES[self.scope])
