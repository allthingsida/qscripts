# What is QScripts?

QScripts is productivity tool and an alternative to IDA's "Recent scripts" (Alt-F9) and "Execute Scripts" (Shift-F2) facilities. QScripts allows you to develop and run any supporting scripting language from the comfort of your own favorite text editor.

![Quick introduction](docs/_resources/qscripts-vid-1.gif)

# Usage

Invoke QScripts from the plugins menu, press Ctrl-3 or its default hotkey Alt-Shift-F9.
When it runs, the scripts list might empty. Just press "Ins" and select a script to add, or press "Del" to delete a script from the list.
QScripts shares the same scripts list as IDA's "Recent Scripts" window.

To execute a script, just press ENTER or double-click it. After running a script once, it will become the active script (shown in **bold**).

An active script (shown in **bold**) will then be monitored for changes. If you modify the script in your favorite text editor and save it, then QScripts will execute the script for you automatically in IDA.

To deactivate a script, just press Ctrl-D or right-click and choose "Deactivate" from the QScripts window. When an active script becomes inactive, it will be shown in *italics*.

There are few options that can be configured in QScripts. Just press Ctrl+E or right-click and selection options:

* Script monitor interval: controls the refresh rate of the script change monitor. Ideally 500ms is a good amount of time to pick up script changes.
* Clear message window before execution: clear the message log before re-running the script. Very handy if you to have a fresh output log each time.
* Show file name when execution: display the name of the file that is automatically executed
* Execute the unload script function: A special function, if defined, called `__quick_unload_script` will be invoked before reloading the script. This gives your script a chance to do some cleanup (for example to unregister some hotkeys)

## Using QScripts programmatically
It is possible to invoke QScripts from a script. For instance, in IDAPython, you can execute the last selected script with:

```python
load_and_run_plugin("qscripts", 1);
```

(note the run argument `1`)

If the script monitor is deactivated, you can programmatically activate it by running the plugin with argument `2`. To deactivate again, use run argument `3`.

# Installation

QScripts is written in C++ with IDA's SDK and therefore it should be deployed like a regular plugin. Copy the plugin binaries to either of those locations:
* `<IDA_install_folder>/plugins`
* `%APPDATA%\Hex-Rays/plugins`

Since the plugin uses IDA's SDK and no other OS specific functions, the plugin should be compilable for macOS and Linux just fine. I only provide MS Windows binaries. Please check the [releases page](https://github.com/0xeb/ida-qscripts/releases).
