#pragma once

// Host imports and runtime entry points
#define MODLOADER_IMPORT(name) __attribute__((import_module("modloader"), import_name(name)))
#define MODLOADER_EXPORT(name) __attribute__((export_name(name)))
#define MODLOADER_API __attribute__((visibility("default"))) // Export symbols shared with dependent modules
#define MLAPI extern "C" // Keep lifecycle entry points unmangled

