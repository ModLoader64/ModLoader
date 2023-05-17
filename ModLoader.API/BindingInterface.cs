﻿namespace ModLoader.API;

public interface BindingInterface
{

    /// <summary>
    /// EventHandler for if the bound program is reset by the user.
    /// </summary>
    public EventHandler<EventEmpty> OnBindingReset { get; set; }
    /// <summary>
    /// EventHandler for when the bound program renders a new frame.
    /// </summary>
    public EventHandler<EventNewFrame> OnNewFrame { get; set; }
    /// <summary>
    /// Access the bound program's memory.
    /// </summary>
    public Memory Memory { get; set; }

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
