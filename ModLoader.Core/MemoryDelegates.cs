//------------------------------------------------------------------------------
// <auto-generated>
//     This code was generated by a tool.
//
//     Changes to this file may cause incorrect behavior and will be lost if
//     the code is regenerated.
// </auto-generated>
//------------------------------------------------------------------------------

namespace ModLoader.Core {
    
    
    public class MemoryDelegates : IMemoryDelegates {
        
        public ReadU8_Delegate ReadU8 { get; set; }//;
        
        public ReadU16_Delegate ReadU16 { get; set; }//;
        
        public ReadU32_Delegate ReadU32 { get; set; }//;
        
        public ReadU64_Delegate ReadU64 { get; set; }//;
        
        public ReadS8_Delegate ReadS8 { get; set; }//;
        
        public ReadS16_Delegate ReadS16 { get; set; }//;
        
        public ReadS32_Delegate ReadS32 { get; set; }//;
        
        public ReadS64_Delegate ReadS64 { get; set; }//;
        
        public ReadF32_Delegate ReadF32 { get; set; }//;
        
        public ReadF64_Delegate ReadF64 { get; set; }//;
        
        public WriteU8_Delegate WriteU8 { get; set; }//;
        
        public WriteU16_Delegate WriteU16 { get; set; }//;
        
        public WriteU32_Delegate WriteU32 { get; set; }//;
        
        public WriteU64_Delegate WriteU64 { get; set; }//;
        
        public WriteS8_Delegate WriteS8 { get; set; }//;
        
        public WriteS16_Delegate WriteS16 { get; set; }//;
        
        public WriteS32_Delegate WriteS32 { get; set; }//;
        
        public WriteS64_Delegate WriteS64 { get; set; }//;
        
        public WriteF32_Delegate WriteF32 { get; set; }//;
        
        public WriteF64_Delegate WriteF64 { get; set; }//;
        
        public MemoryDelegates(System.Type binding) {
            ReadU8 = DelegateHelper.Cast<ReadU8_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadU8", new Type[] {typeof(System.UInt32),})!, null));
            ReadU16 = DelegateHelper.Cast<ReadU16_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadU16", new Type[] {typeof(System.UInt32),})!, null));
            ReadU32 = DelegateHelper.Cast<ReadU32_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadU32", new Type[] {typeof(System.UInt32),})!, null));
            ReadU64 = DelegateHelper.Cast<ReadU64_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadU64", new Type[] {typeof(System.UInt32),})!, null));
            ReadS8 = DelegateHelper.Cast<ReadS8_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadS8", new Type[] {typeof(System.UInt32),})!, null));
            ReadS16 = DelegateHelper.Cast<ReadS16_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadS16", new Type[] {typeof(System.UInt32),})!, null));
            ReadS32 = DelegateHelper.Cast<ReadS32_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadS32", new Type[] {typeof(System.UInt32),})!, null));
            ReadS64 = DelegateHelper.Cast<ReadS64_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadS64", new Type[] {typeof(System.UInt32),})!, null));
            ReadF32 = DelegateHelper.Cast<ReadF32_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadF32", new Type[] {typeof(System.UInt32),})!, null));
            ReadF64 = DelegateHelper.Cast<ReadF64_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("ReadF64", new Type[] {typeof(System.UInt32),})!, null));
            WriteU8 = DelegateHelper.Cast<WriteU8_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteU8", new Type[] {typeof(System.UInt32),typeof(System.Byte),})!, null));
            WriteU16 = DelegateHelper.Cast<WriteU16_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteU16", new Type[] {typeof(System.UInt32),typeof(System.UInt16),})!, null));
            WriteU32 = DelegateHelper.Cast<WriteU32_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteU32", new Type[] {typeof(System.UInt32),typeof(System.UInt32),})!, null));
            WriteU64 = DelegateHelper.Cast<WriteU64_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteU64", new Type[] {typeof(System.UInt32),typeof(System.UInt64),})!, null));
            WriteS8 = DelegateHelper.Cast<WriteS8_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteS8", new Type[] {typeof(System.UInt32),typeof(System.SByte),})!, null));
            WriteS16 = DelegateHelper.Cast<WriteS16_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteS16", new Type[] {typeof(System.UInt32),typeof(System.Int16),})!, null));
            WriteS32 = DelegateHelper.Cast<WriteS32_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteS32", new Type[] {typeof(System.UInt32),typeof(System.Int32),})!, null));
            WriteS64 = DelegateHelper.Cast<WriteS64_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteS64", new Type[] {typeof(System.UInt32),typeof(System.Int64),})!, null));
            WriteF32 = DelegateHelper.Cast<WriteF32_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteF32", new Type[] {typeof(System.UInt32),typeof(System.Single),})!, null));
            WriteF64 = DelegateHelper.Cast<WriteF64_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod("WriteF64", new Type[] {typeof(System.UInt32),typeof(System.Double),})!, null));
        }
    }
}
