# Building Binote

## What you need

| Item | Notes |
|---|---|
| A C++17 compiler | Visual Studio (the `binote.sln` solution), or any MSVC toolchain with CMake |
| CMake 3.16 or later | with Ninja or the Visual Studio generator |
| The folder of the third party libraries | see below |
| Python 3.12 or later | **optional**: only to run scripts and the grammar engine; Binote runs without it |

Binote is built and tested on Windows. The CMake file has a branch for other systems (Python development files, no Windows libraries) but the interface code and the link line of the third party libraries are written for Windows; Linux is not done.

## The third party libraries

Dear ImGui and ImGuiColorTextEdit are in the repository, in `third/`: `third/imgui-1.92.1` (Dear ImGui and its GLFW and OpenGL3 backends) and `third/ImGuiColorTextEdit` (`TextEditor.cpp` and its header, the editor of the scripts). They keep their own licenses.

GLFW and the Python headers are not in the repository. They are read from one folder, which CMake calls `BINOTE_THIRD_DIR` and the Visual Studio project `ThirdPartyDir`. By default it is `../../openpostpro/openpostpro` relative to `binote-app` (a folder shared with another project of the same author). The files Binote reads from it:

| Path in the folder | Content |
|---|---|
| `src/third/GLFW/` | the GLFW headers |
| `lib/glfw/glfw3.lib` | the GLFW library |
| `src/third/python/313/` | the Python headers (stable ABI, `Py_LIMITED_API` 3.12) |
| `lib/python/python3.lib` | the import library of `python3.dll` (Windows) |

Other libraries come with the repository: Hunspell (`binote-app/third/hunspell`, compiled with Binote), md4c (`imgui-md/third/md4c`), and the fonts (`binote-app/fonts`).

To use another folder: `-DBINOTE_THIRD_DIR=C:/path/to/third` (CMake), or set `ThirdPartyDir` in `binote-app.vcxproj` (Visual Studio).

## With CMake and Ninja

Open a Visual Studio developer prompt (or call `vcvars64.bat`), then:

```
cd binote-app
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is `binote-app/build/binote.exe`. The default build type is Release. The two components (`imgui-md`, `imgui-prose`) are built as subprojects, without their demos.

## With Visual Studio

Open `binote.sln`, choose `Release` and `x64`, build. From a prompt:

```
MSBuild.exe binote.sln /p:Configuration=Release /p:Platform=x64
```

## What is copied beside the executable

At every build (also when only a resource changed) the folders below are copied to the folder of the executable:

| Folder | Content |
|---|---|
| `fonts/` | the DejaVu and Roboto fonts |
| `dict/` | the Hunspell dictionaries: French in the repository, the others (English, Spanish, German, Italian, Portuguese, Polish, Czech, Russian) downloaded by the build into `bin/Release/dict` with their licenses (`tools/fetch-dictionaries.ps1`, needs PowerShell and a network the first time; without them the build goes on with French only) |
| `icons/` | the icons |
| `scripts/` | the delivered Python scripts and their library |
| `lexicon/` | the lexicon of the C++ grammar checker |
| `lang/` | the interface languages |
| `grammar-engine/` | the Python grammar engine (the package itself), copied from `../grammar-engine/grammar_engine` |

The dictionaries are also searched in `%AppData%\Binote\dict`, and in the extensions of LibreOffice (listed apart in the menu).

## Python is optional

Binote does not link against a fixed Python: `python3.dll` is delay-loaded, so the executable starts on a machine without Python. The first script, or the first grammar check, loads it. When it is not there the scripts are unavailable and the grammar falls back on the C++ checker (`src/analysis/grammar.*`). Python is not needed to build.

## Checking the engine alone

The grammar engine is independent: `cd grammar-engine`, then `python -m unittest discover -s tests`. No build is needed.
