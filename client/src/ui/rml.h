#pragma once

#include "module.h"

namespace Rml {
class Context;
} // namespace Rml

void Rml_Register_Natives();
void Rml_Start();
void Rml_Stop();
void Rml_Context_Removed(Rml::Context* context);
