#pragma once

#include <modloader/types.h>

extern "C" {

// On_Prepare precedes symbol binding and C++ constructors
void On_Prepare();

// Init/shutdown: after constructors / before destructors; dependencies initialize first and shut down last

#define MODLOADER_LIFECYCLE(field, arguments, pre, normal, post, event) \
    void pre arguments; \
    void normal arguments; \
    void post arguments;
#include <modloader/detail/lifecycle.def>
#undef MODLOADER_LIFECYCLE

}
