# Reads the Stamp-C3's entire 4 MB flash to a file BEFORE anything is uploaded,
# then reports what firmware it contains. Reading does not modify the board
# (the chip is reset into its ROM bootloader and back).
#
#   pwsh tools\backup_flash.ps1 -Port COM5
#   pwsh tools\backup_flash.ps1 -Inspect backups\stamp-c3-....bin   # describe an existing dump
param(
    [string]$Port,
    [string]$Inspect,
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'backups')
)
$ErrorActionPreference = 'Stop'
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
$esptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py'

function Get-Text([byte[]]$bytes, [int]$offset, [int]$length) {
    $raw = [Text.Encoding]::ASCII.GetString($bytes, $offset, $length)
    $end = $raw.IndexOf([char]0)
    if ($end -ge 0) { $raw.Substring(0, $end) } else { $raw }
}

function Show-FlashContents([string]$file) {
    $image = [IO.File]::ReadAllBytes($file)
    $blank = [Text.Encoding]::GetEncoding(28591).GetString($image).Trim([char]0xFF).Length -eq 0
    if ($blank) { Write-Output 'Flash is entirely erased (all 0xFF): there is no firmware to preserve.'; return }

    Write-Output ('Bootloader at 0x0: ' + $(if ($image[0] -eq 0xE9) { 'present' } else { 'no ESP image header' }))
    $apps = 0
    for ($entry = 0x8000; $entry -lt 0x8C00 -and $image[$entry] -eq 0xAA -and $image[$entry + 1] -eq 0x50; $entry += 32) {
        $type = $image[$entry + 2]
        $offset = [BitConverter]::ToUInt32($image, $entry + 4)
        $size = [BitConverter]::ToUInt32($image, $entry + 8)
        $label = Get-Text $image ($entry + 12) 16
        $line = '  partition {0,-10} type={1} offset=0x{2:X6} size=0x{3:X6}' -f $label, $type, $offset, $size
        if ($type -eq 0 -and $offset + 0x120 -le $image.Length) {
            if ($image[$offset] -ne 0xE9) {
                $line += '  [no application image]'
            } elseif ([BitConverter]::ToUInt32($image, $offset + 0x20) -eq [uint32]2882360370) {  # 0xABCD5432
                $apps++
                $d = $offset + 0x20
                $line += '  [APPLICATION: project "{0}" version "{1}" built {2} {3}, {4}]' -f `
                    (Get-Text $image ($d + 48) 32), (Get-Text $image ($d + 16) 32),
                    (Get-Text $image ($d + 96) 16), (Get-Text $image ($d + 80) 16), (Get-Text $image ($d + 112) 32)
            } else {
                $apps++
                $line += '  [APPLICATION image without a description block]'
            }
        }
        Write-Output $line
    }
    if ($apps) {
        Write-Output "This board HAS firmware ($apps application image(s)). The backup file above preserves all of it,"
        Write-Output 'including NVS settings. Keep the file if that firmware matters to you.'
    } else {
        Write-Output 'No application image found through the partition table. The flash is not blank, though:'
        Write-Output 'keep the backup if you are unsure.'
    }
}

if ($Inspect) { Show-FlashContents (Resolve-Path $Inspect).Path; exit }
if (-not $Port) { throw 'Give the serial port, e.g. -Port COM5. `pio device list` shows it.' }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$file = Join-Path $OutDir ('stamp-c3-{0}-4MB.bin' -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

& $python $esptool --chip esp32c3 --port $Port --baud 460800 flash_id
if ($LASTEXITCODE) { throw 'Could not talk to the chip. Check the port, the cable and the CH9102 driver.' }
& $python $esptool --chip esp32c3 --port $Port --baud 460800 read_flash 0 0x400000 $file
if ($LASTEXITCODE) { throw 'Flash read failed. No backup was made; do not upload yet.' }

Write-Output ''
Write-Output "Backup: $file"
Write-Output ('SHA256: ' + (Get-FileHash $file -Algorithm SHA256).Hash)
Show-FlashContents $file
Write-Output ''
Write-Output 'To restore this exact image later:'
Write-Output "  & `"$python`" `"$esptool`" --chip esp32c3 --port $Port --baud 460800 write_flash 0 `"$file`""
