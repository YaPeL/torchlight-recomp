<#
Makes the Windows zip from a `cmake --install` tree (the Windows counterpart of
packaging/linux/make_appimage.sh): the tree as installed (torchlight.exe next to the DLLs it loads,
the GPU plugins, the MSVC runtime, data/ui and ogre/; the top CMakeLists.txt) in one folder,
TorchlightRecomp/, inside the zip. Symbols (.pdb) never go in: the release keeps them apart.
Usage: make_zip.ps1 INSTALL_DIR OUTPUT.zip
#>
param(
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [Parameter(Mandatory = $true)][string]$Output
)
$ErrorActionPreference = 'Stop'

$install = (Resolve-Path $InstallDir).Path
if (-not (Test-Path (Join-Path $install 'torchlight.exe'))) {
    throw "no torchlight.exe in $InstallDir"
}
$symbols = @(Get-ChildItem -Recurse -File $install -Filter '*.pdb')
if ($symbols.Count -gt 0) {
    throw "symbols in the install tree (they do not go in the zip): $($symbols.Name -join ', ')"
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("tl_zip_" + [Guid]::NewGuid().ToString('N'))
$folder = Join-Path $work 'TorchlightRecomp'
try {
    New-Item -ItemType Directory -Force $folder | Out-Null
    Copy-Item -Recurse -Path (Join-Path $install '*') -Destination $folder
    $outputPath = [IO.Path]::GetFullPath($Output)
    if (Test-Path $outputPath) { Remove-Item $outputPath }
    # Entries named with '/' (the zip format's separator; .NET Framework's CreateFromDirectory
    # writes '\', which other unzip tools take as part of the name).
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::Open($outputPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -Recurse -File $folder) {
            $name = $file.FullName.Substring($work.Length + 1).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $name,
                [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally {
        $zip.Dispose()
    }
    Write-Output "make_zip: $outputPath"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
