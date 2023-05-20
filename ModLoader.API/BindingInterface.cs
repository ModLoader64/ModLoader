namespace ModLoader.API;

public interface BindingInterface
{
    /// <summary>
    /// Initialize the binding.
    /// </summary>
    public void InitBinding();

    /// <summary>
    /// Starts the bound program.
    /// </summary>
    public void StartBinding();

    /// <summary>
    /// Stops the bound program.
    /// </summary>
    public void StopBinding();

    /// <summary>
    /// Sets the rom/iso for the bound program if applicable.
    /// </summary>
    /// <param name="file"></param>
    public void SetGameFile(string file);

    /// <summary>
    /// Change the rom/iso for the bound program if applicable.
    /// </summary>
    /// <param name="file"></param>
    /// <returns>If the change was successful</returns>
    public bool ChangeGameFile(string file);

    /// <summary>
    /// Pause the bound program if applicable.
    /// </summary>
    public void TogglePause();

    /// <summary>
    /// Save a state if applicable.
    /// </summary>
    /// <param name="file"></param>
    /// <returns>If the save was successful.</returns>
    public bool SaveState(string file);

    /// <summary>
    /// Load a state if applicable.
    /// </summary>
    /// <param name="file"></param>
    /// <returns>If the load was successful.</returns>
    public bool LoadState(string file);

}

/// <summary>
/// Required to be on the entry point of a bound program.
/// </summary>
[AttributeUsage(AttributeTargets.Class)]
public class BindingAttribute : Attribute
{

    public string name { get; set; }
    public BindingInterface? instance { get; set; }

    public BindingAttribute(string name)
    {
        this.name = name;
    }

}