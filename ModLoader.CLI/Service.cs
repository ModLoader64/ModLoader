using ModLoader.Core;

namespace ModLoader.CLI;

internal class Service
{
    public static PluginLoader loader = new PluginLoader();
    public static BindingLoader bindingLoader = new BindingLoader();
}
