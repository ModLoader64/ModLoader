//------------------------------------------------------------------------------
// <auto-generated>
//     This code was generated by a tool.
//
//     Changes to this file may cause incorrect behavior and will be lost if
//     the code is regenerated.
// </auto-generated>
//------------------------------------------------------------------------------

namespace ModLoader.API {
    
    
    public delegate void Init_Delegate();
    
    public delegate void Destroy_Delegate();
    
    public interface IPluginDelegates {
        
        public Init_Delegate Init {get; set;}
        public Destroy_Delegate Destroy {get; set;}
    }
}
