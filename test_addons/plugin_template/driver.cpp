// Copyright (c) 2019-2026 Elias Bachaalany
// SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
//
// This file is licensed under the Human-Origin Source License v1.0.
// See LICENSE.

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
