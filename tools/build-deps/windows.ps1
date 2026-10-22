<#
.SYNOPSIS
Builds the host dependencies of the Windows build: zlib 1.3.2 (static) and OGRE 14.6.0 (GL3+ and
Direct3D 11 render systems, RTSS with its shaders, STBI codec), installed into one prefix; with
-SdkPrefix also the ReXGlue SDK with this project's patches, into its own prefix.

.DESCRIPTION
The SDK (with -SdkPrefix) is the Windows counterpart of tools/deps/build_sdk.sh: the same repository
and commit (read from that script), checked out with LF endings (the patches are LF), the patches of
patches/series in order without the "posix" ones, built with the SDK's win-amd64 preset (LLVM's
clang) and installed for -SdkConfigs. CI builds Release only, as on Linux; a developer's SDK has
every configuration (the default).


The same OGRE options as the Linux recipe in docs/BUILDING.md (without the Wayland build of the GL3+ plugin),
plus the Direct3D 11 render system (it uses the Windows SDK's D3D11, DXGI and D3DCompiler), with
the dynamic C runtime the ReXGlue SDK uses (/MD, /MDd in Debug). zlib is built with LLVM 21's
clang, like the game. OGRE is built with MSVC's cl.exe, its supported Windows compiler: its Win32 GL
code has two constructs only cl.exe accepts, and clang rejects them with either driver (clang++ and
clang-cl):
  - RenderSystems/GLSupport/src/win32/OgreWin32GLSupport.cpp:254 and OgreWin32Window.cpp:86:
    `static const TCHAR staticVar;`, a const object without an initializer;
  - RenderSystems/GL3Plus/src/GLSL/src/OgreGLSLProgramManager.cpp:43: the explicit specialization of
    Singleton<GLSLProgramManager>::msSingleton after its instantiation (OGRE silences it for cl.exe
    with /wd4661).
The game's clang++ targets the same MSVC C++ ABI, and the runtime of every library is checked after
the build, so OGRE's DLLs link with the rest unchanged; OGRE gets only our functional patches
(patches/ogre-*.patch, as on the other platforms), none for the compiler. Every
configuration in -Configs is built and installed into the same prefix (OGRE names its Debug files
with "_d", zlib with "d"). zlib is pinned by SHA-256 and OGRE by commit; nothing else is downloaded.
OGRE finds zlib in the prefix and builds Codec_STBI with it, as on Linux with the system zlib.

Requirements: Visual Studio 2022 (or its Build Tools) with the MSVC x64 tools (cl.exe), a Windows
SDK and the CMake tools (CMake and Ninja); LLVM 21; Git. The MSVC environment is loaded by the script.

.EXAMPLE
tools\build-deps\windows.ps1
tools\build-deps\windows.ps1 -Prefix D:\ogre14-install -WorkDir D:\ogre-build -Configs RelWithDebInfo
tools\build-deps\windows.ps1 -Prefix D:\deps\ogre -SdkPrefix D:\deps\sdk -Configs RelWithDebInfo -SdkConfigs Release
tools\build-deps\windows.ps1 -SdkPrefix D:\sdk-tests\sdk -WorkDir D:\sdk-tests -SdkConfigs Release -SdkTests
#>
param(
    # Install prefix; %USERPROFILE%\ogre14-install is the default of TORCHLIGHT_OGRE_INSTALL.
    [string]$Prefix = (Join-Path $env:USERPROFILE 'ogre14-install'),
    # Downloads, sources and build directories.
    [string]$WorkDir = (Join-Path $env:USERPROFILE 'ogre14-build'),
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string[]]$Configs = @('Debug', 'RelWithDebInfo'),
    [string]$LlvmBin = 'C:\Program Files\LLVM\bin',
    # The SDK's install prefix (what -DCMAKE_PREFIX_PATH finds); empty: the SDK is not built.
    [string]$SdkPrefix = '',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string[]]$SdkConfigs = @('Debug', 'Release', 'RelWithDebInfo'),
    # Only the SDK (needs -SdkPrefix), with its own tests built and run in its first -SdkConfigs;
    # fails if one fails, is skipped or does not run. No zlib or OGRE. The sdk-tests workflow.
    [switch]$SdkTests
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$ZlibVersion = '1.3.2'
$ZlibUrl = "https://github.com/madler/zlib/releases/download/v$ZlibVersion/zlib-$ZlibVersion.tar.gz"
$ZlibSha256 = 'bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16'
$OgreTag = 'v14.6.0'
$OgreCommit = '5cccbeec824798629931cfd9184bde729610e30d'
$OgreUrl = 'https://github.com/OGRECave/ogre.git'

