# Binote

*English version: [README.md](README.md).*

**Binote est un éditeur de bureau pour réécrire, réviser et relire un texte, paragraphe par paragraphe.** L'original reste à gauche, en lecture seule ; votre version est à droite. Binote garde les deux alignés, montre ce qui a changé et vous aide à soigner le résultat : correcteurs orthographique et grammatical, typographie française, analyse de style, scripts Python, historique de chaque version et export.

Texte brut ou Markdown, un seul fichier ou un livre entier (un fichier par chapitre). Tout se passe sur votre ordinateur : pas de compte, pas d'envoi de texte, pas de modèle d'IA.

![Binote, thème clair](docs/images/main-light.png)

## Objectif

Binote sert à réécrire un texte paragraphe par paragraphe en gardant l'original sous les yeux : reprendre un brouillon, polir une traduction, réviser un livre chapitre par chapitre. Il sert aussi d'éditeur ordinaire, en simple vue, pour écrire, relire ou lire un texte seul.

## Fonctions

**Vues et navigation**
- **Double vue ou simple vue** (la liste déroulante de la barre d'outils) : *Original*, *Travail* ou les deux. En double vue, les paragraphes restent face à face même quand vous en ajoutez ou en retirez ; les deux volets défilent ensemble.
- **Projets** : un fichier seul, ou un dossier de chapitres avec un plan en arbre (`binote.json`) ; numéros de lignes et de paragraphes, paragraphe courant surligné, aller à une ligne, sommaire.
- **Aperçu Markdown** avec images et diagrammes ; les **liens** sont suivis (un fichier Markdown s'ouvre dans Binote, une adresse web ouvre le navigateur).
- Thèmes clair et sombre, interface en anglais, en français et en espagnol.
- **Texte chinois, japonais et coréen** : une police de secours (trouvée sur le système, ou choisie dans les préférences), les règles de coupure de ligne de l'écriture est-asiatique (kinsoku) et la position du curseur transmise à la méthode de saisie.

**Correction et typographie**
- **Orthographe** (Hunspell) et **grammaire** : un moteur Python de règles indépendantes, un fichier par langue, avec un étiqueteur et un lexique pour le français, l'anglais et l'espagnol (les variantes comme l'espagnol argentin dérivent de leur langue). Les fautes sont soulignées et listées avec leurs numéros de ligne. Le moteur est un [composant séparé](grammar-engine).
- **Analyse de style** : répétitions, formules toutes faites, régularité des phrases, vocabulaire, lisibilité… des observations avec les passages colorés dans le texte, et des conseils. Une indication, jamais un verdict.
- **Typographie française** pendant la frappe (« », espaces insécables, ’ et …) ou à la demande pour un paragraphe ou un chapitre.
- **Caractères invisibles** affichés avec une couleur par type (espaces insécables, fines et de largeur nulle, trait d'union conditionnel…), et un tableau des caractères.

**Outils de travail**
- **Recherche et remplacement** avec expressions régulières et séquences d'échappement, dans le fichier ou dans tous les chapitres.
- **Scripts Python** applicables à la sélection, à un paragraphe, à une section, à un fichier ou au projet, avec une boîte de dialogue, un éditeur et un aperçu ; des scripts livrés pour les guillemets, les majuscules des titres, les listes, les espaces insécables…
- **Notes** sur les paragraphes, **noms propres** relevés dans le projet, **statistiques** et **objectifs** en mots.
- **Historique** de chaque version enregistrée, avec les différences et la restauration.
- **Git et Subversion** (état, commit, mise à jour, push, journal, différences).
- **Export** en PDF, Word, EPUB 3, HTML ou en un seul fichier Markdown, sans bibliothèque externe.

<p>
<img src="docs/images/problems.png" width="49%" alt="Problèmes d'orthographe et de grammaire">
<img src="docs/images/style.png" width="49%" alt="Analyse de style">
</p>

Texte japonais et chinois, en double vue (les premières lignes de *Je suis un chat* et du *Journal d'un fou*, dans le domaine public) :

<p>
<img src="docs/images/cjk-japanese.png" width="49%" alt="Texte japonais">
<img src="docs/images/cjk-chinese.png" width="49%" alt="Texte chinois">
</p>

## Pour commencer

1. Compilez Binote (ci-dessous) et lancez `binote.exe`.
2. **Fichier > Ouvrir un fichier** (un `.md` ou un `.txt`) ou **Ouvrir un dossier** (un fichier par chapitre). La première fois, Binote demande où écrire votre version et crée un `binote.json`.
3. Écrivez dans le volet de droite. La liste déroulante de la barre d'outils choisit la vue ; **Ctrl+F7** et **Ctrl+F8** activent l'orthographe et la grammaire ; **F7** corrige la typographie du paragraphe.

Un projet d'exemple (les premiers chapitres d'*Alice au pays des merveilles*, en anglais, et une courte réécriture) est dans [`docs/sample`](docs/sample) ; voir [`docs/SAMPLE_CREDITS.txt`](docs/SAMPLE_CREDITS.txt).

## Compilation

Binote est écrit en C++17 avec [Dear ImGui](https://github.com/ocornut/imgui) et GLFW. Il se compile avec Visual Studio (ouvrez `binote.sln`) ou avec CMake :

```
cd binote-app
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Dear ImGui et ImGuiColorTextEdit sont dans `third/`, Hunspell dans `binote-app/third`. GLFW et les en-têtes de Python sont lus dans le dossier donné par `BINOTE_THIRD_DIR` (CMake) ou `ThirdPartyDir` (Visual Studio). **Python 3.12 ou plus récent est facultatif** : il est chargé la première fois qu'un script ou le correcteur grammatical sert. Le dossier `grammar-engine` est copié à côté de l'exécutable par la compilation. Binote est compilé et testé sous Windows. Tous les détails sont dans [docs/BUILD.md](docs/BUILD.md) (en anglais).

## Documentation

La documentation est en anglais, sauf la référence de l'API du moteur grammatical, qui existe aussi en français.

- [Manuel de l'utilisateur](docs/MANUAL.md)
- [Architecture](docs/ARCHITECTURE.md) : l'organisation de `binote-app/src`, le flux de données, la passerelle vers Python
- [Compilation](docs/BUILD.md) : prérequis, bibliothèques tierces, CMake, Visual Studio
- [Tests](docs/TESTING.md) : le pont de test et ses commandes, les tests du moteur, les captures d'écran
- [Langues de l'interface](docs/LANGUAGES.md) : le format des fichiers de langue, comment ajouter une langue
- [Scripts](binote-app/scripts/README_scripts.txt) : le contrat d'un script
- **Le moteur grammatical** : [README](grammar-engine/README.md), [référence de l'API](grammar-engine/docs/API.fr.md) ([English](grammar-engine/docs/API.en.md)), [architecture](grammar-engine/docs/ARCHITECTURE.md), [écrire une langue](grammar-engine/docs/WRITING_A_LANGUAGE.md), [format du lexique](grammar-engine/docs/LEXICON.md), [fabriquer les lexiques](grammar-engine/docs/BUILDING_LEXICONS.md), [règles](grammar-engine/docs/RULES.md), [exemples d'interfaces en six langages](grammar-engine/examples/README.md)

## Organisation du dépôt

| Dossier | Contenu |
| --- | --- |
| `binote-app` | L'application (`src/ui`, `src/common`, `src/project`, `src/analysis`, `src/export`, `src/script`, `src/vcs`), dictionnaires, langues, scripts |
| `grammar-engine` | Le correcteur grammatical (Python, MIT) : règles par langue, étiqueteur, lexiques, exemples. Indépendant de Binote |
| `imgui-md` | Composant Markdown pour Dear ImGui (MIT), indépendant |
| `imgui-prose` | Composant d'édition de prose pour Dear ImGui (MIT), indépendant |
| `third` | Dear ImGui et ImGuiColorTextEdit, avec leurs propres licences |
| `docs` | Manuel, captures d'écran, projet d'exemple |

## Licences

Binote est un logiciel libre sous **licence publique générale GNU Affero 3.0 ou ultérieure** (voir [LICENSE](LICENSE)). `imgui-md`, `imgui-prose` et `grammar-engine` sont sous licence MIT. Les composants tiers gardent leur licence : Dear ImGui, GLFW, md4c, stb, ImGuiColorTextEdit (MIT), Hunspell (MPL/GPL/LGPL), le lexique de Grammalecte (MPL 2.0), les polices DejaVu et Roboto, et les lexiques anglais et espagnol du moteur grammatical (issus de Wiktionnaire, CC BY-SA 4.0) ; les mentions sont dans [grammar-engine/THIRD_PARTY_LICENSES.md](grammar-engine/THIRD_PARTY_LICENSES.md).

© 2026 Thomas Gourgnier - [www.rebisway.com](https://www.rebisway.com)
