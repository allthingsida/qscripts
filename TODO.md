- Isolate the timer code from the UI logic
- Automatically select the previous active script when the UI launches
- Restore the QScripts window layout when closed and re-opened
- Use OS specific file change monitors
- Refactor filemonitor into a utility class
-- Perhaps implement per OS on Win32, and as is for Linux/Mac


--> introduce an automatic 64 detection macro
/triggerfile /keep $env:IDASDK$\bin\plugins\qplugin.dll
/triggerfile /keep $env:IDASDK$\bin\plugins\qplugin[64].dll
