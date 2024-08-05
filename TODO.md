# Ideas

## Allow dot folders

- Check for $(pwd)/.qscripts/<sourcefilename.depsfiles> first before checking the current directory
- This allows less pollution in the current folder

# TODO

- Automatically select the previous active script when the UI launches
- Restore the QScripts window layout when closed and re-opened
- Use OS specific file change monitors

- It is time to re-write this, now that we have all the specs and features

# Monitor code and refactoring

- Isolate the timer code from the UI logic
    - Refactor filemonitor into a utility class
    - Perhaps implement per OS on Win32, and as is for Linux/Mac
    - For Linux, perhaps call an external process like 'inotify-wait' ?


