#include "base.h"
#include "session.h"

#include <stdio.h>

namespace {

void Print_Usage() {
    printf(
        "ModLoader\n"
        "\n"
        "usage: ModLoader --rom <file> [--mod <plugin.pak>]... [options]\n"
        "       ModLoader --server [--mod <plugin.pak>]... [options]\n"
        "\n"
        "  --server              dedicated server\n"
        "  --mod <file>           a .wasm or .pak\n"
        "  --adapter <library>   the emulator\n"
        "  --data <dir>          directory for userdata\n"
        "  --verbose             debug logging\n"
        "\n"
        "Online:\n"
        "  --connect <host[:port]>   override the configured server address and port\n"
        "  --nickname <name>         override the configured nickname\n"
        "  --lobby <name>            override the configured lobby\n"
        "  --password <text>         the lobby's password\n"
        "  --host <port>             host and play\n"
        "  --host-address <address>  the address to host on (defaults to every interface)\n"
        "\n"
        "Settings: <data>/config/ (client_settings.json, the emulator's and the plugins'), changed in the toolbar.\n"
        "Plugins and cores keep their files in <data>/mods/<name>/. F5 saves a state, F7 loads it.\n"
    );
}

} // namespace

int main(int argument_count, char** arguments) {
    Run_Options options;
    std::string_view argument;
    std::pair<std::string_view, std::string*> texts[] = {
        { "--rom", &options.romPath },       { "--adapter", &options.adapterPath },      { "--data", &options.dataDirectory },
        { "--connect", &options.connect },   { "--nickname", &options.nickname },        { "--lobby", &options.lobby },
        { "--password", &options.password }, { "--host-address", &options.hostAddress },
    };
    bool known;

    for (s32 index = 1; index < argument_count; index++) {
        argument = arguments[index];
        known = true;
        if (argument == "--verbose") {
            Log_Set_Level(Log_Level::Debug);
        }
        else if (argument == "--server") {
            options.server = true;
        }
        else if (argument == "--mod" && index + 1 < argument_count) {
            options.modulePaths.emplace_back(arguments[++index]);
        }
        else if (argument == "--host" && index + 1 < argument_count) {
            const auto port = Text_Parse_U64(arguments[++index]);
            if (!port || *port == 0 || *port > 65535) {
                Log_Error("ModLoader", "invalid port %s", arguments[index]);
                return 2;
            }
            options.host = true;
            options.hostPort = static_cast<u16>(*port);
        }
        else {
            known = false;
            for (const auto& [name, value] : texts) {
                if (argument == name && index + 1 < argument_count) {
                    *value = arguments[++index];
                    known = true;
                    break;
                }
            }
        }

        if (!known) {
            Log_Error("ModLoader", "unknown option %s", arguments[index]);
            Print_Usage();
            return 2;
        }
    }

    if (options.romPath.empty() && !options.server) {
        Print_Usage();
        return 2;
    }

    if (options.dataDirectory.empty()) {
        options.dataDirectory = Path_Join(Path_Executable_Directory(), "data");
    }

    Directory_Create_All(options.dataDirectory);
    return options.server ? Dedicated_Run(options) : Session_Run(options);
}