# The C runtime of the ReXGlue SDK: the DLL runtime, its debug flavor in Debug.
$MsvcRuntime = 'MultiThreaded$<$<CONFIG:Debug>:Debug>DLL'

# Runs a program and fails on its exit code only: Windows PowerShell turns a program's stderr into
# error records when the output is redirected (CI), which would stop the script on a warning.
function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments)
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe failed with exit code $LASTEXITCODE"
    }
}

# Loads the MSVC x64 environment (STL, Windows SDK, CMake, Ninja) into this session.
function Import-MsvcEnvironment {
    if ($env:VCToolsInstallDir -and (Get-Command cmake -ErrorAction SilentlyContinue)) {
        return
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe not found: install Visual Studio 2022 or its Build Tools'
    }
    $vs = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) {
        throw 'No Visual Studio installation with the MSVC x64 tools'
    }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    # Its stderr too: vcvars complains there when vswhere is not in PATH, which with the output
    # redirected (CI) would stop the script; its exit code still tells a failure.
    $lines = & cmd.exe /d /s /c "`"$vcvars`" >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) {
        throw "$vcvars failed"
    }
    foreach ($line in $lines) {
        $eq = $line.IndexOf('=')
        if ($eq -gt 0) {
            Set-Item -Path "env:$($line.Substring(0, $eq))" -Value $line.Substring($eq + 1)
        }
    }
}

# Checks the C runtime of a library or DLL: the runtime DLLs it imports, or for a static library the
# default libraries its objects ask for.
function Test-Runtime {
    param([string]$File, [bool]$Debug)
    if ($File.EndsWith('.dll')) {
        $text = (& dumpbin /nologo /dependents $File) -join "`n"
        $ok = if ($Debug) { $text -match '(?i)vcruntime140d\.dll' } `
              else { ($text -match '(?i)vcruntime140\.dll') -and ($text -notmatch '(?i)vcruntime140d\.dll') }
    } else {
        $text = (& dumpbin /nologo /directives $File) -join "`n"
        $ok = if ($Debug) { $text -match '(?i)defaultlib:"?msvcrtd' } `
              else { ($text -match '(?i)defaultlib:"?msvcrt[^d]') -and ($text -notmatch '(?i)defaultlib:"?msvcrtd') }
    }
    if (-not $ok) {
        throw "$File does not use the expected C runtime ($(if ($Debug) { '/MDd' } else { '/MD' }))"
    }
    Write-Host "runtime ok: $File"
}

# The ReXGlue SDK into -SdkPrefix (with -SdkTests, its tests built and run too).
function Build-Sdk {
    # The repository and commit of tools/deps/build_sdk.sh, so both platforms build the same SDK.
    $buildSdk = Get-Content -Raw (Join-Path $Root 'tools\deps\build_sdk.sh')
    $SdkRepository = [regex]::Match($buildSdk, '(?m)^SDK_REPOSITORY=(\S+)').Groups[1].Value
    $SdkCommit = [regex]::Match($buildSdk, '(?m)^SDK_COMMIT=(\S+)').Groups[1].Value
    if (-not $SdkRepository -or -not $SdkCommit) {
        throw 'SDK_REPOSITORY or SDK_COMMIT not found in tools/deps/build_sdk.sh'
    }
    New-Item -ItemType Directory -Force $SdkPrefix | Out-Null
    $SdkPrefix = (Resolve-Path $SdkPrefix).Path
    $sdkSrc = Join-Path $WorkDir 'rexglue-sdk'
    if (-not (Test-Path (Join-Path $sdkSrc '.git'))) {
        Invoke-Native 'git' @('clone', '--quiet', '-c', 'core.autocrlf=false', $SdkRepository,
            $sdkSrc)
    }
    Invoke-Native 'git' @('-C', $sdkSrc, 'config', 'core.autocrlf', 'false')
    Invoke-Native 'git' @('-C', $sdkSrc, '-c', 'advice.detachedHead=false', 'checkout', '--quiet',
        '--force', $SdkCommit)
    Invoke-Native 'git' @('-C', $sdkSrc, 'clean', '-fdxq', '-e', 'out')
    Invoke-Native 'git' @('-C', $sdkSrc, 'submodule', 'update', '--init', '--recursive', '--quiet')

    # The patches, in order; the POSIX-only ones are skipped.
    foreach ($line in Get-Content (Join-Path $Root 'patches\series')) {
        if ($line -match '^\s*(#|$)') { continue }
        $patch, $kind = -split $line
        if ($kind -eq 'posix') { continue }
        Write-Host "patch: $patch"
        Invoke-Native 'git' @('-C', $sdkSrc, 'apply', (Join-Path $Root "patches\$patch"))
    }

    $sdkConfigList = $SdkConfigs -join ';'
    Push-Location $sdkSrc
    try {
        Invoke-Native 'cmake' (@('--preset', 'win-amd64',
            "-DCMAKE_CONFIGURATION_TYPES=$sdkConfigList",
            "-DCMAKE_DEFAULT_BUILD_TYPE=$($SdkConfigs[0])", "-DCMAKE_CROSS_CONFIGS=$sdkConfigList",
            "-DCMAKE_DEFAULT_CONFIGS=$sdkConfigList", "-DCMAKE_INSTALL_PREFIX=$SdkPrefix") +
            $(if ($SdkTests) { @('-DREXGLUE_BUILD_TESTS=ON') } else { @() }))
        Invoke-Native 'cmake' @('--build', 'out/build/win-amd64', '--target', 'install',
            '--parallel')
        if ($SdkTests) {
            Test-Sdk
        }
    } finally {
        Pop-Location
    }
    if ($SdkConfigs -contains 'Release') {
        Test-Runtime (Join-Path $SdkPrefix 'bin\rexruntime.dll') $false
    }
    Write-Host "Installed the ReXGlue SDK $SdkCommit ($($SdkConfigs -join ', ')) into $SdkPrefix"
}

# Runs the SDK's tests (from its checkout): every one has to run and pass, and the only skips are
# Windows' in tools/deps/sdk_test_skips.txt, checked from ctest's JUnit file by
# tools/deps/check_sdk_tests.py as build_sdk.sh does. One at a time: they are not written for
# parallel processes.
function Test-Sdk {
    $python = (Get-Command python -ErrorAction Stop).Source
    $junit = Join-Path $WorkDir 'sdk-tests.xml'
    # A reused -WorkDir's from an earlier run cannot pass the check.
    Remove-Item -Force -ErrorAction SilentlyContinue $junit
    $ErrorActionPreference = 'Continue'
    & ctest --test-dir out/build/win-amd64 -C $SdkConfigs[0] --output-on-failure --no-tests=error `
        --output-junit $junit 2>&1 | ForEach-Object { "$_" }
    & $python (Join-Path $Root 'tools\deps\check_sdk_tests.py') $junit windows
    if ($LASTEXITCODE -ne 0) {
        throw 'the SDK tests are not as expected: see above'
    }
}

