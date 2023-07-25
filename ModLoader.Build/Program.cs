using Cake.Common;
using Cake.Common.IO;
using Cake.Common.Tools.DotNet.Build;
using Cake.Common.Tools.DotNet;
using Cake.Core;
using Cake.Frosting;
using System.IO.Compression;
using System.IO;
using System;
using System.Collections.Generic;

public static class Program
{
    public static int Main(string[] args)
    {
        return new CakeHost()
            .UseContext<BuildContext>()
            .Run(args);
    }
}

public class BuildContext : FrostingContext
{
    public string MsBuildConfiguration { get; set; }

    public BuildContext(ICakeContext context)
        : base(context)
    {
        MsBuildConfiguration = context.Argument("configuration", "Release");
    }
}

[TaskName("Clean")]
public sealed class CleanTask : FrostingTask<BuildContext>
{
    public override void Run(BuildContext context)
    {
        Console.WriteLine("Cleaning bin directories...");
        context.CleanDirectory($"../ModLoader.CLI/bin/{context.MsBuildConfiguration}");
        context.CleanDirectory($"../ModLoader.API/bin/{context.MsBuildConfiguration}");
        context.CleanDirectory($"../ModLoader.Core/bin/{context.MsBuildConfiguration}");
        context.CleanDirectory($"../ModLoader.DynamicCodeGeneration/bin/{context.MsBuildConfiguration}");
        context.CleanDirectory($"../ModLoader.PluginTest/bin/{context.MsBuildConfiguration}");
    }
}

[TaskName("Build")]
[IsDependentOn(typeof(CleanTask))]
public sealed class BuildTask : FrostingTask<BuildContext>
{
    public override void Run(BuildContext context)
    {
        context.DotNetBuild("../ModLoader.sln", new DotNetBuildSettings
        {
            Configuration = context.MsBuildConfiguration,
        });
    }
}

[TaskName("Package")]
[IsDependentOn(typeof(BuildTask))]
public sealed class PackageTask : FrostingTask<BuildContext>
{
    public override void Run(BuildContext context)
    {
        Console.WriteLine("Packaging files...");
        if (!Directory.Exists("../out"))
        {
            Directory.CreateDirectory("../out");
        }
        if (File.Exists("../out/ModLoader.CLI.zip"))
        {
            File.Delete("../out/ModLoader.CLI.zip");
        }
        List<string> DeleteMe = new List<string> { "ModLoader.CLI.deps.json" };
        foreach(var file in Directory.EnumerateFiles("../ModLoader.CLI/bin/Release/net7.0"))
        {
            if (Path.GetExtension(file) == ".xml" || Path.GetExtension(file) == ".pdb"){
                File.Delete(file);
            }else if (DeleteMe.Contains(Path.GetFileName(file)))
            {
                File.Delete(file);
            }
        }
        ZipFile.CreateFromDirectory("../ModLoader.CLI/bin/Release/net7.0", "../out/ModLoader.CLI.zip");
    }
}

[TaskName("Default")]
[IsDependentOn(typeof(PackageTask))]
public class DefaultTask : FrostingTask
{
}