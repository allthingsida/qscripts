# Snipper Manager Plugin for IDA

This plugin for IDA provides a set of functionality for importing, exporting or deleting all snippets from IDA (Shift-F2 window).

## Functions

Type the following functions from IDA's CLI or just call them directly from Python:

- `idaapi.ext.save_snippets(output_folder='')`: Save snippets to a specified output folder.
- `idaapi.ext.load_snippets(input_folder='')`: Load snippets from a specified input folder.
- `idaapi.ext.delete_snippets()`: Delete all existing snippets.

By default, if no input or output folders are specified, then the plugin will default to the `os.path.join(os.path.dirname(idc.get_idb_path()), '.snippets')` folder.

## Installation

Copy the `snippetmanager.py` file to your IDA plugin folder of your choice.

Alternatively, for example on Windows, just make a symbolic link directly:

```batch
C:\Projects\ida-qscripts\snippet_manager>mklink c:\ida\plugins\snippetmanager.py %cd%\snippetmanager.py
```

_(Just for fun, I provided the [ChatGPT-4/Code Interpreter prompt](.temp/gpt4-prompt-readme.txt) used to bootstrap this readme file)_

## Very important

Due to the IDA UI's caching of snippets in memory, changes will not be immediately visible. Therefore, upon executing any of the functions, it is obligatory to close, save, and then reopen the database manually in order to observe the modifications.