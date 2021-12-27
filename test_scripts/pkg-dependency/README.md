This is an example where we have a dependency file that relies on a package.

The package (supposedly) is under development and you want to have its modules be reloaded when they change.

```
# Non package dependencies first
/reload import importlib;importlib.reload($basename$)
dep.py

# Package dependencies
/pkgbase C:\Python38\Lib\site-packages
/reload import importlib;import $pkgmodname$;importlib.reload($pkgmodname$)

$pkgbase$\PIL\BlpImagePlugin.py
$pkgbase$\PIL\ContainerIO.py
```

* We start by describing the local dependencies if any. Note the Python specific reload directive
* Then we use `pkgbase` to define the package base. This will help with the reload directive
* The reload directive uses the special variable `pkgmodname` that will use the package directory to formulate a Python module name
* Finally, we define our dependencies relative to their package base folder using the variable `pkgbase`
