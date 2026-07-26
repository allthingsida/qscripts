// Copyright (c) 2019-2026 Elias Bachaalany
// SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
//
// This file is licensed under the Human-Origin Source License v1.0.
// See LICENSE.

#pragma once

#pragma warning(push)
#pragma warning(disable: 4267 4244 4146)
#include <loader.hpp>
#include <idp.hpp>
#include <expr.hpp>
#include <prodir.h>
#include <kernwin.hpp>
#include <diskio.hpp>
#include <registry.hpp>
#include <idacpp/kernwin/kernwin.hpp>

using namespace idacpp::kernwin;
#pragma warning(pop)

// IDA 8.3
#ifndef CH_NOIDB
    #define CH_NOIDB CH_UNUSED
#endif
