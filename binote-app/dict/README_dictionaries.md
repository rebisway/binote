# Dictionaries of the spell checker

Only the French dictionary is stored in the repository. The others are **downloaded by the build** (`tools\fetch-dictionaries.ps1`, run after the compilation by Visual Studio and by CMake) into the `dict` folder beside `binote.exe`, with their licenses in `dict\licenses`: the zip and the installer are made from that folder. A build without network goes on with the dictionaries which are there (a warning is printed); to fetch them by hand: `powershell -ExecutionPolicy Bypass -File binote-app\tools\fetch-dictionaries.ps1 -Output bin\Release\dict`. The files which are already there are not downloaded again.

Hunspell dictionaries, taken from the [LibreOffice dictionaries](https://github.com/LibreOffice/dictionaries) (the French one from [Grammalecte](https://grammalecte.net/), see `README_dict_fr.txt`). Each one keeps its own license: the text of the licenses and the readmes of the authors are in `dict\licenses` once downloaded. They are data, used by Hunspell: they are not part of the program and are not under its license.

| File | Language | Source | License |
|---|---|---|---|
| `fr` | French | Grammalecte 7.7 | MPL 2.0 |
| `en_US`, `en_GB` | English | LibreOffice (`en`) | see `licenses/en-LICENSE.txt` and the readmes |
| `es_ES`, `es_AR` | Spanish | LibreOffice (`es`) | GPL 3, LGPL 3 or MPL 1.1 (your choice) |
| `de_DE` | German | LibreOffice (`de`, *frami*) | GPL 2 or 3, LGPL |
| `it_IT` | Italian | LibreOffice (`it_IT`) | GPL 3 |
| `pt_BR` | Portuguese (Brazil) | LibreOffice (`pt_BR`, VERO) | LGPL 3 or MPL |
| `pt_PT` | Portuguese (Portugal) | LibreOffice (`pt_PT`) | GPL, LGPL, MPL |
| `pl_PL` | Polish | LibreOffice (`pl_PL`, sjp.pl) | GPL 2, LGPL 2.1, MPL 1.1, Apache 2.0, CC BY 4.0 |
| `cs_CZ` | Czech | LibreOffice (`cs_CZ`) | see `licenses/cs_CZ-README.txt` |
| `ru_RU` | Russian | LibreOffice (`ru_RU`) | LGPL |

The files are not changed, except for the encoding: `de_DE` (from ISO 8859-1) and `pl_PL` (from ISO 8859-2) were converted to UTF-8 and `SET UTF-8` written in their `.aff`, because Binote gives UTF-8 words to Hunspell. `de_DE` is `de_DE_frami` under its short name. A byte order mark was removed from `pt_BR`.

There is no Hunspell dictionary for Chinese and Japanese (the typography works, the spelling does not). To add a language, copy its `.dic` and `.aff` (UTF-8) here or into `%AppData%\Binote\dict`; the dictionaries of LibreOffice and of the system are listed apart, in **Other dictionaries**.
