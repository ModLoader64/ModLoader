namespace ModLoader.CLI;

internal class Service
{
    public static PluginLoader loader = new PluginLoader();
    public static BindingLoader bindingLoader = new BindingLoader();
    public static Server server = new Server();
    public static Client client = new Client();
    public static SignatureManager signatureManager = new SignatureManager();
    public static ImGuiManager ImGuiManager = new ImGuiManager();
}
