<#
.SYNOPSIS
Run a built plugin's automation tests in a headless editor and report the results.

.DESCRIPTION
The plugin, as BuildPlugin packaged it with binaries, goes into a blank host project, the way a
buyer installs it. That also proves the packaged plugin loads, which a compile alone does not.

The editor runs with -nullrhi, so tests needing a GPU (flagged NonNullRHI) are not run. Results are
read from the JSON report rather than the exit code, which does not reflect test failures. A run
that matches no tests fails, so a renamed test or a wrong filter cannot pass silently.

.EXAMPLE
./Invoke-PluginTests.ps1 -PluginDir C:\t\b\out\KhaosDocs -EngineVersion 5.8 -WorkDir C:\t\t
#>
[CmdletBinding()]
param(
	# The packaged plugin from Build-Plugin.ps1, with Binaries.
	[Parameter(Mandatory)][string]$PluginDir,
	[Parameter(Mandatory)][string]$EngineVersion,
	[Parameter(Mandatory)][string]$WorkDir,

	# Automation test filter, matched against full test names. Defaults to the plugin name, so
	# name tests '<PluginName>.<Area>.<Case>'.
	[string]$Filter,

	[int]$TimeoutMinutes = 30
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'KhaosPluginCI.psm1') -Force

$EngineRoot = Find-UnrealEngine $EngineVersion
$Editor = Join-Paths $EngineRoot 'Engine' 'Binaries' 'Win64' 'UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $Editor)) { throw "No editor at $Editor." }

$Descriptor = Get-PluginDescriptor $PluginDir
$PluginName = $Descriptor.Name
if (-not $Filter) { $Filter = $PluginName }

$WorkDir = Get-FullPath $WorkDir
$HostDir = Join-Paths $WorkDir 'Host'
$ReportDir = Join-Paths $WorkDir 'report'
$LogDir = Join-Paths $WorkDir 'logs'
Reset-Directory $HostDir
Reset-Directory $ReportDir
New-Item -ItemType Directory -Path $LogDir -Force | Out-Null

Copy-Item -LiteralPath $Descriptor.Dir -Destination (Join-Paths $HostDir 'Plugins' $PluginName) -Recurse
$Project = Join-Paths $HostDir 'Host.uproject'
Write-Utf8File $Project (@"
{
	"FileVersion": 3,
	"EngineAssociation": "$EngineVersion",
	"Plugins": [ { "Name": "$PluginName", "Enabled": true } ]
}
"@)

$EditorLog = Join-Paths $LogDir 'Editor.log'
# Start-Process joins an argument array with bare spaces, so quoting is done here.
$Arguments = @(
	"`"$Project`""
	"-ExecCmds=`"Automation RunTests $Filter`""
	'-TestExit="Automation Test Queue Empty"'
	"-ReportExportPath=`"$ReportDir`""
	"-abslog=`"$EditorLog`""
	'-unattended', '-nopause', '-nosplash', '-nullrhi', '-nosound', '-NoLoadingScreen', '-buildmachine'
) -join ' '

Start-CiGroup "Automation tests matching '$Filter' on UE $EngineVersion"
Write-Host "> `"$Editor`" $Arguments" -ForegroundColor DarkGray
$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -NoNewWindow -PassThru `
	-RedirectStandardOutput (Join-Paths $LogDir 'Editor.stdout.txt') `
	-RedirectStandardError (Join-Paths $LogDir 'Editor.stderr.txt')
# Without touching Handle first, ExitCode reads back empty once the process has gone.
$null = $Process.Handle

if (-not $Process.WaitForExit($TimeoutMinutes * 60 * 1000))
{
	& taskkill.exe /PID $Process.Id /T /F | Out-Null
	Stop-CiGroup
	throw "The editor did not finish within $TimeoutMinutes minutes and was killed. See $EditorLog."
}
$ExitCode = $Process.ExitCode

# The automation lines tell the story; the full log is uploaded with the run.
if (Test-Path -LiteralPath $EditorLog)
{
	Get-Content -LiteralPath $EditorLog | Where-Object { $_ -match 'Test Completed\.|LogAutomationCommandLine|Error:|Fatal' } | ForEach-Object { Write-Host $_ }
}
Write-Host "Editor exited with code $ExitCode."
Stop-CiGroup

$Index = Join-Paths $ReportDir 'index.json'
if (-not (Test-Path -LiteralPath $Index))
{
	throw "No test report was written (editor exit code $ExitCode). The editor most likely failed to start or to load $PluginName; see $EditorLog."
}

$Report = [IO.File]::ReadAllText($Index) | ConvertFrom-Json
$Tests = @(Get-JsonValue $Report 'tests' @())

$Summary = @(
	"### UE $EngineVersion automation tests: $PluginName"
	''
)

if ($Tests.Count -eq 0)
{
	Add-CiSummary (($Summary + "No tests matched ``$Filter``.") -join "`n")
	throw "No automation tests matched '$Filter'. Name tests '$PluginName.<Area>.<Case>', set the test-filter input, or set run-tests to false for a plugin without tests."
}

$Failed = 0
$Rows = @()
foreach ($Test in ($Tests | Sort-Object { $_.fullTestPath }))
{
	$State = [string]$Test.state
	$Duration = '{0:N2}s' -f [double](Get-JsonValue $Test 'duration' 0)
	$Icon = ':white_check_mark:'
	if ($State -eq 'Fail') { $Icon = ':x:'; $Failed++ }
	elseif ($State -ne 'Success') { $Icon = ':grey_question:' }
	elseif ([int](Get-JsonValue $Test 'warnings' 0) -gt 0) { $Icon = ':warning:' }
	$Rows += "| $Icon | ``$($Test.fullTestPath)`` | $State | $Duration |"

	if ($State -ne 'Fail') { continue }

	$Messages = @()
	$File = $null
	$Line = 0
	foreach ($Entry in @(Get-JsonValue $Test 'entries' @()))
	{
		if ([string]$Entry.event.type -ne 'Error') { continue }
		$Messages += [string]$Entry.event.message
		# Point at the assertion when it is in the plugin's own sources.
		if (-not $File -and $Entry.filename -match ('(?i)[\\/]Plugins[\\/]' + [regex]::Escape($PluginName) + '[\\/](?<path>.+)$'))
		{
			$File = $Matches['path'].Replace('\', '/')
			$Line = [int]$Entry.lineNumber
		}
	}
	if ($Messages.Count -eq 0) { $Messages = @('Failed without an error message; see the editor log.') }
	Write-CiAnnotation error ($Messages -join "`n") -File $File -Line $Line -Title "$($Test.fullTestPath) (UE $EngineVersion)"
}

$Passed = @($Tests | Where-Object { $_.state -eq 'Success' }).Count
$Summary += "$Passed of $($Tests.Count) passed."
$Summary += ''
$Summary += '| | Test | Result | Time |'
$Summary += '| --- | --- | --- | --- |'
$Summary += $Rows
Add-CiSummary ($Summary -join "`n")

Write-Host "$Passed of $($Tests.Count) tests passed."
if ($Failed -gt 0)
{
	throw "$Failed automation test(s) failed on UE $EngineVersion."
}
if ($Passed -ne $Tests.Count)
{
	throw "$($Tests.Count - $Passed) test(s) did not run to completion on UE $EngineVersion."
}
