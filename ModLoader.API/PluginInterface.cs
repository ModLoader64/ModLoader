namespace ModLoader.API;
public interface IPlugin
{
    /// <summary>
    /// Called when the plugin needs to be initialized.
    /// </summary>
    public static abstract void Init();

    /// <summary>
    /// Called when the plugin needs to be destroyed.
    /// </summary>
    public static abstract void Destroy();

}
