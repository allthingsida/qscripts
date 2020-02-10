# What is QScripts?

`QScripts` is productivity tool and an alternative to IDA's "Recent scripts" (Alt-F9) and "Execute Scripts" (Shift-F2) facilities. `QScripts` allows you to develop and run any supporting scripting language from the comfort of your own favorite text editor.

![Quick introduction](docs/_resources/qscripts-vid-1.gif)

# Usage

Invoke `QScripts` from the plugins menu, press Ctrl-3 or its default hotkey Alt-Shift-F9.
When it runs, the scripts list might be empty. Just press `Ins` and select a script to add, or press `Del` to delete a script from the list.
`QScripts` shares the same scripts list as IDA's `Recent Scripts` window.

To execute a script, just press `ENTER` or double-click it. After running a script once, it will become the active script (shown in **bold**).

An active script will then be monitored for changes. If you modify the script in your favorite text editor and save it, then `QScripts` will execute the script for you automatically in IDA.

To deactivate a script, just press `Ctrl-D` or right-click and choose `Deactivate script monitor` from the `QScripts` window. When an active script becomes inactive, it will be shown in *italics*.

There are few options that can be configured in `QScripts`. Just press `Ctrl+E` or right-click and selection options:

* Script monitor interval: controls the refresh rate of the script change monitor. Ideally 500ms is a good amount of time to pick up script changes.
* Clear message window before execution: clear the message log before re-running the script. Very handy if you to have a fresh output log each time.
* Show file name when execution: display the name of the file that is automatically executed
* Execute the unload script function: A special function, if defined, called `__quick_unload_script` will be invoked before reloading the script. This gives your script a chance to do some cleanup (for example to unregister some hotkeys)

## Executing a script without activating it

It is possible to execute a script from `QScripts` without having to activate it. Just press `Shift-ENTER` on a script and it will be executed.

## Working with dependencies

It is possible to instruct `QScripts` to re-execute the active script if any of its dependent scripts are also modified. To use the automatic dependency system, please create a file named exactly like your active script but with the additional `.deps.qscripts` extension. In that file you put your dependent scripts full path.

When using Python, it would be helpful if we can also `reload` the changed dependent script from the active script automatically. To do that, simply add the directive line `/reload` along with the desired reload syntax. For example, here's a complete `.deps.qscripts` file with a `reload` directive:

```
/reload reload($basename$)
t2.py
//This is a comment
t3.py
```

So what happens now if we have an active file `t1.py` with the dependency file above?

1. Any time `t1.py` changes, it will be automatically re-executed in IDA. That's the default behavior of `QScripts` <= 1.0.5.
2. If the dependency index file `t1.py.deps.qscripts` is changed, then your new dependencies will be reloaded and the active script will be executed again.
3. If any dependency script file has changed, then the active script will re-execute. If you had a `reload` directive set up, then the modified dependency files will also be reloaded.

Please note that if each dependent script file has its own dependency index file, then QScripts will recursively make all the linked dependencies as part of the active script dependencies.

### Special variables in the dependency index file

* `$basename$`: This variable is expanded to the base name of the current dependency line
* `$env:EnvVariableName$`: `EnvVariableName` is expanded to its environment variable value if it exists or left unexpanded otherwise


## Using QScripts programmatically
It is possible to invoke `QScripts` from a script. For instance, in IDAPython, you can execute the last selected script with:

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
