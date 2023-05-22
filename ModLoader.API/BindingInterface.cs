namespace ModLoader.API;

public interface IBinding
{
    /// <summary>
    /// Initialize the binding.
    /// </summary>
    public static abstract void InitBinding();

    /// <summary>
    /// Starts the bound program.
    /// </summary>
    public static abstract void StartBinding();

    /// <summary>
    /// Stops the bound program.
    /// </summary>
    public static abstract void StopBinding();

    /// <summary>
    /// Sets the rom/iso for the bound program if applicable.
    /// </summary>
    /// <param name="file"></param>
    public static abstract void SetGameFile(string file);

    /// <summary>
    /// Change the rom/iso for the bound program if applicable.
    /// </summary>
    /// <param name="file"></param>
    /// <returns>If the change was successful</returns>
    public static abstract bool ChangeGameFile(string file);

    /// <summary>
    /// Pause the bound program if applicable.
    /// </summary>
    public static abstract void TogglePause();

    /// <summary>
    /// Save a state if applicable.
    /// </summary>
    /// <param name="file"></param>
    /// <returns>If the save was successful.</returns>
    public static abstract bool SaveState(string file);

    /// <summary>
    /// Load a state if applicable.
    /// </summary>
    /// <param name="file"></param>
    /// <returns>If the load was successful.</returns>
    public static abstract bool LoadState(string file);

}

/// <summary>
/// Required to be on the entry point of a bound program.
/// </summary>
[AttributeUsage(AttributeTargets.Class)]
public class BindingAttribute : Attribute
{

    public string name { get; set; }

    public BindingAttribute(string name)
    {
        this.name = name;
    }

}