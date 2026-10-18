# Once a second while the game runs: the size and write time of the program cache file
# (%LOCALAPPDATA%\TorchlightRecomp\ogre\microcode-*.bin) and of programs.txt, to a CSV. A row whose
# .bin write time differs from the previous row's is marked "rewritten". Waits up to 60 s for the
# game's process, stops when it exits.
param([Parameter(Mandatory)][string]$Out)
$cache = Join-Path $env:LOCALAPPDATA 'TorchlightRecomp\ogre'
$deadline = (Get-Date).AddSeconds(60)
$p = $null
while (-not $p -and (Get-Date) -lt $deadline) {
    $p = Get-Process torchlight -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { Start-Sleep -Milliseconds 200 }
}
'time,bin,bin_bytes,bin_written,programs_txt_bytes,programs_txt_written,event' | Out-File $Out -Encoding ascii
if (-not $p) { 'no game process' | Out-File $Out -Append -Encoding ascii; exit 1 }
$last = $null
while (-not $p.HasExited) {
    $t0 = Get-Date
    $bin = Get-ChildItem $cache -Filter 'microcode-*.bin' -ErrorAction SilentlyContinue | Select-Object -First 1
    $txt = Get-Item (Join-Path $cache 'programs.txt') -ErrorAction SilentlyContinue
    $bw = if ($bin) { $bin.LastWriteTime.ToString('HH:mm:ss.fff') } else { '' }
    $event = if ($null -eq $last) { 'first' } elseif ($bw -ne $last) { 'rewritten' } else { '' }
    $last = $bw
    '{0},{1},{2},{3},{4},{5},{6}' -f $t0.ToString('HH:mm:ss.fff'), $(if ($bin) { $bin.Name } else { 'none' }),
        $(if ($bin) { $bin.Length } else { '' }), $bw, $(if ($txt) { $txt.Length } else { '' }),
        $(if ($txt) { $txt.LastWriteTime.ToString('HH:mm:ss.fff') } else { '' }), $event |
        Out-File $Out -Append -Encoding ascii
    $wait = 1000 - ((Get-Date) - $t0).TotalMilliseconds
    if ($wait -gt 0) { Start-Sleep -Milliseconds $wait }
}
$bin = Get-ChildItem $cache -Filter 'microcode-*.bin' -ErrorAction SilentlyContinue | Select-Object -First 1
$bw = if ($bin) { $bin.LastWriteTime.ToString('HH:mm:ss.fff') } else { '' }
'{0},{1},{2},{3},,,exit{4}' -f (Get-Date).ToString('HH:mm:ss.fff'), $(if ($bin) { $bin.Name } else { 'none' }),
    $(if ($bin) { $bin.Length } else { '' }), $bw, $(if ($bw -ne $last) { ' rewritten' } else { '' }) |
    Out-File $Out -Append -Encoding ascii