Import-MsvcEnvironment
$env:PATH = "$LlvmBin;$env:PATH"
Invoke-Native 'clang++' @('--version')
New-Item -ItemType Directory -Force $WorkDir, $Prefix | Out-Null
$Prefix = (Resolve-Path $Prefix).Path
$WorkDir = (Resolve-Path $WorkDir).Path

if ($SdkTests) {
    if (-not $SdkPrefix) {
        throw '-SdkTests needs -SdkPrefix'
    }
    Build-Sdk
    return
}

# zlib: the release tarball, checked against its SHA-256.
$zlibTar = Join-Path $WorkDir "zlib-$ZlibVersion.tar.gz"
$zlibSrc = Join-Path $WorkDir "zlib-$ZlibVersion"
if (-not (Test-Path $zlibTar)) {
    Invoke-WebRequest -Uri $ZlibUrl -OutFile $zlibTar -UseBasicParsing
}
$hash = (Get-FileHash $zlibTar -Algorithm SHA256).Hash.ToLower()
if ($hash -ne $ZlibSha256) {
    throw "zlib-$ZlibVersion.tar.gz: SHA-256 $hash, expected $ZlibSha256"
}
if (-not (Test-Path $zlibSrc)) {
    Invoke-Native 'tar' @('-xf', $zlibTar, '-C', $WorkDir)
}

