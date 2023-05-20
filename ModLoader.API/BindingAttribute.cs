namespace ModLoader.API;

/// <summary>
/// Required to be on the entry point of a bound program.
/// </summary>
public class BindingAttribute : Attribute
{

    public string name { get; set; }
    public BindingInterface? instance { get; set; }

    public BindingAttribute(string name)
    {
        this.name = name;
    }

}