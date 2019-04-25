PROC=qscripts

include ../plugin.mak

# MAKEDEP dependency list ------------------
$(F)qscripts$(O) : $(I)loader.hpp $(I)idp.hpp $(I)expr.hpp  \
                   $(I)kernwin.hpp $(I)diskio.hpp $(I)registry.hpp \
                   qscripts.cpp