# OGRE: the release tag, checked against its commit.
$ogreSrc = Join-Path $WorkDir "ogre-$($OgreTag.TrimStart('v'))"
if (-not (Test-Path $ogreSrc)) {
    # LF line endings: our patches are LF and do not apply to a CRLF checkout.
    Invoke-Native 'git' @('-c', 'advice.detachedHead=false', '-c', 'core.autocrlf=false', 'clone', '--depth', '1',
        '--branch', $OgreTag, $OgreUrl, $ogreSrc)
}
$head = (& git -C $ogreSrc rev-parse HEAD).Trim()
if ($head -ne $OgreCommit) {
    throw "$ogreSrc is at $head, expected $OgreCommit ($OgreTag)"
}
# Our patches to OGRE (patches/ogre-*.patch, patches/README.md), in name order; a checkout they are
# already applied to (a reused -WorkDir) is left as it is.
foreach ($patch in (Get-ChildItem (Join-Path $Root 'patches') -Filter 'ogre-*.patch' | Sort-Object Name)) {
    # The check's stderr redirected: Windows PowerShell would turn it into an error record and stop
    # the script (as Invoke-Native says), so errors only continue here.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & git -C $ogreSrc apply --reverse --check $patch.FullName 2>$null
    $applied = $LASTEXITCODE -eq 0
    $ErrorActionPreference = $previous
    if ($applied) { continue }
    & git -C $ogreSrc apply $patch.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "$($patch.Name) does not apply to $ogreSrc (a CRLF checkout from before? delete it and run again)"
    }
}

$common = @(
    '-G', 'Ninja',
    "-DCMAKE_INSTALL_PREFIX=$Prefix",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=$MsvcRuntime",
    '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW'
)
$ogreCompilers = @('-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl')

