# Builds and runs the offline joycore checks (packets, calibration, debounce,
# light controller). Touches no hardware and no Bluetooth.
#
# Uses a g++ on PATH if there is one, otherwise the g++ inside WSL.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$flags = '-std=c++17 -O1 -Wall -Wextra -Werror -Ilib/joycore/src'
$sources = 'lib/joycore/src/*.cpp test/host/test_main.cpp'

if (Get-Command g++ -ErrorAction SilentlyContinue) {
    $out = Join-Path $env:TEMP 'joycore_host_tests.exe'
    Push-Location $root
    try {
        $files = @(Get-ChildItem lib/joycore/src/*.cpp | ForEach-Object FullName) + 'test/host/test_main.cpp'
        & g++ @($flags -split ' ') @files -o $out
        if ($LASTEXITCODE) { throw 'Host test build failed.' }
        & $out
    } finally { Pop-Location }
} elseif (Get-Command wsl -ErrorAction SilentlyContinue) {
    $drive = $root.Substring(0, 1).ToLower()
    $wslRoot = "/mnt/$drive" + $root.Substring(2).Replace('\', '/')
    wsl -- bash -c "cd '$wslRoot' && g++ $flags $sources -o /tmp/joycore_host_tests && /tmp/joycore_host_tests"
} else {
    throw 'No g++ on PATH and no WSL. Install a C++17 compiler to run the host tests.'
}
exit $LASTEXITCODE
