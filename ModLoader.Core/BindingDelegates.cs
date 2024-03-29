//------------------------------------------------------------------------------
// <auto-generated>
//     This code was generated by a tool.
//
//     Changes to this file may cause incorrect behavior and will be lost if
//     the code is regenerated.
// </auto-generated>
//------------------------------------------------------------------------------

namespace ModLoader.Core {
    
    
    public class BindingDelegates : IBindingDelegates {
        
        public InitBinding_Delegate InitBinding { get; set; }//;
        
        public StartBinding_Delegate StartBinding { get; set; }//;
        
        public StopBinding_Delegate StopBinding { get; set; }//;
        
        public SetGameFile_Delegate SetGameFile { get; set; }//;
        
        public ChangeGameFile_Delegate ChangeGameFile { get; set; }//;
        
        public TogglePause_Delegate TogglePause { get; set; }//;
        
        public SaveState_Delegate SaveState { get; set; }//;
        
        public LoadState_Delegate LoadState { get; set; }//;
        
        public BindingDelegates(System.Type binding) {
            InitBinding = DelegateHelper.Cast<InitBinding_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("InitBinding", new Type[] {})!, null));
            StartBinding = DelegateHelper.Cast<StartBinding_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("StartBinding", new Type[] {})!, null));
            StopBinding = DelegateHelper.Cast<StopBinding_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("StopBinding", new Type[] {})!, null));
            SetGameFile = DelegateHelper.Cast<SetGameFile_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("SetGameFile", new Type[] {typeof(System.String),})!, null));
            ChangeGameFile = DelegateHelper.Cast<ChangeGameFile_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ChangeGameFile", new Type[] {typeof(System.String),})!, null));
            TogglePause = DelegateHelper.Cast<TogglePause_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("TogglePause", new Type[] {})!, null));
            SaveState = DelegateHelper.Cast<SaveState_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("SaveState", new Type[] {typeof(System.String),})!, null));
            LoadState = DelegateHelper.Cast<LoadState_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("LoadState", new Type[] {typeof(System.String),})!, null));
        }
    }
}
