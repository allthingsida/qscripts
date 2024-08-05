# Package Dependency Example

In this example, we're dealing with a dependency file for a package that's currently being developed. This setup is designed to ensure that modules from the package, `idapyx` in this instance, are automatically reloaded upon any changes.

For this purpose, we can either explicitly specify the package's full path or refer to it through an environment variable, as demonstrated below:

```plaintext
# Define package base folder
/pkgbase $env:idapyx$
# Automatically reload the package's modules when they change
/reload import importlib;from $pkgparentmodname$ import $basename$ as __qscripts_autoreload__; importlib.reload(__qscripts_autoreload__)
# Specify the paths to the package modules that need to be reloaded if they change
$pkgbase$/idapyx/bin/pe/rtfuncs.py
$pkgbase$/idapyx/bin/pe/types.py
```

- The `/pkgbase` directive specifies the base path of the package, aiding in the module reload process.
- The reload directive employs the `pkgmodparentname` variable to derive the Python parent module name based on the package directory.
- Similarly, the reload directive utilizes the `basename` variable to determine the Python module name, again using the package directory.
- Dependencies are specified relative to their package base folder, facilitated by the `pkgbase` variable.
