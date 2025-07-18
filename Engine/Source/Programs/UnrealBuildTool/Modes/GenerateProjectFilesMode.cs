// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	/// <summary>
	/// Generates project files for one or more projects
	/// </summary>
	[ToolMode("GenerateProjectFiles", ToolModeOptions.XmlConfig | ToolModeOptions.BuildPlatforms | ToolModeOptions.SingleInstance | ToolModeOptions.UseStartupTraceListener | ToolModeOptions.StartPrefetchingEngine | ToolModeOptions.ShowExecutionTime)]
	class GenerateProjectFilesMode : ToolMode
	{
		/// <summary>
		/// Types of project files to generate
		/// </summary>
		[CommandLine("-ProjectFileFormat")]
		[CommandLine("-2022", Value = nameof(ProjectFileFormat.VisualStudio2022))] // + override compiler
		[CommandLine("-Makefile", Value = nameof(ProjectFileFormat.Make))]
		[CommandLine("-CMakefile", Value = nameof(ProjectFileFormat.CMake))]
		[CommandLine("-QMakefile", Value = nameof(ProjectFileFormat.QMake))]
		[CommandLine("-KDevelopfile", Value = nameof(ProjectFileFormat.KDevelop))]
		[CommandLine("-CodeLiteFiles", Value = nameof(ProjectFileFormat.CodeLite))]
		[CommandLine("-XCodeProjectFiles", Value = nameof(ProjectFileFormat.XCode))]
		[CommandLine("-EddieProjectFiles", Value = nameof(ProjectFileFormat.Eddie))]
		[CommandLine("-VSCode", Value = nameof(ProjectFileFormat.VisualStudioCode))]
		[CommandLine("-VSMac", Value = nameof(ProjectFileFormat.VisualStudioMac))]
		[CommandLine("-VSWorkspace", Value = nameof(ProjectFileFormat.VisualStudioWorkspace))]
		[CommandLine("-CLion", Value = nameof(ProjectFileFormat.CLion))]
		[CommandLine("-Rider", Value = nameof(ProjectFileFormat.Rider))]
#if __VPROJECT_AVAILABLE__
		[CommandLine("-VProject", Value = nameof(ProjectFileFormat.VProject))]
#endif
		HashSet<ProjectFileFormat> ProjectFileFormats = new HashSet<ProjectFileFormat>();

		/// <summary>
		/// Disable native project file generators for platforms. Platforms with native project file generators typically require IDE extensions to be installed.
		/// </summary>
		[XmlConfigFile(Category = "ProjectFileGenerator")]
		string[]? DisablePlatformProjectGenerators = null;

		/// <summary>
		/// Whether this command is being run in an automated mode
		/// </summary>
		[CommandLine("-Automated")]
		bool bAutomated = false;

		/// <summary>
		/// Execute the tool mode
		/// </summary>
		/// <param name="Arguments">Command line arguments</param>
		/// <returns>Exit code</returns>
		/// <param name="Logger"></param>
		public override Task<int> ExecuteAsync(CommandLineArguments Arguments, ILogger Logger)
		{
			// Apply any command line arguments to this class
			Arguments.ApplyTo(this);

			// Apply the XML config to this class
			XmlConfig.ApplyTo(this);

			// Apply to architecture configs that need to read commandline arguments and didn't have the Arguments passed in during construction
			foreach (UnrealArchitectureConfig Config in UnrealArchitectureConfig.AllConfigs())
			{
				Arguments.ApplyTo(Config);
			}

			// set up logging (taken from BuildMode)
			if (!Log.HasFileWriter())
			{
				FileReference LogFile = FileReference.Combine(Unreal.EngineProgramSavedDirectory, "UnrealBuildTool", "Log_GPF.txt");
				Log.AddFileWriter("DefaultLogTraceListener", LogFile);
			}
			else
			{
				Log.RemoveStartupTraceListener();
			}

			// Parse rocket-specific arguments.
			FileReference? ProjectFile;
			Utils.TryParseProjectFileArgument(Arguments, Logger, out ProjectFile);

			// Apply the XML config again with a project specific BuildConfiguration.xml 
			XmlConfig.ReadConfigFiles(null, ProjectFile?.Directory, Logger);
			XmlConfig.ApplyTo(this);

			// Warn if there are explicit project file formats specified
			if (ProjectFileFormats.Count > 0 && !bAutomated)
			{
				StringBuilder Configuration = new StringBuilder();
				Configuration.Append("Project file formats specified via the command line will be ignored when generating\n");
				Configuration.Append("project files from the editor and other engine tools.\n");
				Configuration.Append('\n');
				Configuration.Append("Consider setting your desired IDE from the editor preferences window, or modify your\n");
				Configuration.Append("BuildConfiguration.xml file with:\n");
				Configuration.Append('\n');
				Configuration.Append("<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n");
				Configuration.Append("<Configuration xmlns=\"https://www.unrealengine.com/BuildConfiguration\">\n");
				Configuration.Append("  <ProjectFileGenerator>\n");
				foreach (ProjectFileFormat ProjectFileFormat in ProjectFileFormats)
				{
					Configuration.AppendFormat("    <Format>{0}</Format>\n", ProjectFileFormat);
				}
				Configuration.Append("  </ProjectFileGenerator>\n");
				Configuration.Append("</Configuration>\n");
				Logger.LogWarning("{Configuration}", Configuration.ToString());
			}

			// If there aren't any formats set, read the default project file format from the config file
			if (ProjectFileFormats.Count == 0)
			{
				// Read from the XML config
				if (!String.IsNullOrEmpty(ProjectFileGeneratorSettings.Format))
				{
					ProjectFileFormats.UnionWith(ProjectFileGeneratorSettings.ParseFormatList(ProjectFileGeneratorSettings.Format, Logger));
				}

				// Read from the editor config
				ProjectFileFormat PreferredSourceCodeAccessor;
				if (ProjectFileGenerator.GetPreferredSourceCodeAccessor(ProjectFile, out PreferredSourceCodeAccessor))
				{
					ProjectFileFormats.Add(PreferredSourceCodeAccessor);
				}

				// If there's still nothing set, get the default project file format for this platform
				if (ProjectFileFormats.Count == 0)
				{
					ProjectFileFormats.UnionWith(BuildHostPlatform.Current.GetDefaultProjectFileFormats());
				}
			}

			// Register all the platform project generators
			PlatformProjectGeneratorCollection PlatformProjectGenerators = new PlatformProjectGeneratorCollection();
			foreach (Type CheckType in Assembly.GetExecutingAssembly().GetTypes())
			{
				if (CheckType.IsClass && !CheckType.IsAbstract && CheckType.IsSubclassOf(typeof(PlatformProjectGenerator)))
				{
					PlatformProjectGenerator Generator = (PlatformProjectGenerator)Activator.CreateInstance(CheckType, Arguments, Logger)!;
					foreach (UnrealTargetPlatform Platform in Generator.GetPlatforms())
					{
						if (DisablePlatformProjectGenerators == null || !DisablePlatformProjectGenerators.Any(x => x.Equals(Platform.ToString(), StringComparison.OrdinalIgnoreCase)))
						{
							Logger.LogDebug("Registering project generator {CheckType} for {Platform}", CheckType, Platform);
							PlatformProjectGenerators.RegisterPlatformProjectGenerator(Platform, Generator, Logger);
						}
					}
				}
			}

			// print out any errors to the log
			List<string> BadPlatformNames = new List<string>();
			Logger.LogDebug("\n---   SDK INFO START   ---");
			foreach (string PlatformName in UnrealTargetPlatform.GetValidPlatformNames())
			{
				UEBuildPlatformSDK? SDK = UEBuildPlatformSDK.GetSDKForPlatform(PlatformName);
				if (SDK != null && SDK.bIsSdkAllowedOnHost)
				{
					// print out the info to the log, and if it's invalid, remember it
					SDKStatus Validity = SDK.PrintSDKInfoAndReturnValidity(LogEventType.Verbose, LogFormatOptions.NoConsoleOutput, LogEventType.Warning, LogFormatOptions.NoConsoleOutput);
					if (Validity == SDKStatus.Invalid)
					{
						BadPlatformNames.Add(PlatformName);
					}
				}
			}
			if (BadPlatformNames.Count > 0)
			{
				Log.TraceInformationOnce("\nSome Platforms were skipped due to invalid SDK setup: {0}.\nSee the log file for detailed information\n\n", String.Join(", ", BadPlatformNames));
				Logger.LogInformation("");
			}
			Logger.LogDebug("---   SDK INFO END   ---");
			Logger.LogDebug("");

			// look for a single target name param
			string? SingleTargetName = null;
			if (Arguments.HasValue("-SingleTarget="))
			{
				SingleTargetName = Arguments.GetString("-SingleTarget=");
			}

			// Create each project generator and run it
			Dictionary<ProjectFileFormat, ProjectFileGenerator> Generators = new();
			foreach (ProjectFileFormat ProjectFileFormat in ProjectFileFormats.Distinct())
			{
				ProjectFileGenerator Generator;
				switch (ProjectFileFormat)
				{
					case ProjectFileFormat.Make:
						Generator = new MakefileGenerator(ProjectFile);
						break;
					case ProjectFileFormat.CMake:
						Generator = new CMakefileGenerator(ProjectFile);
						break;
					case ProjectFileFormat.QMake:
						Generator = new QMakefileGenerator(ProjectFile);
						break;
					case ProjectFileFormat.KDevelop:
						Generator = new KDevelopGenerator(ProjectFile);
						break;
					case ProjectFileFormat.CodeLite:
						Generator = new CodeLiteGenerator(ProjectFile, Arguments);
						break;
					case ProjectFileFormat.VisualStudio:
						Generator = new VCProjectFileGenerator(ProjectFile, VCProjectFileFormat.Default, Arguments);
						break;
					case ProjectFileFormat.VisualStudio2022:
						Generator = new VCProjectFileGenerator(ProjectFile, VCProjectFileFormat.VisualStudio2022, Arguments);
						break;
					case ProjectFileFormat.XCode:
						Generator = new XcodeProjectFileGenerator(ProjectFile, Arguments);
						break;
					case ProjectFileFormat.Eddie:
						Generator = new EddieProjectFileGenerator(ProjectFile);
						break;
					case ProjectFileFormat.VisualStudioCode:
						Generator = new VSCodeProjectFileGenerator(ProjectFile);
						break;
					case ProjectFileFormat.VisualStudioWorkspace:
						Generator = new VSWorkspaceProjectFileGenerator(ProjectFile, Arguments);
						break;
					case ProjectFileFormat.CLion:
						Generator = new CLionGenerator(ProjectFile);
						break;
					case ProjectFileFormat.VisualStudioMac:
						Generator = new VCMacProjectFileGenerator(ProjectFile, Arguments);
						break;
					case ProjectFileFormat.Rider:
						Generator = new RiderProjectFileGenerator(ProjectFile, Arguments);
						break;
#if __VPROJECT_AVAILABLE__
					case ProjectFileFormat.VProject:
						Generator = new VProjectFileGenerator(ProjectFile);
						break;
#endif
					default:
						throw new BuildException("Unhandled project file type '{0}", ProjectFileFormat);
				}
				// remember if we only wanted a single target (similar to -game -project, except usable with progarms without uprojects)
				Generator.SingleTargetName = SingleTargetName;

				Generators[ProjectFileFormat] = Generator;
			}

			// Check there are no superfluous command line arguments
			// TODO (still pass raw arguments below)
			// Arguments.CheckAllArgumentsUsed();

			// Now generate project files
			ProjectFileGenerator.bGenerateProjectFiles = true;
			bool bGenerateSuccess = true;
			foreach (KeyValuePair<ProjectFileFormat, ProjectFileGenerator> Pair in Generators)
			{
				using ITimelineEvent ExecuteScope = Timeline.ScopeEvent($"Generating {Pair.Key} project files");
				Logger.LogInformation("");
				Logger.LogInformation($"Generating {Pair.Key} project files:");

				ProjectFileGenerator.Current = Pair.Value;
				Arguments.ApplyTo(Pair.Value);
#pragma warning disable 0618 // Type or member is obsolete
				bGenerateSuccess = Pair.Value.GenerateProjectFiles(PlatformProjectGenerators, Arguments.GetRawArray(), false, Logger);
#pragma warning restore 0618 // Type or member is obsolete
				ProjectFileGenerator.Current = null;

				if (!bGenerateSuccess)
				{
					break;
				}
			}
			if (bGenerateSuccess && Generators.Count > 0)
			{
				Generators.First().Value.GenerateQueryTargetsDataForEditor(Arguments.GetRawArray(), Logger);
			}
			return bGenerateSuccess ? Task.FromResult((int)CompilationResult.Succeeded) : Task.FromResult((int)CompilationResult.OtherCompilationError);
		}
	}
}
