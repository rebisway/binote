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

# Binote script: capital letters of the titles: one capital per word, or a capital on the first word only.
# The titles are the Markdown headings (# Title). With a selection or a paragraph, every line is a title.
import re
import textcore

SMALL_FR = {'le', 'la', 'les', 'l', 'un', 'une', 'des', 'du', 'de', 'd', 'à', 'au', 'aux', 'et', 'ou', 'ni', 'mais', 'donc', 'car', 'en', 'sur', 'sous', 'dans', 'par', 'pour', 'avec', 'sans',
            'chez', 'vers', 'que', 'qui', 'à', 'ce', 'ces', 'son', 'sa', 'ses'}
SMALL_EN = {'a', 'an', 'the', 'of', 'in', 'on', 'at', 'to', 'for', 'and', 'or', 'but', 'nor', 'by', 'with', 'from', 'as', 'is', 'into', 'over'}
SMALL_ES = {'el', 'la', 'los', 'las', 'un', 'una', 'de', 'del', 'y', 'o', 'en', 'a', 'por', 'para', 'con', 'sin', 'que', 'al'}


class Textscript:
    def __init__(self):
        self.scope = 3
        self.mode = 0
        self.shouting = False

        self.en_us = {'description': 'Capitals of the titles', 'mode': 'Capitals', 'modes': ['One capital per word', 'A capital on the first word only', 'All in lower case']}
        self.fr_fr = {'description': 'Majuscules des titres', 'mode': 'Majuscules', 'modes': ['Une majuscule par mot', 'Une majuscule sur le premier mot', 'Tout en minuscules']}
        self.es_es = {'description': 'Mayúsculas de los títulos', 'mode': 'Mayúsculas', 'modes': ['Una mayúscula por palabra', 'Una mayúscula en la primera palabra', 'Todo en minúsculas']}
        self.Language = {'en-us': self.en_us, 'fr-fr': self.fr_fr, 'es-es': self.es_es}

    def set_language(self, value):
        self.language = value
        self.lang = self.Language.get(value, self.en_us)

    def get_description(self):
        return self.lang['description']

    def get_category(self):
        return 'text'

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

    def small_words(self):
        code = self.core.text_language[:2]
        return SMALL_FR if code == 'fr' else SMALL_ES if code == 'es' else SMALL_EN

    # the word is kept as it is when it looks like an acronym (NASA) or has capitals inside (iPhone)
    def keep(self, word):
        letters = [c for c in word if c.isalpha()]
        if self.shouting:
            return False
        return len(letters) > 1 and (word.isupper() or any(c.isupper() for c in word[1:]))

    def case_word(self, word, first, small):
        if self.keep(word):
            return word
        low = word.lower()
        if self.mode == 2:
            return low
        if self.mode == 1:
            return low[:1].upper() + low[1:] if first else low
        if not first and low in small:
            return low
        return low[:1].upper() + low[1:]

    def case_title(self, title):
        self.shouting = title.isupper()		# a title written in capitals has no acronym to keep
        small = self.small_words()
        state = {'first': True}

        # a word with its elisions and hyphens: l'homme, peut-être. The part after an apostrophe is the word itself.
        def word(m):
            text = m.group(0)
            parts = re.split(r"([-'’])", text)
            out = []
            for i, part in enumerate(parts):
                if i % 2 == 1 or not part:
                    out.append(part)
                    continue
                next_is_apostrophe = i + 1 < len(parts) and parts[i + 1] in ("'", '’')
                if next_is_apostrophe:
                    # l', d', qu': kept in lower case (a capital only at the start of the title)
                    out.append(part.lower() if not state['first'] or self.mode == 2 else part.lower()[:1].upper() + part.lower()[1:])
                    continue
                out.append(self.case_word(part, state['first'] and i == 0, small))
            state['first'] = False
            return ''.join(out)

        return re.sub(r"[^\W\d_]+(?:[-'’][^\W\d_]+)*", word, title)

    def convert(self, text):
        lines = text.split('\n')
        headings = [i for i, line in enumerate(lines) if re.match(r'\s{0,3}#{1,6}\s', line)]
        # no heading in a selection or a paragraph: every line is a title
        targets = headings if headings or self.scope > 1 else [i for i, line in enumerate(lines) if line.strip()]
        for i in targets:
            m = re.match(r'(\s{0,3}#{1,6}\s+)(.*)$', lines[i])
            if m:
                lines[i] = m.group(1) + self.case_title(m.group(2))
            else:
                lines[i] = self.case_title(lines[i])
        return '\n'.join(lines)

    def run(self):
        self.core.transform(self.convert, textcore.SCOPES[self.scope])
