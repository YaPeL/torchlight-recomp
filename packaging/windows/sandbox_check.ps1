<#
Opens the Windows zip in Windows Sandbox: a clean Windows without Visual Studio or the MSVC
redistributable, where a missing DLL shows as the system's "was not found" dialog. The zip's folder
is mapped read-only; at logon the sandbox unpacks the zip to its desktop and starts torchlight.exe,
which with no game files installed shows the first start's setup. With -GamePackageDir, the folder
holding the user's Torchlight XBLA package is mapped read-only too (the sandbox maps folders, not
files), on the sandbox's desktop as "game-package", to pick it in the setup. The sandbox passes no
USB devices, so a gamepad does not reach it: -KeyboardInput starts the game with the SDK's keyboard
controller emulation (--mnk_mode=true: WASD the left stick, Space A, Backspace B, Enter Start,
Shift+arrows the d-pad) and leaves "Torchlight (keyboard).cmd" on the desktop to start it again
the same way. Where the host allows it the sandbox shares the host's GPU (vGPU), so it has the
host's OpenGL; -NoGpu turns that off, leaving Windows' own OpenGL 1.1 and Direct3D 11 in software
(WARP), as on a machine without a GPU driver: the case of the GL3+ fallback. What to look at is in
docs/windows-port.md (Windows package). Needs the Windows Sandbox feature
(Containers-DisposableClientVM).
Usage: sandbox_check.ps1 PACKAGE.zip [-GamePackageDir FOLDER] [-KeyboardInput] [-NoGpu]
#>
param(
    [Parameter(Mandatory = $true)][string]$Zip,
    [string]$GamePackageDir,
    [switch]$KeyboardInput,
    [switch]$NoGpu
)
$ErrorActionPreference = 'Stop'

$sandbox = Join-Path $env:WINDIR 'System32\WindowsSandbox.exe'
if (-not (Test-Path $sandbox)) {
    throw 'Windows Sandbox is not enabled (Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM, then restart)'
}
$zipFile = Get-Item -LiteralPath (Resolve-Path -LiteralPath $Zip).Path
$desktop = 'C:\Users\WDAGUtilityAccount\Desktop'
$inside = "$desktop\package"

# A read-only mapped folder; the paths go into XML, so they are escaped (spaces need nothing).
function Get-MappedFolder([string]$HostFolder, [string]$SandboxFolder) {
    $hostEscaped = [Security.SecurityElement]::Escape($HostFolder)
    $sandboxEscaped = [Security.SecurityElement]::Escape($SandboxFolder)
    return @"
    <MappedFolder>
      <HostFolder>$hostEscaped</HostFolder>
      <SandboxFolder>$sandboxEscaped</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
"@
}
$mapped = Get-MappedFolder $zipFile.DirectoryName $inside
if ($GamePackageDir) {
    $gameDir = (Get-Item -LiteralPath $GamePackageDir).FullName
    if (-not (Test-Path -LiteralPath $gameDir -PathType Container)) { throw "not a folder: $GamePackageDir" }
    $mapped += "`n" + (Get-MappedFolder $gameDir "$desktop\game-package")
}

# What the sandbox runs at logon, as a script in a folder of its own (mapped read-only too), so no
# quoting has to survive the .wsb's command line.
$setup = Join-Path ([IO.Path]::GetTempPath()) 'torchlight_sandbox_check'
New-Item -ItemType Directory -Force $setup | Out-Null
$exe = "$desktop\TorchlightRecomp\torchlight.exe"
$arguments = if ($KeyboardInput) { '--mnk_mode=true' } else { '' }
$logon = @"
Expand-Archive -Force '$inside\$($zipFile.Name)' '$desktop'
if ('$arguments') {
    Set-Content -Encoding ascii '$desktop\Torchlight (keyboard).cmd' '@start "" "$exe" $arguments'
    Start-Process '$exe' -ArgumentList '$arguments'
} else {
    Start-Process '$exe'
}
"@
[IO.File]::WriteAllText((Join-Path $setup 'logon.ps1'), $logon)
$mapped += "`n" + (Get-MappedFolder $setup "$desktop\sandbox-setup")
$command = "powershell -NoProfile -ExecutionPolicy Bypass -File $desktop\sandbox-setup\logon.ps1"
$gpu = if ($NoGpu) { 'Disable' } else { 'Default' }
$config = @"
<Configuration>
  <vGPU>$gpu</vGPU>
  <MappedFolders>
$mapped
  </MappedFolders>
  <LogonCommand>
    <Command>$([Security.SecurityElement]::Escape($command))</Command>
  </LogonCommand>
</Configuration>
"@
$wsb = Join-Path ([IO.Path]::GetTempPath()) 'torchlight_sandbox_check.wsb'
[IO.File]::WriteAllText($wsb, $config)
Write-Output "sandbox_check: $wsb"
Start-Process -FilePath $wsb
