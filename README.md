# What is QScripts?

QScripts is productivity tool and an alternative to IDA's "Recent scripts" (Alt-F9) and "Execute Scripts" (Shift-F2) facilities. QScripts allows you to develop and run any supported scripting language (\*.py; \*.idc, etc.) from the comfort of your own favorite text editor as soon as you save the active script, the trigger file or any of its dependencies.

![Quick introduction](docs/_resources/qscripts-vid-1.gif)

Video tutorials on the [AllThingsIDA](https://www.youtube.com/@allthingsida) YouTube channel:

- [Boost your IDA programming efficiency tenfold using the ida-qscripts productivity plugin](https://youtu.be/1UEoLAgEGMc?si=YMieIKHEY0AXgMHU)
- [Scripting concepts and productivity tips for IDAPython & IDC](https://youtu.be/RgHmwHN0NLk?si=OCnLMhcAmHAQPgNI)
- [An exercise in netnodes with the snippet manager plugin](https://youtu.be/yhVdLYzFJW0?si=z3xMqCEFOU89gAkI)

# Usage

Invoke QScripts from the plugins menu, press Ctrl-3 or its default hotkey Alt-Shift-F9.
When it runs, the scripts list might be empty. Just press `Ins` and select a script to add, or press `Del` to delete a script from the list.
QScripts shares the same scripts list as IDA's `Recent Scripts` window.

To execute a script, just press `ENTER` or double-click it. After running a script once, it will become the active script (shown in **bold**).

An active script will then be monitored for changes. If you modify the script in your favorite text editor and save it, then QScripts will execute the script for you automatically in IDA.

To deactivate the script monitor, just press `Ctrl-D` or right-click and choose `Deactivate script monitor` from the QScripts window. When an active script becomes inactive, it will be shown in *italics*.

There are few options that can be configured in QScripts. Just press `Ctrl+E` or right-click and select `Options`:

* Clear message window before execution: clear the message log before re-running the script. Very handy if you to have a fresh output log each time.
* Show file name when execution: display the name of the file that is automatically executed
* Execute the unload script function: A special function, if defined in the global scope (usually by your active script), called `__quick_unload_script` will be invoked before reloading the script. This gives your script a chance to do some cleanup (for example to unregister some hotkeys)
* Script monitor interval: controls the refresh rate of the script change monitor. Ideally 500ms is a good amount of time to pick up script changes.
* Allow QScripts execution to be undo-able: The executed script's side effects can be reverted with IDA's Undo.

## Executing a script without activating it

It is possible to execute a script from QScripts without having to activate it. Just press `Shift-Enter` on a script and it will be executed (disregarding if there's an active script or not).

## Managing Dependencies in QScripts

QScripts offers a feature that allows automatic re-execution of the active script when any of its dependent scripts, undergo modifications.

### Setting Up Automatic Dependencies

To leverage the automatic dependency tracking feature, create a file named identically to your active script, appending `.deps.qscripts` to its name. This file should contain paths to dependent scripts, along with any necessary reload directives.

Optionally, you can place the `.deps.qscripts` file within a `.qscripts` subfolder, located alongside your active script.

### Integrating Python Scripts

For projects involving Python, QScripts can automatically [reload](https://docs.python.org/3/library/importlib.html#importlib.reload) any changed dependent Python scripts. Include a `/reload` directive in your `.deps.qscripts` file, followed by the appropriate Python reload syntax.

#### Example `.deps.qscripts` file for `t1.py`:

```txt
/reload import importlib; import $basename$; importlib.reload($basename$);
t2.py
# This is a comment
t3.py
```

The `t1.py.deps.qscripts` configuration enables the following behavior:

1. **Script Auto-Execution**: Changes to `t1.py` trigger its automatic re-execution within the IDA environment.
2. **Dependency Reload**: Modifications to the dependency index file (`t1.py.deps.qscripts`) lead to the reloading of specified dependencies, followed by the re-execution of the active script.
3. **Dependency Script Changes**: Any alteration in a dependency script file causes the active script to re-execute. If a reload directive is present, the modified dependency files are also reloaded. In our cases, if either or both of `t2.py` and `t3.py` are modified, `t1.py` is re-executed and the modified dependencies are reloaded as well.

**Note**: If a dependent script possesses its own `.deps.qscripts` file, QScripts recursively integrates all linked dependencies into the active script's dependencies. However, specific directives (e.g., `reload`) within these recursive dependencies are disregarded.

See also:

* [Simple dependency example](test_scripts/dependency-test/README.md)
* [Package dependency example](test_scripts/pkg-dependency/README.md)

### Special variables in the dependency index file

* `$basename$`: This variable is expanded to the base name of the current dependency line
* `$env:EnvVariableName$`: `EnvVariableName` is expanded to its environment variable value if it exists or left unexpanded otherwise
* `$pkgbase$`: Specify a package base directory. Can be used as part of a dependency file path.
* `$pkgparentmodname$` and `$pkgmodname$`: These are mainly used inside the `reload` directive. They help with proper [package dependency](test_scripts/pkg-dependency/README.md) reloading.
* `$ext$`: This resolves to the plugin suffix and extension ("64.dll", ".so", "64.dylib", etc.). See the trigger native deps files for reference.

## Using QScripts like a Jupyter notebook

It is possible to use QScripts as if you were working in a regular Jupiter notebook. Your `.deps.qscripts` file should have the `/notebook` keyword. This allows you to monitor a folder, where each file in that folder is considered a cell in the notebook. When you save a file, the last saved cell will be re-executed.

See also:

* [Notebooks dependency example](test_scripts/notebooks/README.md)

## Using QScripts with trigger files

Sometimes you don't want to trigger QScripts when your scripts are saved, instead you want your own trigger condition.
One way to achieve a custom trigger is by using the `/triggerfile` directive:

```
/triggerfile createme.tmp

; Dependencies...
dep1.py
```

This tells QScripts to wait until the trigger file `createme.tmp` is created (or modified) before executing your script. Now, any time you want to execute the active script, just create (or modify) the trigger file.


You may pass the `/keep` option so QScripts does not delete your trigger file, for example:

```
/triggerfile /keep dont_del_me.info
```

## Using QScripts programmatically

It is possible to invoke QScripts from a script. For instance, in IDAPython, you can execute the last selected script with:

```python
load_and_run_plugin("qscripts", 1);
```

(note the run argument `1`)

If the script monitor is deactivated, you can programmatically activate it by running the plugin with argument `2`. To deactivate again, use run argument `3`.

## Using QScripts with compiled code

QScripts is not designed to work with compiled code, however using a combination of tricks, we can use QScripts for such cases:

![Compiled code](docs/_resources/trigger_native.gif)

What you just saw was the `hello` sample from the IDA SDK. This plugin has the `PLUGIN_UNL` flag. This flag tells IDA to unload the plugin after each invocation.
We can then use the trigger files option and specify the compiled binary path as the trigger file. Additionally, we need to write a simple script that loads and runs that newly compiled plugin in IDA.

First, let's start with the script that we need to activate and run:

```python
# Optionally clear the screen:
idaapi.msg_clear()

# Load your plugin and pass any arg value you want
idaapi.load_and_run_plugin('hello', 0)

# Optionally, do post work, etc.
# ...
```

Then let's create the dependency file with the proper trigger file configuration:

```
/triggerfile /keep C:\<ida_dir>\plugins\hello.dll
```

Now, simply use your favorite IDE (or terminal) and build (or rebuild) the `hello` sample plugin.

The moment the compilation succeeds, the new binary will be detected (since it is the trigger file) then your active script will use IDA's `load_and_run_plugin()` to run the plugin again.

Please check the [trigger-native](test_scripts/trigger-native/) example.

# Building

QScripts uses [idax](https://github.com/0xeb/idax) and is built using [ida-cmake](https://github.com/0xeb/ida-cmake).

If you don't want to build from sources, then there are release pre-built for MS Windows.

# Installation

QScripts is written in C++ with IDA's SDK and therefore it should be deployed like a regular plugin. Copy the plugin binaries to either of those locations:

* `<IDA_install_folder>/plugins`
* `%APPDATA%\Hex-Rays/plugins`

Since the plugin uses IDA's SDK and no other OS specific functions, the plugin should be compilable for macOS and Linux just fine. I only provide MS Windows binaries. Please check the [releases page](https://github.com/0xeb/ida-qscripts/releases).

# BONUS

## Snippet Manager

QScripts ships with a simple [Snippet Manager](snippet_manager/README.md) plugin to allow you to manage script snippets.