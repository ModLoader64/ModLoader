using Microsoft.CSharp;
using ModLoader.API;
using System.CodeDom;
using System.CodeDom.Compiler;

namespace ModLoader.DynamicCodeGeneration;

/// <summary>
/// The idea here is we want things that should only exist once to be static, but we also need to reference them in an interface like manner. This is a problem.
/// So this code generates some delegate glue to make the static classes seem like instantiatable objects with an interface to map them out.
/// </summary>
public class Program
{
    public static void Main(string[] args)
    {
        GenerateBindingInterface(typeof(IBinding), "BindingDelegates", "../../../../ModLoader.API/BindingDelegates.cs");
        GenerateBindingInterface(typeof(IMemory), "MemoryDelegates", "../../../../ModLoader.API/MemoryDelegates.cs");
        GenerateBindingInterface(typeof(IPlugin), "PluginDelegates", "../../../../ModLoader.API/PluginDelegates.cs");

        GenerateBindingDelegates(typeof(IBinding), "BindingDelegates", "../../../../ModLoader.Core/BindingDelegates.cs");
        GenerateBindingDelegates(typeof(IMemory), "MemoryDelegates", "../../../../ModLoader.Core/MemoryDelegates.cs");
        GenerateBindingDelegates(typeof(IPlugin), "PluginDelegates", "../../../../ModLoader.Core/PluginDelegates.cs");
    }

    private static void GenerateBindingInterface(Type t, string className, string file) {
        Console.WriteLine($"Starting interface generation for {className}");
        CodeCompileUnit compileUnit = new CodeCompileUnit();
        CodeNamespace samples = new CodeNamespace("ModLoader.API");
        compileUnit.Namespaces.Add(samples);

        foreach (var m in t.GetMethods())
        {

            Console.WriteLine($"Generating delegate for {m.Name}");
            CodeTypeDelegate @delegate = new CodeTypeDelegate($"{m.Name}_Delegate");
            @delegate.Attributes = MemberAttributes.Public;
            foreach (var p in m.GetParameters())
            {
                @delegate.Parameters.Add(new CodeParameterDeclarationExpression(p.ParameterType, p.Name!));
            }
            @delegate.ReturnType = new CodeTypeReference(m.ReturnType);
            samples.Types.Add(@delegate);
        }

        CodeTypeDeclaration BindingInterface = new CodeTypeDeclaration($"I{className}");
        BindingInterface.IsInterface = true;
        BindingInterface.Attributes = MemberAttributes.Public;

        foreach (var m in t.GetMethods())
        {
            Console.WriteLine($"Generating interface member for {m.Name}");
            BindingInterface.Members.Add(new CodeSnippetTypeMember($"        public {m.Name}_Delegate {m.Name} {{get; set;}}"));
        }

        samples.Types.Add(BindingInterface);

        CSharpCodeProvider provider = new CSharpCodeProvider();
        string sourceFile = file;

        using (StreamWriter sw = new StreamWriter(sourceFile, false))
        {
            IndentedTextWriter tw = new IndentedTextWriter(sw, "    ");

            provider.GenerateCodeFromCompileUnit(compileUnit, tw,
                new CodeGeneratorOptions());

            tw.Close();
        }
    }

    private static void GenerateBindingDelegates(Type t, string className, string file)
    {
        Console.WriteLine($"Starting Binding generation for {className}");
        CodeCompileUnit compileUnit = new CodeCompileUnit();
        CodeNamespace samples = new CodeNamespace("ModLoader.Core");
        compileUnit.Namespaces.Add(samples);

        CodeTypeDeclaration BindingDelegates = new CodeTypeDeclaration(className);
        BindingDelegates.IsClass = true;
        BindingDelegates.TypeAttributes = System.Reflection.TypeAttributes.Public;
        BindingDelegates.BaseTypes.Add($"I{className}");

        CodeConstructor con = new CodeConstructor();
        con.Attributes = MemberAttributes.Public;
        con.Parameters.Add(new CodeParameterDeclarationExpression(typeof(Type), "binding"));
        foreach (var m in t.GetMethods())
        {
            Console.WriteLine($"Generating member for {m.Name}");
            CodeMemberField field = new CodeMemberField($"{m.Name}_Delegate", m.Name);
            field.Attributes = MemberAttributes.Public;
            // This is utterly stupid. Is there really no other way?
            field.Name += " { get; set; }//";
            BindingDelegates.Members.Add(field);

            Console.WriteLine($"Generating impl for {m.Name}");
            string types = $"new Type[] {{";
            foreach(var param in m.GetParameters())
            {
                types += $"typeof({param.ParameterType.FullName}),";
            }
            if (types.Last().Equals(","))
            {
                types = types.Remove(types.Length - 1, 1);
            }
            types += "}";
            string code = $"            {m.Name} = DelegateHelper.Cast<{m.Name}_Delegate>(DelegateHelper.CreateDelegate(binding.GetMethod(\"{m.Name}\", {types})!, null));";
            CodeSnippetStatement snip = new CodeSnippetStatement();
            snip.Value = code;
            con.Statements.Add(snip);
        }

        BindingDelegates.Members.Add(con);

        samples.Types.Add(BindingDelegates);

        CSharpCodeProvider provider = new CSharpCodeProvider();
        string sourceFile = file;

        using (StreamWriter sw = new StreamWriter(sourceFile, false))
        {
            IndentedTextWriter tw = new IndentedTextWriter(sw, "    ");

            provider.GenerateCodeFromCompileUnit(compileUnit, tw,
                new CodeGeneratorOptions());

            tw.Close();
        }
    }
}