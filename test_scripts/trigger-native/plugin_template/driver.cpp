#include "idasdk.h"

extern bool main(size_t);

//--------------------------------------------------------------------------
struct plugin_ctx_t : public plugmod_t
{
    bool idaapi run(size_t arg) override
    {
        return main(arg);
    }
};

//--------------------------------------------------------------------------
plugin_t PLUGIN =
{
  IDP_INTERFACE_VERSION,
  PLUGIN_UNL | PLUGIN_MULTI,
  []()->plugmod_t* {return new plugin_ctx_t; },
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  "QScripts native plugin driver",
  nullptr,
};
