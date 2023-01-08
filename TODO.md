# Ideas

## Allow dot folders

- Check for $(pwd)/.qscripts/<sourcefilename.depsfiles> first before checking the current directory
- This allows less pollution in the current folder

## Native bitness detection

Introduce an automatic 64 detection macro. We don't want two seperate deps files like this:

```
/triggerfile /keep $env:IDASDK$\bin\plugins\qplugin.dll
/triggerfile /keep $env:IDASDK$\bin\plugins\qplugin[64].dll
```

# TODO

- Isolate the timer code from the UI logic
- Automatically select the previous active script when the UI launches
- Restore the QScripts window layout when closed and re-opened
- Use OS specific file change monitors


- Refactor filemonitor into a utility class
-- Perhaps implement per OS on Win32, and as is for Linux/Mac
-- For Linux, perhaps call an external process like 'inotify-wait' ?


