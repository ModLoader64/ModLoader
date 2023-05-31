namespace ModLoader.API;

[AttributeUsage(AttributeTargets.Field)]
public class Signature : Attribute
{

    public string bytes = "";

    public Signature(string bytes)
    {
        this.bytes = bytes;
    }

}
