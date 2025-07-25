// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;
using Microsoft.Win32;

namespace UnrealGameSync
{
	public sealed class UserErrorException : Exception
	{
		public int Code { get; }

		public UserErrorException(string message, int code = 1) : base(message)
		{
			Code = code;
		}
	}

	public class PerforceChangeDetails
	{
		public int Number { get; }
		public string Description { get; }
		public bool ContainsCode { get; }
		public bool ContainsContent { get; }
		public bool ContainsUgsConfig { get; }

		public PerforceChangeDetails(DescribeRecord describeRecord, Func<string, bool>? isCodeFile = null)
		{
			isCodeFile ??= IsCodeFile;

			Number = describeRecord.Number;
			Description = describeRecord.Description;

			// Check whether the files are code or content
			foreach (DescribeFileRecord file in describeRecord.Files)
			{
				if (isCodeFile(file.DepotFile))
				{
					ContainsCode = true;
				}
				else
				{
					ContainsContent = true;
				}

				if (file.DepotFile.EndsWith("/UnrealGameSync.ini", StringComparison.OrdinalIgnoreCase))
				{
					ContainsUgsConfig = true;
				}
			}
		}

		public static bool IsCodeFile(string depotFile)
		{
			return PerforceUtils.CodeExtensions.Any(extension => depotFile.EndsWith(extension, StringComparison.OrdinalIgnoreCase));
		}
	}

	public static class Utility
	{
		static readonly MemoryCache s_changeCache = new MemoryCache(new MemoryCacheOptions { SizeLimit = 4096 });

		class CachedChangeRecord
		{
			public ChangesRecord ChangesRecord { get; }
			public int? PrevNumber { get; set; }

			public CachedChangeRecord(ChangesRecord changesRecord) => ChangesRecord = changesRecord;
		}

		static string GetChangeCacheKey(int number, Sha1Hash config) => $"change: {number} ({config})";

		static string GetChangeDetailsCacheKey(int number, Sha1Hash config) => $"details: {number} ({config})";

		static Sha1Hash GetConfigHash(IPerforceConnection perforce, IEnumerable<string> syncPaths, IEnumerable<string> codeRules)
		{
			StringBuilder digest = new StringBuilder();
			digest.AppendLine($"Server: {perforce.Settings.ServerAndPort}");
			digest.AppendLine($"User: {perforce.Settings.UserName}");
			digest.AppendLine($"Client: {perforce.Settings.ClientName}");
			foreach (string syncPath in syncPaths)
			{
				digest.AppendLine($"Sync: {syncPath}");
			}
			foreach (string codeRule in codeRules)
			{
				digest.AppendLine($"Rule: {codeRule}");
			}
			return Sha1Hash.Compute(Encoding.UTF8.GetBytes(digest.ToString()));
		}

		/// <summary>
		/// Gets the code filter from the project config file
		/// </summary>
		public static string[] GetCodeFilter(ConfigFile projectConfigFile)
		{
			ConfigSection? projectConfigSection = projectConfigFile.FindSection("Perforce");
			return projectConfigSection?.GetValues("CodeFilter", (string[]?)null) ?? Array.Empty<string>();
		}

