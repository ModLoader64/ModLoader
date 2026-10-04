#pragma once

#include "base.h"

struct ImGuiStyle;

enum class Ui_Theme : u32 {
    Dark,
    Light,
};

Ui_Theme Ui_Theme_From_Name(std::string_view name);
void Ui_Theme_Style(Ui_Theme theme, ImGuiStyle& style);
