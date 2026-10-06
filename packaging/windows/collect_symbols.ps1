<#
Collects the symbols of the Windows zip's binaries (the counterpart of
packaging/linux/split_debug.sh): for every .exe and .dll of the install tree, the .pdb of the same
name from the given folders (the build tree, OGRE's and the SDK's bin), copied to OUTPUT_DIR at the
binary's relative path. The zip never carries them (make_zip.ps1 refuses them); the release keeps
them in the symbols repository. Binaries without one (the MSVC runtime, the SDK's Release DLLs) are
listed.
Usage: collect_symbols.ps1 INSTALL_DIR OUTPUT_DIR SEARCH_DIR...
#>
param(
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$SearchDirs
)
$ErrorActionPreference = 'Stop'

$install = (Resolve-Path $InstallDir).Path
New-Item -ItemType Directory -Force $OutputDir | Out-Null
$without = @()
foreach ($binary in Get-ChildItem -Recurse -File $install | Where-Object { $_.Extension -in '.exe', '.dll' }) {
    $relative = $binary.DirectoryName.Substring($install.Length).TrimStart('\')
    $name = "$($binary.BaseName).pdb"
    $pdb = $SearchDirs | ForEach-Object { Join-Path $_ $name } | Where-Object { Test-Path $_ } |
        Select-Object -First 1
    if (-not $pdb) { $without += $binary.Name; continue }
    $destination = [IO.Path]::Combine($OutputDir, $relative)
    New-Item -ItemType Directory -Force $destination | Out-Null
    Copy-Item $pdb $destination
    Write-Output "collect_symbols: $([IO.Path]::Combine($relative, $name))"
}
Write-Output "collect_symbols: without symbols: $($without -join ', ')"
