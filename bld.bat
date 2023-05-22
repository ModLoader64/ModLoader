mkdir %SCRIPTS%
msbuild ModLoader.sln -nologo -t:Clean;Build /p:Configuration=Release /p:Platform=x64 /p:VisualStudioVersion=17.0
xcopy ModLoader.CLI\bin\x64\Release\net7.0 %SCRIPTS%