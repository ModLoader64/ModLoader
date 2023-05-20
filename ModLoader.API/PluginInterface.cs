namespace ModLoader.API;
public interface PluginInterface
{
    /// <summary>
    /// Called when the plugin needs to be initialized.
    /// </summary>
    void Init();

    /// <summary>
    /// Called when the plugin needs to be destroyed.
    /// </summary>
    void Destroy();

}
