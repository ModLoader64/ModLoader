#pragma once

#include "module_execution.h"
#include "renderer/renderer.h"
#include "settings.h"
#include "ui/theme.h"

class Client_Settings {
public:
    explicit Client_Settings(Settings_Changed changed = nullptr);

    std::string Get(std::string_view key) const;
    Renderer_Scaling Scaling() const;
    Renderer_Filter Filter() const;
    Ui_Theme Theme() const;
    u16 Server_Port() const;
    Module_Execution Execution_Mode() const;

private:
    Settings_Group* group;
};
