namespace ModLoader.API;
/// <summary>
/// Required on the entry point class of a plugin.
/// </summary>
public class PluginAttribute : Attribute
{
    public string Name { get; set; }

    public PluginAttribute(string name)
    {
        Name = name;
    }
}