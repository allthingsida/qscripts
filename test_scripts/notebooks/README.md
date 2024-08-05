# Notebook dependency example

## Quick start

To define a notebook, just add the `/notebook [title]` directive as such:

```
/notebook Test notebook #B
```

Then, to select the files to be monitored, use the `/notebook.cells_re` directive, specifying a regular expression pattern to match the desired files:

```
/notebook.cells_re B\d{4}.*\.py$
```

In this example, the notebook will monitor all files that match the pattern `B\d{4}.*\.py$`, for example `B0001_test.py`, `B0002_test.py`, etc.

Now, when a notebook is activated, you have options to:

- Execute the main script (`exec_main`)
- All scripts (`exec_all`) 
- or no scripts (`exec_none`)

Using the `/notebook.activate` directive:

```
/notebook.activate exec_main
```

## Provided notebooks examples

- `0000 Imports and Init.py` - This notebook has its own dependency file that looks for the "nnnn *.py" Python files
- `A0000 Init.py` - This notebook has its own dependency file that looks for the "Annn *.py" Python files
- `B0000 Init.py` - This notebook has its own dependency file that looks for the "Bnnnn *.py" Python files

As you can see, it is possible to have various notebooks in the same folder, each with its own dependency file, as long as their `cells_re` configuration does not overlap.
