#include "idasdk.h"

bool main(size_t)
{
    auto nfuncs = get_func_qty();

    size_t c = 0;
    for (size_t i = 0; i < nfuncs; ++i)
    {
        func_t* func = getn_func(i);
        if (func->flags & FUNC_LIB)
            continue;

        ++c;
        qstring name;
        get_func_name(&name, func->start_ea);
        msg("%a: %s\n", func->start_ea, name.c_str());
    }

    msg("%zu function(s)\n", c);
	return true;
}