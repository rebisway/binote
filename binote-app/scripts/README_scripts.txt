Binote scripts
==============

Python 3.12 or later must be installed on the computer. The Scripts menu lists the scripts by category; a click opens the dialog
box of the script, OK runs it. Scripts > Edit the scripts opens the editor: edit, check the syntax, run (test) a script, make a new one
or a copy of a delivered script (the delivered files are read only; a script of your folder replaces a delivered one with the same name).
Your scripts are in %AppData%\Binote\scripts (Scripts > Open the scripts folder).

A script is a file with a class Textscript (fix_spaces.py is the model to copy):

    get_description()   text of the menu              get_category()   'text', 'typography', 'structure', 'analysis' or any name
    set_language(code)  'fr-fr', 'en-us', 'es-es'... language of the interface
    set_core(core)      the Textcore object: keep it
    get_properties_count(), get_property_key(i), get_property_description(i), get_property(i), set_property(i, value)
                        the parameters shown in the dialog box. The type returned by get_property(i) decides the control:
                        bool = check box; int, float, str = box; list = choice (first item: selected index as a text, then the choices).
                        Optional: get_property_file(i) -> ('open' or 'save', 'Name', '*.ext') and get_property_multiline(i) -> True
    use_properties()    False if the script displays its own dialog box (tkinter) in run() (see count_words.py)
    run()               does the work with the core

The part of the text to work on (selection, paragraph, section, file or whole project) is a parameter of the script: see the examples.

The core (lib/textcore.py):
    core.selection, core.paragraph, core.section, core.file, core.file_name, core.files (dict name -> text),
    core.language (interface), core.text_language (text of the project), core.markdown
    core.transform(function, scope)   apply a function text -> text to a scope: 'selection', 'paragraph', 'section', 'file', 'project'
    core.replace_selection(text), replace_paragraph(text), replace_section(text), replace_file(text), replace_in(name, text)
    core.text_of(scope), core.log(text), core.message(text), core.open_file(...), core.save_file(...)

The changes are applied after run() returns, in one step for the current file (Ctrl+Z undoes it); the other files of a project are written
in their destination. What the script prints goes to the log of Binote. An error shows its traceback in the log and the editor marks the line.
The values of the parameters are kept from one run to the next.
