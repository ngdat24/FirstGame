param(
    [ValidateSet('sim', 'tests', 'protocol', 'server', 'play', 'all')]
    [string]$Target = 'sim'
)

$ErrorActionPreference = 'Stop'
$compiler = (Get-Command g++ -ErrorAction Stop).Source
$targets = if ($Target -eq 'all') { @('sim', 'tests', 'protocol', 'server', 'play') } else { @($Target) }
$sources = @{
    sim = @('main.cpp', 'GameLogic.cpp')
    tests = @('tests.cpp', 'GameLogic.cpp')
    protocol = @('tests_protocol.cpp', 'protocol.cpp', 'GameLogic.cpp')
    server = @('server.cpp', 'protocol.cpp', 'GameLogic.cpp')
    play = @('play.cpp', 'protocol.cpp', 'GameLogic.cpp')
}
$outputs = @{ sim = 'sim'; tests = 'tests'; protocol = 'tests_protocol'; server = 'server'; play = 'play' }
foreach ($item in $targets) {
    $outputPath = Join-Path $PSScriptRoot ($outputs[$item] + '.exe')
    $buildArguments = @('-std=c++17', '-Wall', '-Wextra', '-O2')
    $buildArguments += @($sources[$item] | ForEach-Object { Join-Path $PSScriptRoot $_ })
    $buildArguments += @('-o', $outputPath)
    if ($item -eq 'server') { $buildArguments += @('-lws2_32', '-lwsock32') }
    Write-Host "Building $item -> $outputPath"
    & $compiler @buildArguments
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $item. Do not use an older executable." }
}
