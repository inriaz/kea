// Copyright (C) 2013  Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

/// @file
/// @brief Basic Callout Library
///
/// This is a test file for the LibraryManager test.  It produces a library
/// that allows for tests of the basic library manager functions.
///
/// The characteristics of this library are:
///
/// - Only the "version" framework function is supplied.
///
/// - A context_create callout is supplied.
///
/// - Three "standard" callouts are supplied corresponding to the hooks
///   "lm_one", "lm_two", "lm_three".  All do some trivial calculations
///   on the arguments supplied to it and the context variables, returning
///   intermediate results through the "result" argument. The result of
///   the calculation is:
///
///   @f[ (10 + data_1) * data_2 - data_3 @f]
///
///   ...where data_1, data_2 and data_3 are the values passed in arguments
///   of the same name to the three callouts (data_1 passed to lm_one,
///   data_2 to lm_two etc.) and the result is returned in the argument
///   "result".

#include <hooks/hooks.h>
#include <fstream>

using namespace isc::hooks;
using namespace std;

extern "C" {

// Callouts

int
context_create(CalloutHandle& handle) {
    handle.setContext("result", static_cast<int>(10));
    handle.setArgument("result", static_cast<int>(10));
    return (0);
}

// First callout adds the passed "data_1" argument to the initialized context
// value of 10.

int
lm_one(CalloutHandle& handle) {
    int data;
    handle.getArgument("data_1", data);

    int result;
    handle.getArgument("result", result);

    result += data;
    handle.setArgument("result", result);

    return (0);
}

// Second callout multiplies the current context value by the "data_2"
// argument.

int
lm_two(CalloutHandle& handle) {
    int data;
    handle.getArgument("data_2", data);

    int result;
    handle.getArgument("result", result);

    result *= data;
    handle.setArgument("result", result);

    return (0);
}

// Final callout subtracts the result in "data_3" and.

int
lm_three(CalloutHandle& handle) {
    int data;
    handle.getArgument("data_3", data);

    int result;
    handle.getArgument("result", result);

    result -= data;
    handle.setArgument("result", result);

    return (0);
}

// Framework functions

int
version() {
    return (BIND10_HOOKS_VERSION);
}

};

