# Copyright Epic Games, Inc. All Rights Reserved.

import os
import sys

#-------------------------------------------------------------------------------
class Pwsh(object):
    def register_shells(self, registrar):
        registrar.add("pwsh", _Shell)
        return super().register_shells(registrar)

#-------------------------------------------------------------------------------
class _Shell(object):
    def __init__(self, system):
        self._system = system

    def get_system(self):
        return self._system

    def boot_shell(self, env, cookie, user_script):
        # no prompt modification here for now
        # could try and modify the Prompt function from the cookie
        # could also publish environment variables users can use from their own prompts
        try: os.makedirs(os.path.dirname(cookie))
        except: pass

        system = self.get_system()
        working_dir = system.get_working_dir()
        shims_path = os.path.normpath(working_dir) + "/shims"
        cmd_tree = system.get_command_tree()
        tree_root = cmd_tree.get_root_node()
        run_py_path = os.path.abspath(__file__ + "/../../core/system/run.py")
        manifest = f"{working_dir}/manifest"
        start_dir = os.getcwd()

        with open(cookie, "wt") as out:
            header = _get_header_script()
            autocompleter = _get_autocompleter_script()
            function_template = _get_function_template_script()
            cleanup = _get_cleanup_script()

            out.write(f"Set-Location \"{start_dir}\"\n")
            # Environment must be written out first so autocomplete daemon gets the right session id
            for key, value in env.read_changes():
                value = value or ""
                value = value.replace("\"", "`\"")
                value = value.replace("$", "`$")
                out.write(f'${{env:{key}}}="{value}"\n')
            out.write(header.format(shims_path=shims_path,working_dir=working_dir))
            out.write(autocompleter.format(python=sys.executable, run_py_path=run_py_path, working_dir=working_dir))
            for name,_ in tree_root.read_children():
                if name.startswith("$"): continue
                out.write(function_template.format(name=name))
            out.write(cleanup)

def _get_header_script():
    return r"""
$ShimsPath = Resolve-Path "{shims_path}"
$env:Path += ";$ShimsPath"
"""

# Braces in this string must be doubled to escape formatting
#TODO: give these functions long names, then optionally add aliases?
def _get_function_template_script():
    return r"""
Register-ArgumentCompleter -CommandName "{name}.exe" -Native -ScriptBlock $SharedCompleter
Register-ArgumentCompleter -CommandName "{name}" -Native -ScriptBlock $SharedCompleter
# Explicitly export this function so that others are not exported automatically
Export-ModuleMember -Function {name}
"""