foreach ($config in $Configs) {
    $debug = $config -eq 'Debug'

    # zlib, static only (zs.lib, zsd.lib in Debug).
    $zlibBuild = Join-Path $WorkDir "build-zlib-$config"
    Invoke-Native 'cmake' (@('-S', $zlibSrc, '-B', $zlibBuild, "-DCMAKE_BUILD_TYPE=$config",
        '-DCMAKE_C_COMPILER=clang', '-DZLIB_BUILD_SHARED=OFF', '-DZLIB_BUILD_STATIC=ON',
        '-DZLIB_BUILD_TESTING=OFF', '-DZLIB_BUILD_MINIZIP=OFF') + $common)
    Invoke-Native 'cmake' @('--build', $zlibBuild)
    Invoke-Native 'cmake' @('--install', $zlibBuild)
    Test-Runtime (Join-Path $Prefix "lib\zs$(if ($debug) { 'd' }).lib") $debug

    # OGRE: the options of the Linux recipe (README.md). FindZLIB does not know zlib's static names,
    # so the libraries are given explicitly.
    $ogreBuild = Join-Path $WorkDir "build-ogre-$config"
    Invoke-Native 'cmake' (@('-S', $ogreSrc, '-B', $ogreBuild, "-DCMAKE_BUILD_TYPE=$config") +
        $ogreCompilers + $common + @(
        "-DZLIB_INCLUDE_DIR=$Prefix\include",
        "-DZLIB_LIBRARY_RELEASE=$Prefix\lib\zs.lib",
        "-DZLIB_LIBRARY_DEBUG=$Prefix\lib\zsd.lib",
        '-DOGRE_BUILD_DEPENDENCIES=OFF',
        '-DOGRE_BUILD_RENDERSYSTEM_GL=OFF', '-DOGRE_BUILD_RENDERSYSTEM_GL3PLUS=ON',
        '-DOGRE_BUILD_RENDERSYSTEM_GLES2=OFF', '-DOGRE_BUILD_RENDERSYSTEM_VULKAN=OFF',
        '-DOGRE_BUILD_RENDERSYSTEM_D3D9=OFF', '-DOGRE_BUILD_RENDERSYSTEM_D3D11=ON',
        '-DOGRE_BUILD_RENDERSYSTEM_TINY=OFF', '-DOGRE_BUILD_PLUGIN_STBI=ON',
        '-DOGRE_BUILD_PLUGIN_ASSIMP=OFF', '-DOGRE_BUILD_PLUGIN_BSP=OFF', '-DOGRE_BUILD_PLUGIN_OCTREE=OFF',
        '-DOGRE_BUILD_PLUGIN_PFX=OFF', '-DOGRE_BUILD_PLUGIN_DOT_SCENE=OFF', '-DOGRE_BUILD_PLUGIN_PCZ=OFF',
        '-DOGRE_BUILD_PLUGIN_CG=OFF', '-DOGRE_BUILD_PLUGIN_FREEIMAGE=OFF',
        '-DOGRE_BUILD_PLUGIN_EXRCODEC=OFF', '-DOGRE_BUILD_PLUGIN_GLSLANG=OFF',
        '-DOGRE_BUILD_PLUGIN_RSIMAGE=OFF', '-DOGRE_BUILD_COMPONENT_RTSHADERSYSTEM=ON',
        '-DOGRE_BUILD_RTSHADERSYSTEM_SHADERS=ON', '-DOGRE_BUILD_COMPONENT_OVERLAY=OFF',
        '-DOGRE_BUILD_COMPONENT_BITES=OFF', '-DOGRE_BUILD_COMPONENT_PAGING=OFF',
        '-DOGRE_BUILD_COMPONENT_MESHLODGENERATOR=OFF', '-DOGRE_BUILD_COMPONENT_TERRAIN=OFF',
        '-DOGRE_BUILD_COMPONENT_VOLUME=OFF', '-DOGRE_BUILD_COMPONENT_PROPERTY=OFF',
        '-DOGRE_BUILD_COMPONENT_BULLET=OFF', '-DOGRE_BUILD_COMPONENT_PYTHON=OFF',
        '-DOGRE_BUILD_COMPONENT_JAVA=OFF', '-DOGRE_BUILD_COMPONENT_CSHARP=OFF',
        '-DOGRE_BUILD_SAMPLES=OFF', '-DOGRE_BUILD_TESTS=OFF', '-DOGRE_BUILD_TOOLS=OFF',
        '-DOGRE_INSTALL_DOCS=OFF', '-DOGRE_INSTALL_SAMPLES=OFF', '-DOGRE_INSTALL_TOOLS=OFF',
        '-DOGRE_CONFIG_ENABLE_ZIP=OFF'))
    Invoke-Native 'cmake' @('--build', $ogreBuild)
    Invoke-Native 'cmake' @('--install', $ogreBuild)
    $suffix = if ($debug) { '_d' } else { '' }
    foreach ($dll in @("OgreMain$suffix.dll", "OgreRTShaderSystem$suffix.dll",
                       "RenderSystem_GL3Plus$suffix.dll", "RenderSystem_Direct3D11$suffix.dll",
                       "Codec_STBI$suffix.dll")) {
        Test-Runtime (Join-Path $Prefix "bin\$dll") $debug
    }
}

Write-Host "Installed zlib $ZlibVersion and OGRE $OgreTag ($($Configs -join ', ')) into $Prefix"

if ($SdkPrefix) {
    Build-Sdk
}
