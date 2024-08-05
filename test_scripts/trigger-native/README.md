# Native plugins and loaders template

## plugin_template

Template project to test regular plugins. Write your code in 'main.cpp'.

## plugin_triton

A template project for use with the [Triton DBA framework](https://github.com/jonathansalwan/Triton).
You need to set up Triton correctly before being able to use this plugin template. [triggerenv.bat](https://github.com/0xeb/useful-scripts/blob/master/tritonenv/README.md) can be of help.

## loader_template

Template project to test loader modules. It has mock `accept_file` and `load_file` methods.
Run IDA with the `-t` switch to start with an empty database.

Fill in your loader logic in the mock methods and test them individually.