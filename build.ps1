param([string]$Compiler = "$PSScriptRoot\.tools\llvm-mingw-20261020-ucrt-x86_64\bin\clang++.exe")
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $Compiler)) { throw "Compiler not found: $Compiler" }
New-Item -ItemType Directory -Force -Path "$PSScriptRoot\build","$PSScriptRoot\dist" | Out-Null
Push-Location $PSScriptRoot
try {
    $toolDirectory = Split-Path -Parent $Compiler
    & "$toolDirectory\llvm-windres.exe" app.rc -O coff -o build/app.res
    if ($LASTEXITCODE) { throw 'Resource compilation failed' }
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static -municode -mwindows main.cpp build/app.res -luser32 -lgdi32 '-Wl,--dynamicbase' '-Wl,--nxcompat' -o dist/MouseLens.exe
    if ($LASTEXITCODE) { throw 'Compilation failed' }
    & $Compiler -std=c++17 -O2 -Wall -Wextra -static tests/geometry_test.cpp -o build/geometry_test.exe
    if ($LASTEXITCODE) { throw 'Test compilation failed' }
    & .\build\geometry_test.exe
    if ($LASTEXITCODE) { throw 'Geometry tests failed' }
    & $Compiler -std=c++17 -O2 -Wall -Wextra -static tests/color_test.cpp -o build/color_test.exe
    if ($LASTEXITCODE) { throw 'Color test compilation failed' }
    & .\build\color_test.exe
    if ($LASTEXITCODE) { throw 'Color tests failed' }
    Get-Item dist/MouseLens.exe | Select-Object FullName,Length
} finally { Pop-Location }
