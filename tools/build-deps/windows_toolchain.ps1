<#
.SYNOPSIS
Puts the LLVM the Windows build uses (21.1.8, the official win64 installer, pinned by SHA-256) in a
folder of its own: the Windows counterpart of tools/deps/ubuntu_toolchain.sh, for CI. A developer
installs the same LLVM with that installer by hand (docs/windows-port.md).

.DESCRIPTION
The installer is unpacked with 7-Zip (on GitHub's Windows images), not run: running it would first
uninstall the runner's own LLVM, of another version, and write to the registry. The rest of the
toolchain (Visual Studio 2022 with the MSVC x64 tools and a Windows SDK, CMake, Ninja, Git) is the
runner's; tools/build-deps/windows.ps1 loads the MSVC environment itself. The folder's bin is given
to windows.ps1 as -LlvmBin and put first in PATH for the project's build.

.EXAMPLE
tools\build-deps\windows_toolchain.ps1 -Destination C:\llvm
#>
param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$WorkDir = [IO.Path]::GetTempPath(),
    [string]$SevenZip = (Join-Path $env:ProgramFiles '7-Zip\7z.exe')
)
$ErrorActionPreference = 'Stop'

$LlvmVersion = '21.1.8'
$LlvmUrl = "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LlvmVersion/LLVM-$LlvmVersion-win64.exe"
$LlvmSha256 = '7a5386c26497db1691f320121e5b113364dd0274b98e55f15f4dbc00c0450113'

$installer = Join-Path $WorkDir "LLVM-$LlvmVersion-win64.exe"
if (-not (Test-Path $installer)) {
    Invoke-WebRequest -Uri $LlvmUrl -OutFile $installer -UseBasicParsing
}
$hash = (Get-FileHash $installer -Algorithm SHA256).Hash.ToLower()
if ($hash -ne $LlvmSha256) {
    throw "LLVM-$LlvmVersion-win64.exe: SHA-256 $hash, expected $LlvmSha256"
}
& $SevenZip x -y -bd "-o$Destination" $installer '-x!$PLUGINSDIR' '-xr!Uninstall.exe' | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "7-Zip failed with exit code $LASTEXITCODE"
}
$clang = Join-Path $Destination 'bin\clang++.exe'
$version = (& $clang --version | Select-Object -First 1)
if ($version -notmatch "clang version $([regex]::Escape($LlvmVersion))\b") {
    throw "$clang is not LLVM $LlvmVersion`: $version"
}
Write-Host "LLVM $LlvmVersion in $Destination ($version)"