		/// <summary>
		/// Creates or returns cached <see cref="PerforceChangeDetails"/> objects describing the requested chagnes
		/// </summary>
		public static async IAsyncEnumerable<ChangesRecord> EnumerateChanges(IPerforceConnection perforce, IEnumerable<string> syncPaths, int? minChangeNumber, int? maxChangeNumber, int? maxChanges, [EnumeratorCancellation] CancellationToken cancellationToken)
		{
			CachedChangeRecord? prevCachedChangeRecord = null;

			Sha1Hash configHash = GetConfigHash(perforce, syncPaths, Array.Empty<string>());
			while (maxChanges == null || maxChanges.Value > 0)
			{
				// If we have a maximum changelist number, see if the previous change is in the cache
				if (maxChangeNumber != null)
				{
					if (minChangeNumber != null && maxChangeNumber.Value < minChangeNumber.Value)
					{
						yield break;
					}

					string cacheKey = GetChangeCacheKey(maxChangeNumber.Value, configHash);
					if (s_changeCache.TryGetValue(cacheKey, out CachedChangeRecord? change) && change != null)
					{
						yield return change.ChangesRecord;

						if (maxChanges != null)
						{
							maxChanges = maxChanges.Value - 1;
						}

						if (change.PrevNumber != null)
						{
							maxChangeNumber = change.PrevNumber.Value;
						}
						else
						{
							maxChangeNumber = change.ChangesRecord.Number - 1;
						}

						prevCachedChangeRecord = change;
						continue;
					}
				}

				// Get the search range
				string range;
				if (minChangeNumber == null)
				{
					if (maxChangeNumber == null)
					{
						range = "";
					}
					else
					{
						range = $"@<={maxChangeNumber.Value}";
					}
				}
				else
				{
					if (maxChangeNumber == null)
					{
						range = $"@{minChangeNumber.Value},#head";
					}
					else
					{
						range = $"@{minChangeNumber.Value},{maxChangeNumber.Value}";
					}
				}

				// Get the next batch of change numbers
				int maxChangesForBatch = maxChanges ?? 20;
				List<string> syncPathsWithChange = syncPaths.Select(x => $"{x}{range}").ToList();

				List<ChangesRecord> changes;
				if (minChangeNumber == null)
				{
					changes = await perforce.GetChangesAsync(ChangesOptions.IncludeTimes | ChangesOptions.LongOutput, maxChangesForBatch, ChangeStatus.Submitted, syncPathsWithChange, cancellationToken);
				}
				else
				{
					changes = await perforce.GetChangesAsync(ChangesOptions.IncludeTimes | ChangesOptions.LongOutput, clientName: null, minChangeNumber: minChangeNumber.Value, maxChangesForBatch, ChangeStatus.Submitted, userName: null, fileSpecs: syncPathsWithChange, cancellationToken: cancellationToken);
				}

				// Sort the changes in case we get interleaved output from multiple sync paths
				changes = changes.OrderByDescending(x => x.Number).Take(maxChangesForBatch).ToList();

				// Add all the previous change numbers to the cache
				foreach (ChangesRecord change in changes)
				{
					if (prevCachedChangeRecord != null)
					{
						prevCachedChangeRecord.PrevNumber = change.Number;
					}

					prevCachedChangeRecord = new CachedChangeRecord(change);

					string cacheKey = GetChangeCacheKey(change.Number, configHash);
					using (ICacheEntry entry = s_changeCache.CreateEntry(cacheKey))
					{
						entry.SetSize(1);
						entry.Value = prevCachedChangeRecord;
					}

					maxChangeNumber = change.Number - 1;
				}

				// Return the results
				foreach (ChangesRecord change in changes)
				{
					yield return change;
				}

				// If we have run out of changes, quit now
				if (changes.Count < maxChangesForBatch)
				{
					break;
				}
				if (maxChanges != null)
				{
					maxChanges = maxChanges.Value - changes.Count;
				}
			}
		}

		/// <summary>
		/// Creates or returns cached <see cref="PerforceChangeDetails"/> objects describing the requested chagnes
		/// </summary>
		public static IAsyncEnumerable<PerforceChangeDetails> EnumerateChangeDetails(IPerforceConnection perforce, int? minChangeNumber, int? maxChangeNumber, IEnumerable<string> syncPaths, IEnumerable<string> codeRules, CancellationToken cancellationToken)
		{
			IAsyncEnumerable<int> changeNumbers = EnumerateChanges(perforce, syncPaths, minChangeNumber, maxChangeNumber, null, cancellationToken).Select(x => x.Number);
			return EnumerateChangeDetails(perforce, changeNumbers, codeRules, cancellationToken);
		}

