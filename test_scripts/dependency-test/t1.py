import sys

subdir = os.path.join(os.path.dirname(__file__), 'subdir')
if subdir not in sys.path:
    print("-->adding to path: %s" % subdir)
    sys.path.append(subdir)

import datetime
import t2, t3, t4, t5

print("--- %s; this is %s.." % (datetime.datetime.now(), __file__))

t2.f2()
t3.f3()
t4.f4()
t5.f5()