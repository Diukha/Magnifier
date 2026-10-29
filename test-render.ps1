param([switch]$Benchmark, [string]$Compiler = "$PSScriptRoot\.tools\llvm-mingw-20261020-ucrt-x86_64\bin\clang++.exe")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path "$PSScriptRoot\build" | Out-Null
Push-Location $PSScriptRoot
try {
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static tests/render_test.cpp -luser32 -lgdi32 -ldwmapi -ld3d11 -ldxgi -ldcomp -ld3dcompiler -lruntimeobject -o build/render_test.exe
    if ($LASTEXITCODE) { throw 'Render test compilation failed' }
    & .\build\render_test.exe
    if ($LASTEXITCODE) { throw 'GPU render test failed' }
    if ($Benchmark) {
        & $Compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static tests/render_benchmark.cpp -luser32 -lgdi32 -ld3d11 -ldxgi -ldcomp -ld3dcompiler -lruntimeobject -o build/render_benchmark.exe
        if ($LASTEXITCODE) { throw 'Benchmark compilation failed' }
        & .\build\render_benchmark.exe
        if ($LASTEXITCODE) { throw 'GPU render benchmark failed' }
    }
} finally { Pop-Location }