		/// <summary>
		/// Creates or returns cached <see cref="PerforceChangeDetails"/> objects describing the requested chagnes
		/// </summary>
		public static async IAsyncEnumerable<PerforceChangeDetails> EnumerateChangeDetails(IPerforceConnection perforce, IAsyncEnumerable<int> changeNumbers, IEnumerable<string> codeRules, [EnumeratorCancellation] CancellationToken cancellationToken)
		{
			// Get a delegate that determines if a file is a code change or not
			Func<string, bool>? isCodeFile = null;
			if (codeRules.Any())
			{
				FileFilter filter = new FileFilter(PerforceUtils.CodeExtensions.Select(x => $"*{x}"));
				foreach (string codeRule in codeRules)
				{
					filter.AddRule(codeRule);
				}
				isCodeFile = filter.Matches;
			}

			// Get the hash of the configuration
			Sha1Hash hash = GetConfigHash(perforce, Enumerable.Empty<string>(), codeRules);

			// Update them in batches
			await using IAsyncEnumerator<int> changeNumberEnumerator = changeNumbers.GetAsyncEnumerator(cancellationToken);
			for (; ; )
			{
				// Get the next batch of changes to query, up to the next change that we already have a cached value for
				const int BatchSize = 10;
				PerforceChangeDetails? cachedDetails = null;

				List<int> changeBatch = new List<int>(BatchSize);
				while (changeBatch.Count < BatchSize && await changeNumberEnumerator.MoveNextAsync(cancellationToken))
				{
					string cacheKey = GetChangeDetailsCacheKey(changeNumberEnumerator.Current, hash);
					if (s_changeCache.TryGetValue(cacheKey, out cachedDetails))
					{
						break;
					}
					changeBatch.Add(changeNumberEnumerator.Current);
				}

				// Describe the requested changes
				if (changeBatch.Count > 0)
				{
					const int InitialMaxFiles = 100;

					List<DescribeRecord> describeRecords = await perforce.DescribeAsync(DescribeOptions.None, InitialMaxFiles, changeBatch.ToArray(), cancellationToken);
					foreach (DescribeRecord describeRecordLoop in describeRecords.OrderByDescending(x => x.Number))
					{
						DescribeRecord describeRecord = describeRecordLoop;
						int queryChangeNumber = describeRecord.Number;

						PerforceChangeDetails details = new PerforceChangeDetails(describeRecord, isCodeFile);

						// Content only changes must be flagged accurately, because code changes invalidate precompiled binaries. Increase the number of files fetched until we can classify it correctly.
						int currentMaxFiles = InitialMaxFiles;
						while (describeRecord.Files.Count >= currentMaxFiles && !details.ContainsCode)
						{
							cancellationToken.ThrowIfCancellationRequested();
							currentMaxFiles *= 10;

							List<DescribeRecord> newDescribeRecords = await perforce.DescribeAsync(DescribeOptions.None, currentMaxFiles, new int[] { queryChangeNumber }, cancellationToken);
							if (newDescribeRecords.Count == 0)
							{
								break;
							}

							describeRecord = newDescribeRecords[0];
							details = new PerforceChangeDetails(describeRecord, isCodeFile);
						}

						// Add it to the cache
						string cacheKey = GetChangeDetailsCacheKey(describeRecord.Number, hash);
						using (ICacheEntry entry = s_changeCache.CreateEntry(cacheKey))
						{
							entry.SetSize(10);
							entry.Value = details;
						}

						// Return the value
						yield return details;
					}
				}

				// Return the cached value, if there was one
				if (cachedDetails != null)
				{
					yield return cachedDetails;
				}
				else if (changeBatch.Count == 0)
				{
					yield break;
				}
			}
		}

		public static event Action<Exception>? TraceException;

		static JsonSerializerOptions GetDefaultJsonSerializerOptions()
		{
			JsonSerializerOptions options = new JsonSerializerOptions();
			options.AllowTrailingCommas = true;
			options.ReadCommentHandling = JsonCommentHandling.Skip;
			options.PropertyNameCaseInsensitive = true;
			options.Converters.Add(new JsonStringEnumConverter());
			return options;
		}

		public static JsonSerializerOptions DefaultJsonSerializerOptions { get; } = GetDefaultJsonSerializerOptions();