# If we give commands long names, we need to separate completers that provide the name ushell $complete expects
def _get_autocompleter_script():
    return r"""
function Get-Daemon {{
    if ($script:CompleteDaemonCache -ne $null -and !$script:CompleteDaemonCache.HasExited) {{
        return $script:CompleteDaemonCache
    }}

    $ProcStart = New-Object System.Diagnostics.ProcessStartInfo
    # the $ in $complete must be escaped for powershell
    $DaemonExe = $true
    if ($DaemonExe) {{
        $ProcStart.FileName = [IO.Path]::Combine($ShimsPath, "`$complete")
        $ProcStart.Arguments = @("--daemon")
    }}
    else {{
        $ProcStart.Filename = "{python}"
        $ProcStart.Arguments = @(
            "-Xutf8",
            "-Esu",
            "`"{run_py_path}`"",
            "`"{working_dir}/manifest`"",
            "`$complete",
            "--daemon"
        )
    }}
    $ProcStart.UseShellExecute = $false
    $ProcStart.RedirectStandardOutput = $true
    $ProcStart.RedirectStandardInput = $true

    $script:CompleteDaemonCache = [System.Diagnostics.Process]::Start($ProcStart)
    if ($null -eq $script:CompleteDaemonCache) {{
        throw "Failed to start autocomplete Daemon!"
    }}
    return $script:CompleteDaemonCache
}}

$SharedCompleter = {{
    param($WordToComplete, $CommandAst, $CursorPosition)

    $Words = -split "$CommandAst"
    $Words[0] = $Words[0] -replace "\.exe"

    # Add ... if we have a partial argument
    if (![string]::IsNullOrEmpty($WordToComplete)) {{
        $Words[-1] += "..."
    }}
    else {{
        $Words += ""
    }}

    $CompleteDaemon = Get-Daemon

    $CompleteDaemon.StandardInput.WriteLine("$([char]0x01)")
    foreach ($Word in $Words) {{
        $CompleteDaemon.StandardInput.WriteLine($Word)
    }}
    $CompleteDaemon.StandardInput.WriteLine("$([char]0x02)")
    $CompleteDaemon.StandardInput.Flush()

    $Results = @()
    $Continue = $true
    while ($Continue) {{
        $Option = $CompleteDaemon.StandardOutput.Readline();
        # -eq and .Equals behave differently for the ascii separator bytes used here
        switch -exact ($Option) {{
            {{ "$([char]0x01)".Equals($Option) }} {{
                $Continue = $false
                break;
            }}
            {{ "$([char]0x02)".Equals($Option) }} {{
                $Results = @();
                $Continue = $false
                break;
            }}
            "" {{
                $Continue = $false
                break;
            }}
            default {{
                $Results += $Option
                break;
            }}
        }}
    }}

    # Filter to only arguments that start with our partial match if it exists
    $Results = $Results | Where-Object {{ $_ -like "$WordToComplete*" }}

    if ($results.Count -eq 0) {{
        # prevent powershell from autocompleting paths to match ushell behavior in cmd
        ""
    }}
    else {{
        $results
    }}
}}

$EnvVarsToRemove = [System.Collections.Generic.HashSet[string]]@()

function Update-UShellEnvVars {{
    $Prompt = $env:FLOW_PROMPT
    if ([string]::IsNullOrEmpty($Prompt)) {{
        return
    }}

    $CompleteDaemon = Get-Daemon

    if ($null -eq $CompleteDaemon) {{
        Write-Error "Update-UShellEnvVars - CompleteDaemon is null!"
    }}
    if ($CompleteDaemon.HasExited) {{
        Write-Error "Update-UShellEnvVars - CompleteDaemon has exited!"
    }}

    $CompleteDaemon.StandardInput.WriteLine("$([char]0x01)")
    $CompleteDaemon.StandardInput.WriteLine("`$`$")
    $CompleteDaemon.StandardInput.WriteLine($($executionContext.SessionState.Path.CurrentLocation))
    $CompleteDaemon.StandardInput.WriteLine("$([char]0x02)")

    $Results = @{{}}
    $Continue = $true
    while ($Continue) {{
        $Key = $CompleteDaemon.StandardOutput.Readline();
        # -eq and .Equals behave differently for the ascii separator bytes used here
        if ([string]::IsNullOrEmpty($Key) -or $Key.Equals("$([char]0x01)")) {{
            break
        }}

        if ($Key.Equals("$([char]0x02)")) {{
            $Results = @{{}}
            break
        }}

        $Value = $CompleteDaemon.StandardOutput.Readline()
        if ([string]::IsNullOrEmpty($Value) -or $Value.Equals("$([char]0x01)") -or $Value.Equals("$([char]0x02)")) {{
            $Results = @{{}}
            break
        }}

        $Results[$Key] = $Value
    }}

    foreach ($i in $Results.GetEnumerator()) {{
        $EnvPath = "Env:\USHELL_$($i.Key)"
        $null = $script:EnvVarsToRemove.Add($EnvPath)
        New-Item $EnvPath -Value $i.Value -Force
    }}
}}

Export-ModuleMember -Function "Update-UShellEnvVars"
"""

# Kill the autocomplete daemon so we don't leak a process
def _get_cleanup_script():
    return r"""
$ExecutionContext.SessionState.Module.OnRemove += {
    if ($script:CompleteDaemonCache -ne $null) {
        Stop-Process $script:CompleteDaemonCache
    }
    foreach ($v in $script:EnvVarsToRemove.GetEnumerator()) {
        Remove-Item $v
    }
}
"""