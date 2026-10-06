<#
Checks the Windows zip (the counterpart of packaging/linux/check_appimage.sh): every DLL each
executable and DLL in it imports is either in the zip's folder, where Windows looks first (next to
torchlight.exe; OGRE's plugins in ogre/plugins/ also find their DLLs there, as OgreMain.dll loads
them from the process), or a Windows one: an API set (api-ms-win-*, ext-ms-*) or a DLL of this
machine's System32 that is not part of a redistributable runtime (the MSVC runtime may be installed
here and must not hide one missing from the zip). Also: no symbols (.pdb) and no debug builds in it.
Prints what the zip carries and who imports each DLL.
Usage: check_zip.ps1 PACKAGE.zip
#>
param([Parameter(Mandatory = $true)][string]$Zip)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
public static class PeImports {
    // The DLL names in a PE file's import table (normal imports; delay-loaded ones are optional).
    public static List<string> Read(string path) {
        var names = new List<string>();
        byte[] b = File.ReadAllBytes(path);
        int pe = BitConverter.ToInt32(b, 0x3C);
        if (BitConverter.ToUInt32(b, pe) != 0x4550) throw new Exception("not a PE file: " + path);
        int sections = BitConverter.ToUInt16(b, pe + 6);
        int optSize = BitConverter.ToUInt16(b, pe + 20);
        int opt = pe + 24;
        bool pe32plus = BitConverter.ToUInt16(b, opt) == 0x20B;
        int dirs = opt + (pe32plus ? 112 : 96);
        uint importRva = BitConverter.ToUInt32(b, dirs + 8);
        if (importRva == 0) return names;
        int sectionTable = opt + optSize;
        Func<uint, int> offset = rva => {
            for (int i = 0; i < sections; ++i) {
                int s = sectionTable + i * 40;
                uint va = BitConverter.ToUInt32(b, s + 12);
                uint size = Math.Max(BitConverter.ToUInt32(b, s + 8), BitConverter.ToUInt32(b, s + 16));
                if (rva >= va && rva < va + size) return (int)(rva - va + BitConverter.ToUInt32(b, s + 20));
            }
            throw new Exception("RVA outside the sections: " + path);
        };
        for (int d = offset(importRva); ; d += 20) {
            uint nameRva = BitConverter.ToUInt32(b, d + 12);
            if (nameRva == 0) break;
            int n = offset(nameRva), end = n;
            while (b[end] != 0) ++end;
            names.Add(Encoding.ASCII.GetString(b, n, end - n));
        }
        return names;
    }
}
'@

# Runtimes Windows does not ship: present in System32 only when something installed them.
$redistributable = '^(vcruntime|msvcp|concrt|vccorlib|vcomp|msvcr|mfc|ucrtbased)'
$system32 = [Environment]::GetFolderPath('System')

$work = Join-Path ([IO.Path]::GetTempPath()) ("tl_check_" + [Guid]::NewGuid().ToString('N'))
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory((Resolve-Path $Zip).Path, $work)
    $root = Join-Path $work 'TorchlightRecomp'
    if (-not (Test-Path (Join-Path $root 'torchlight.exe'))) { throw "no TorchlightRecomp\torchlight.exe in $Zip" }
    $failed = $false
    $pdbs = @(Get-ChildItem -Recurse -File $root -Filter '*.pdb')
    if ($pdbs.Count) { Write-Output "SYMBOLS in the zip: $($pdbs.Name -join ', ')"; $failed = $true }
    $local = @{}
    Get-ChildItem -File $root -Filter '*.dll' | ForEach-Object { $local[$_.Name.ToLower()] = $_.Name }
    $binaries = @(Get-ChildItem -Recurse -File $root | Where-Object { $_.Extension -in '.exe', '.dll' })
    $importers = @{}
    foreach ($file in $binaries) {
        $relative = $file.FullName.Substring($root.Length + 1)
        # Debug and profiling builds: OGRE's "_d", the SDK's "d" and "rd" next to the release name.
        foreach ($suffix in '_d', 'rd', 'd') {
            $release = ($file.Name -replace "$suffix\.dll$", '.dll').ToLower()
            if ($release -ne $file.Name.ToLower() -and ($local.ContainsKey($release) -or
                    (Test-Path (Join-Path $file.DirectoryName $release)))) {
                Write-Output "DEBUG BUILD in the zip: $relative"; $failed = $true; break
            }
        }
        foreach ($dll in [PeImports]::Read($file.FullName)) {
            $key = $dll.ToLower()
            if (-not $importers.ContainsKey($key)) { $importers[$key] = @() }
            $importers[$key] += $relative
            if ($local.ContainsKey($key)) { continue }
            if ($key -match '^(api|ext)-ms-') { continue }
            if ($key -notmatch $redistributable -and (Test-Path (Join-Path $system32 $dll))) { continue }
            Write-Output "MISSING: $dll (imported by $relative)"
            $failed = $true
        }
    }
    Write-Output "check_zip: the zip carries:"
    foreach ($file in $binaries) {
        $relative = $file.FullName.Substring($root.Length + 1)
        $by = $importers[$file.Name.ToLower()]
        $loaded = if ($by) { "imported by " + (($by | Sort-Object -Unique) -join ', ') } else { 'loaded at run time' }
        Write-Output ("  {0,-45} {1,10:N0} bytes  {2}" -f $relative, $file.Length, $loaded)
    }
    $other = @(Get-ChildItem -Recurse -File $root | Where-Object { $_.Extension -notin '.exe', '.dll' })
    Write-Output "  ...and $($other.Count) other files (data\, ogre\media\, THIRD_PARTY_NOTICES.md)"
    Write-Output "check_zip: from Windows: $((($importers.Keys | Where-Object { -not $local.ContainsKey($_) -and $_ -notmatch '^(api|ext)-ms-' }) | Sort-Object) -join ', ') (and the api-ms-win-* API sets)"
    if ($failed) { throw "check_zip: the zip is incomplete" }
    Write-Output "check_zip: every DLL resolves"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