		public static bool TryLoadJson<T>(FileReference file, [NotNullWhen(true)] out T? obj) where T : class
		{
			if (!FileReference.Exists(file))
			{
				obj = null;
				return false;
			}

			try
			{
				obj = LoadJson<T>(file);
				return true;
			}
			catch (Exception ex)
			{
				TraceException?.Invoke(ex);
				obj = null;
				return false;
			}
		}

		public static T? TryDeserializeJson<T>(byte[] data) where T : class
		{
			try
			{
				return JsonSerializer.Deserialize<T>(data, DefaultJsonSerializerOptions)!;
			}
			catch (Exception ex)
			{
				TraceException?.Invoke(ex);
				return null;
			}
		}

		public static T LoadJson<T>(FileReference file)
		{
			byte[] data = FileReference.ReadAllBytes(file);
			return JsonSerializer.Deserialize<T>(data, DefaultJsonSerializerOptions)!;
		}

		public static byte[] SerializeJson<T>(T obj)
		{
			JsonSerializerOptions options = new JsonSerializerOptions { DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull, WriteIndented = true };
			options.Converters.Add(new JsonStringEnumConverter());

			byte[] buffer;
			using (MemoryStream stream = new MemoryStream())
			{
				using (Utf8JsonWriter writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
				{
					JsonSerializer.Serialize(writer, obj, options);
				}
				buffer = stream.ToArray();
			}

			return buffer;
		}

		public static void SaveJson<T>(FileReference file, T obj)
		{
			byte[] buffer = SerializeJson(obj);
			FileReference.WriteAllBytes(file, buffer);
		}

		public static string GetPathWithCorrectCase(FileInfo info)
		{
			DirectoryInfo parentInfo = info.Directory!;
			if (info.Exists)
			{
				return Path.Combine(GetPathWithCorrectCase(parentInfo), parentInfo.GetFiles(info.Name)[0].Name);
			}
			else
			{
				return Path.Combine(GetPathWithCorrectCase(parentInfo), info.Name);
			}
		}

		public static string GetPathWithCorrectCase(DirectoryInfo info)
		{
			DirectoryInfo? parentInfo = info.Parent;
			if (parentInfo == null)
			{
				return info.FullName.ToUpperInvariant();
			}
			else if (info.Exists)
			{
				return Path.Combine(GetPathWithCorrectCase(parentInfo), parentInfo.GetDirectories(info.Name)[0].Name);
			}
			else
			{
				return Path.Combine(GetPathWithCorrectCase(parentInfo), info.Name);
			}
		}

		public static void ForceDeleteFile(string fileName)
		{
			if (File.Exists(fileName))
			{
				File.SetAttributes(fileName, File.GetAttributes(fileName) & ~FileAttributes.ReadOnly);
				File.Delete(fileName);
			}
		}

		public static bool SpawnProcess(string fileName, string commandLine)
		{
			using (Process childProcess = new Process())
			{
				childProcess.StartInfo.FileName = fileName;
				childProcess.StartInfo.Arguments = String.IsNullOrEmpty(commandLine) ? "" : commandLine;
				childProcess.StartInfo.UseShellExecute = false;
				return childProcess.Start();
			}
		}

		public static bool SpawnHiddenProcess(string fileName, string commandLine)
		{
			using (Process childProcess = new Process())
			{
				childProcess.StartInfo.FileName = fileName;
				childProcess.StartInfo.Arguments = String.IsNullOrEmpty(commandLine) ? "" : commandLine;
				childProcess.StartInfo.UseShellExecute = false;
				childProcess.StartInfo.RedirectStandardOutput = true;
				childProcess.StartInfo.RedirectStandardError = true;
				childProcess.StartInfo.CreateNoWindow = true;
				try
				{
					return childProcess.Start();
				}
				catch
				{
					return false;
				}
			}
		}

		public static async Task<int> ExecuteProcessAsync(string fileName, string? workingDir, string commandLine, Action<string> outputLine, IReadOnlyDictionary<string, string>? env = null, CancellationToken cancellationToken = default)
		{
			using (ManagedProcessGroup newProcessGroup = new ManagedProcessGroup())
			using (ManagedProcess newProcess = new ManagedProcess(newProcessGroup, fileName, commandLine, workingDir, env, null, ProcessPriorityClass.Normal))
			{
				for (; ; )
				{
					string? line = await newProcess.ReadLineAsync(cancellationToken);
					if (line == null)
					{
						await newProcess.WaitForExitAsync(cancellationToken);
						return newProcess.ExitCode;
					}
					outputLine(line);
				}
			}
		}

		public static async Task<int> ExecuteProcessAsync(string fileName, string? workingDir, string commandLine, Action<string> outputLine, CancellationToken cancellationToken)
		{
			using (ManagedProcessGroup newProcessGroup = new ManagedProcessGroup())
			using (ManagedProcess newProcess = new ManagedProcess(newProcessGroup, fileName, commandLine, workingDir, null, null, ProcessPriorityClass.Normal))
			{
				for (; ; )
				{
					string? line = await newProcess.ReadLineAsync(cancellationToken);
					if (line == null)
					{
						await newProcess.WaitForExitAsync(cancellationToken);
						return newProcess.ExitCode;
					}
					outputLine(line);
				}
			}
		}

		public static bool SafeIsFileUnderDirectory(string fileName, string directoryName)
		{
			try
			{
				string fullDirectoryName = Path.GetFullPath(directoryName).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
				string fullFileName = Path.GetFullPath(fileName);
				return fullFileName.StartsWith(fullDirectoryName, StringComparison.InvariantCultureIgnoreCase);
			}
			catch (Exception)
			{
				return false;
			}
		}

		/// <summary>
		/// Expands variables in $(VarName) format in the given string. Variables are retrieved from the given dictionary, or through the environment of the current process.
		/// Any unknown variables are ignored.
		/// </summary>
		/// <param name="inputString">String to search for variable names</param>
		/// <param name="additionalVariables">Lookup of variable names to values</param>
		/// <returns>String with all variables replaced</returns>
		public static string ExpandVariables(string inputString, Dictionary<string, string>? additionalVariables = null)
		{
			string result = inputString;
			for (int idx = result.IndexOf("$(", StringComparison.Ordinal); idx != -1; idx = result.IndexOf("$(", idx, StringComparison.Ordinal))
			{
				// Find the end of the variable name
				int endIdx = result.IndexOf(")", idx + 2, StringComparison.Ordinal);
				if (endIdx == -1)
				{
					break;
				}

				// Extract the variable name from the string
				string name = result.Substring(idx + 2, endIdx - (idx + 2));

				// Strip the format from the name
				string? format = null;
				int formatIdx = name.IndexOf(':', StringComparison.Ordinal);
				if (formatIdx != -1)
				{
					format = name.Substring(formatIdx + 1);
					name = name.Substring(0, formatIdx);
				}

				// Find the value for it, either from the dictionary or the environment block
				string? value;
				if (additionalVariables == null || !additionalVariables.TryGetValue(name, out value))
				{
					value = Environment.GetEnvironmentVariable(name);
					if (value == null)
					{
						idx = endIdx + 1;
						continue;
					}
				}

				// Encode the variable if necessary
				if (format != null)
				{
					if (String.Equals(format, "URI", StringComparison.OrdinalIgnoreCase))
					{
						value = Uri.EscapeDataString(value);
					}
				}

				// Replace the variable, or skip past it
				result = result.Substring(0, idx) + value + result.Substring(endIdx + 1);
			}
			return result;
		}

		class ProjectJson
		{
			public bool Enterprise { get; set; }
		}

		/// <summary>
		/// Determines if a project is an enterprise project
		/// </summary>
		public static bool IsEnterpriseProjectFromText(string text)
		{
			try
			{
				JsonSerializerOptions options = new JsonSerializerOptions();
				options.PropertyNameCaseInsensitive = true;
				options.Converters.Add(new JsonStringEnumConverter());

				ProjectJson project = JsonSerializer.Deserialize<ProjectJson>(text, options)!;

				return project.Enterprise;
			}
			catch
			{
				return false;
			}
		}

		/******/

		private static void AddLocalConfigPaths_WithSubFolders(DirectoryInfo baseDir, string fileName, List<FileInfo> files)
		{
			if (baseDir.Exists)
			{
				FileInfo baseFileInfo = new FileInfo(Path.Combine(baseDir.FullName, fileName));
				if (baseFileInfo.Exists)
				{
					files.Add(baseFileInfo);
				}

				foreach (DirectoryInfo subDirInfo in baseDir.EnumerateDirectories())
				{
					FileInfo subFile = new FileInfo(Path.Combine(subDirInfo.FullName, fileName));
					if (subFile.Exists)
					{
						files.Add(subFile);
					}
				}
			}
		}

		private static void AddLocalConfigPaths_WithExtensionDirs(DirectoryInfo? baseDir, string relativePath, string fileName, List<FileInfo> files)
		{
			if (baseDir != null && baseDir.Exists)
			{
				AddLocalConfigPaths_WithSubFolders(new DirectoryInfo(Path.Combine(baseDir.FullName, relativePath)), fileName, files);

				DirectoryInfo platformExtensionsDir = new DirectoryInfo(Path.Combine(baseDir.FullName, "Platforms"));
				if (platformExtensionsDir.Exists)
				{
					foreach (DirectoryInfo platformExtensionDir in platformExtensionsDir.EnumerateDirectories())
					{
						AddLocalConfigPaths_WithSubFolders(new DirectoryInfo(Path.Combine(platformExtensionDir.FullName, relativePath)), fileName, files);
					}
				}

				DirectoryInfo restrictedBaseDir = new DirectoryInfo(Path.Combine(baseDir.FullName, "Restricted"));
				if (restrictedBaseDir.Exists)
				{
					foreach (DirectoryInfo restrictedDir in restrictedBaseDir.EnumerateDirectories())
					{
						AddLocalConfigPaths_WithSubFolders(new DirectoryInfo(Path.Combine(restrictedDir.FullName, relativePath)), fileName, files);
					}
				}
			}
		}

		public static List<FileInfo> GetLocalConfigPaths(DirectoryInfo engineDir, FileInfo projectFile)
		{
			List<FileInfo> searchPaths = new List<FileInfo>();
			AddLocalConfigPaths_WithExtensionDirs(engineDir, "Programs/UnrealGameSync", "UnrealGameSync.ini", searchPaths);

			if (projectFile.Name.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
			{
				AddLocalConfigPaths_WithExtensionDirs(projectFile.Directory!, "Build", "UnrealGameSync.ini", searchPaths);
			}
			else if (projectFile.Name.EndsWith(".uefnproject", StringComparison.OrdinalIgnoreCase))
			{
				AddLocalConfigPaths_WithExtensionDirs(projectFile.Directory?.Parent, "Build", "UnrealGameSync.ini", searchPaths);
			}
			else
			{
				AddLocalConfigPaths_WithExtensionDirs(engineDir, "Programs/UnrealGameSync", "DefaultEngine.ini", searchPaths);
			}
			return searchPaths;
		}

		/******/

		private static void AddDepotConfigPaths_PlatformFolders(string basePath, string fileName, List<string> searchPaths)
		{
			searchPaths.Add(String.Format("{0}/{1}", basePath, fileName));
			searchPaths.Add(String.Format("{0}/*/{1}", basePath, fileName));
		}

		private static void AddDepotConfigPaths_PlatformExtensions(string basePath, string relativePath, string fileName, List<string> searchPaths)
		{
			AddDepotConfigPaths_PlatformFolders(basePath + relativePath, fileName, searchPaths);
			AddDepotConfigPaths_PlatformFolders(basePath + "/Platforms/*" + relativePath, fileName, searchPaths);
			AddDepotConfigPaths_PlatformFolders(basePath + "/Restricted/*" + relativePath, fileName, searchPaths);
		}

		public static List<string> GetDepotConfigPaths(string enginePath, string projectPath)
		{
			List<string> searchPaths = new List<string>();
			AddDepotConfigPaths_PlatformExtensions(enginePath, "/Programs/UnrealGameSync", "UnrealGameSync.ini", searchPaths);

			if (projectPath.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
			{
				AddDepotConfigPaths_PlatformExtensions(projectPath.Substring(0, projectPath.LastIndexOf('/')), "/Build", "UnrealGameSync.ini", searchPaths);
			}
			else if (projectPath.EndsWith(".uefnproject", StringComparison.OrdinalIgnoreCase))
			{
				string parentFolder = projectPath.Substring(0, projectPath.LastIndexOf('/'));
				parentFolder = parentFolder.Substring(0, parentFolder.LastIndexOf('/'));

				AddDepotConfigPaths_PlatformExtensions(parentFolder, "/Build", "UnrealGameSync.ini", searchPaths);
			}
			else
			{
				AddDepotConfigPaths_PlatformExtensions(enginePath, "/Programs/UnrealGameSync", "DefaultEngine.ini", searchPaths);
			}
			return searchPaths;
		}

		/******/

		public static async Task<string[]?> TryPrintFileUsingCacheAsync(IPerforceConnection perforce, string depotPath, DirectoryReference cacheFolder, string? digest, ILogger logger, CancellationToken cancellationToken)
		{
			if (digest == null)
			{
				PerforceResponse<PrintRecord<string[]>> printLinesResponse = await perforce.TryPrintLinesAsync(depotPath, cancellationToken);
				if (printLinesResponse.Succeeded)
				{
					return printLinesResponse.Data.Contents;
				}
				else
				{
					return null;
				}
			}

			FileReference cacheFile = FileReference.Combine(cacheFolder, digest);
			if (FileReference.Exists(cacheFile))
			{
				logger.LogDebug("Reading cached copy of {DepotFile} from {LocalFile}", depotPath, cacheFile);
				try
				{
					string[] lines = await FileReference.ReadAllLinesAsync(cacheFile, cancellationToken);
					try
					{
						FileReference.SetLastWriteTimeUtc(cacheFile, DateTime.UtcNow);
					}
					catch (Exception ex)
					{
						logger.LogWarning(ex, "Exception touching cache file {LocalFile}", cacheFile);
					}
					return lines;
				}
				catch (Exception ex)
				{
					logger.LogWarning(ex, "Error while reading cache file {LocalFile}: {Message}", cacheFile, ex.Message);
				}
			}

			DirectoryReference.CreateDirectory(cacheFolder);

			FileReference tempFile = new FileReference(String.Format("{0}.{1}.temp", cacheFile.FullName, Guid.NewGuid()));
			PerforceResponseList<PrintRecord> printResponse = await perforce.TryPrintAsync(tempFile.FullName, depotPath, cancellationToken);
			if (!printResponse.Succeeded)
			{
				return null;
			}
			else
			{
				string[] lines = await FileReference.ReadAllLinesAsync(tempFile, cancellationToken);
				try
				{
					FileReference.SetAttributes(tempFile, FileAttributes.Normal);
					FileReference.SetLastWriteTimeUtc(tempFile, DateTime.UtcNow);
					FileReference.Move(tempFile, cacheFile);
				}
				catch
				{
					try
					{
						FileReference.Delete(tempFile);
					}
					catch
					{
					}
				}
				return lines;
			}
		}

		public static void ClearPrintCache(DirectoryReference cacheFolder)
		{
			DirectoryInfo cacheDir = cacheFolder.ToDirectoryInfo();
			if (cacheDir.Exists)
			{
				DateTime deleteTime = DateTime.UtcNow - TimeSpan.FromDays(5.0);
				foreach (FileInfo cacheFile in cacheDir.EnumerateFiles())
				{
					if (cacheFile.LastWriteTimeUtc < deleteTime || cacheFile.Name.EndsWith(".temp", StringComparison.OrdinalIgnoreCase))
					{
						try
						{
							cacheFile.Attributes = FileAttributes.Normal;
							cacheFile.Delete();
						}
						catch
						{
						}
					}
				}
			}
		}

		public static Color Blend(Color first, Color second, float t)
		{
			return Color.FromArgb((int)(first.R + (second.R - first.R) * t), (int)(first.G + (second.G - first.G) * t), (int)(first.B + (second.B - first.B) * t));
		}

		public static PerforceSettings OverridePerforceSettings(IPerforceSettings defaultConnection, string? serverAndPort, string? userName)
		{
			PerforceSettings newSettings = new PerforceSettings(defaultConnection);
			if (!String.IsNullOrWhiteSpace(serverAndPort))
			{
				newSettings.ServerAndPort = serverAndPort;
			}
			if (!String.IsNullOrWhiteSpace(userName))
			{
				newSettings.UserName = userName;
			}
			return newSettings;
		}

		public static string FormatRecentDateTime(DateTime date)
		{
			DateTime now = DateTime.Now;

			DateTime midnight = new DateTime(now.Year, now.Month, now.Day);
			DateTime midnightTonight = midnight + TimeSpan.FromDays(1.0);

			if (date > midnightTonight)
			{
				return String.Format("{0} at {1}", date.ToLongDateString(), date.ToShortTimeString());
			}
			else if (date >= midnight)
			{
				return String.Format("today at {0}", date.ToShortTimeString());
			}
			else if (date >= midnight - TimeSpan.FromDays(1.0))
			{
				return String.Format("yesterday at {0}", date.ToShortTimeString());
			}
			else if (date >= midnight - TimeSpan.FromDays(5.0))
			{
				return String.Format("{0:dddd} at {1}", date, date.ToShortTimeString());
			}
			else
			{
				return String.Format("{0} at {1}", date.ToLongDateString(), date.ToShortTimeString());
			}
		}

		public static string FormatDurationMinutes(TimeSpan duration)
		{
			return FormatDurationMinutes((int)(duration.TotalMinutes + 1));
		}

		public static string FormatDurationMinutes(int totalMinutes)
		{
			if (totalMinutes > 24 * 60)
			{
				return String.Format("{0}d {1}h", totalMinutes / (24 * 60), (totalMinutes / 60) % 24);
			}
			else if (totalMinutes > 60)
			{
				return String.Format("{0}h {1}m", totalMinutes / 60, totalMinutes % 60);
			}
			else
			{
				return String.Format("{0}m", totalMinutes);
			}
		}

		public static string FormatUserName(string userName)
		{
			StringBuilder normalUserName = new StringBuilder();
			for (int idx = 0; idx < userName.Length; idx++)
			{
				if (idx == 0 || userName[idx - 1] == '.')
				{
					normalUserName.Append(Char.ToUpper(userName[idx]));
				}
				else if (userName[idx] == '.')
				{
					normalUserName.Append(' ');
				}
				else
				{
					normalUserName.Append(Char.ToLower(userName[idx]));
				}
			}
			return normalUserName.ToString();
		}

		public static void OpenUrl(string url)
		{
			ProcessStartInfo startInfo = new ProcessStartInfo();
			startInfo.FileName = url;
			startInfo.UseShellExecute = true;
			using Process? _ = Process.Start(startInfo);
		}

		[SupportedOSPlatform("windows")]
		public static void DeleteRegistryKey(RegistryKey rootKey, string keyName, string valueName)
		{
			using (RegistryKey? key = rootKey.OpenSubKey(keyName, true))
			{
				if (key != null)
				{
					DeleteRegistryKey(key, valueName);
				}
			}
		}

		[SupportedOSPlatform("windows")]
		public static void DeleteRegistryKey(RegistryKey key, string name)
		{
			string[] valueNames = key.GetValueNames();
			if (valueNames.Any(x => String.Equals(x, name, StringComparison.OrdinalIgnoreCase)))
			{
				try
				{
					key.DeleteValue(name);
				}
				catch
				{
				}
			}
		}
	}
}
