// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MRG0721

#pragma once

// Small callbacks shared by the long running parts of the core.
//
// They live in their own header so that features which are not file system
// operations (searching, for example) can use them without pulling in
// operations.hpp.

#include <functional>

namespace mxplorer {

/// Returning true asks the operation to stop at the next safe point. The
/// terminal front-end wires it to SIGINT; a Qt6 front-end would wire it to a
/// cancel button.
using CancelToken = std::function<bool()>;

} // namespace mxplorer
