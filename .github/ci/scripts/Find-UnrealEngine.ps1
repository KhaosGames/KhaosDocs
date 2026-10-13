<#
.SYNOPSIS
Fail early, and clearly, when this machine does not have an engine version the plugin targets.

.DESCRIPTION
Prints which engine build will be used, so a run records exactly what it was tested against. A
missing engine is an error rather than a skipped version: Fab compiles the plugin for every engine
version on the listing, so a version that cannot be built here would ship untested.

.EXAMPLE
./Find-UnrealEngine.ps1 -EngineVersion 5.8
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string]$EngineVersion
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'KhaosPluginCI.psm1') -Force

$Root = Find-UnrealEngine $EngineVersion
$Build = Get-UnrealEngineBuild $Root
Write-Host "Unreal Engine $($Build.Description) at $Root"
Add-CiSummary "Built and tested against Unreal Engine $($Build.Description) on $([Environment]::MachineName)."
Set-CiOutput 'root' $Root
