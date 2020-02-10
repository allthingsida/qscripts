This is a dependency test folder.

The script `t1.py` has its dependency index file which describes the reload directive and one dependency on `t2.py`:
```
/reload reload($basename$)
t2.py
```

OTOH, `t2.py` has a relative dependency on `t3.py` (in the same folder) and on `subdir/t4.py` in a sub-folder:
```
t3.py
subdir/t4.py
```

The script `t4.py` has a single relative dependency in its index file:
```
t5.py
```


If you activate `t1.py` from QScripts in IDA, then changing any of the files and their indices will re-invoke `t1.py` and perhaps `reload` the changed modules.