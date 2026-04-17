// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

// This file MUST be compiled with -std=gnu2x -fms-extensions.
// It includes the client implementation exactly once, which registers
// all message types via __attribute__((constructor)).

// clang-format off
#include <gr_api_client_impl.h>
// clang-format on

#include "shim.h"
