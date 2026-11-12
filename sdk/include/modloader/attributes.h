#pragma once

#define MODLOADER_API __attribute__((visibility("default"))) // Export symbols shared with dependent modules
#define MLAPI extern "C" // Keep lifecycle entry points unmangled

