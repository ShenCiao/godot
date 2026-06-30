using System;
using System.Globalization;
using System.IO;
using System.Text;
using Microsoft.Build.Construction;
using Microsoft.Build.Evaluation;
using GodotTools.Shared;

namespace GodotTools.ProjectEditor
{
    public static class ProjectGenerator
    {
        public const string GodotSdkAttrValue = "Godot.NET.Sdk";

        public static string GodotSdkVersion => GeneratedGodotNupkgsVersions.GodotNETSdk;

        public static string GodotMinimumRequiredTfm => "net8.0";

        public static ProjectRootElement GenGameProject(string name)
        {
            if (name.Length == 0)
                throw new ArgumentException("Project name is empty.", nameof(name));

            var root = ProjectRootElement.Create(NewProjectFileOptions.None);

            root.Sdk = GodotSdkAttrValue;

            var mainGroup = root.AddPropertyGroup();
            mainGroup.AddProperty("TargetFramework", GodotMinimumRequiredTfm);

            // Non-gradle builds require .NET 9 to match the jar libraries included in the export template.
            var net9 = mainGroup.AddProperty("TargetFramework", "net9.0");
            net9.Condition = " '$(GodotTargetPlatform)' == 'android' ";

            mainGroup.AddProperty("EnableDynamicLoading", "true");

            string sanitizedName = IdentifierUtils.SanitizeQualifiedIdentifier(name, allowEmptyIdentifiers: true);

            // If the name is not a valid namespace, manually set RootNamespace to a sanitized one.
            if (sanitizedName != name)
                mainGroup.AddProperty("RootNamespace", sanitizedName);

            return root;
        }

        public static void EnsureGlobalJsonExists(string dir)
        {
            string path = Path.Combine(dir, "global.json");
            if (File.Exists(path))
                return;

            SaveGlobalJson(path);
        }

        private static void SaveGlobalJson(string path)
        {
            string contents =
                "{\n" +
                "  \"msbuild-sdks\": {\n" +
                $"    \"Godot.NET.Sdk\": \"{GodotSdkVersion}\"\n" +
                "  }\n" +
                "}\n";

            File.WriteAllText(path, contents, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        }

        public static string GenAndSaveGameProject(string dir, string name)
        {
            if (name.Length == 0)
                throw new ArgumentException("Project name is empty.", nameof(name));

            string path = Path.Combine(dir, name + ".csproj");

            var root = GenGameProject(name);

            // Save (without BOM)
            root.Save(path, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
            EnsureGlobalJsonExists(dir);

            return Guid.NewGuid().ToString().ToUpperInvariant();
        }
    }
}
