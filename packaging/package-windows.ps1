param(
    [string]$BuildDir = "",
    [string]$PlatformTools = "",
    [string]$RuntimeDir = "",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $projectRoot "build-windows" }
if (-not $PlatformTools) { $PlatformTools = $env:FASTBOOT_ENHANCE_PLATFORM_TOOLS }
if (-not $RuntimeDir) { $RuntimeDir = $env:FASTBOOT_ENHANCE_RUNTIME_DIR }
if (-not $Output) { $Output = Join-Path $projectRoot "dist\FastbootEnhance-Windows-x86_64.exe" }

$binaryCandidates = @(
    (Join-Path $BuildDir "FastbootEnhance.exe"),
    (Join-Path $BuildDir "Release\FastbootEnhance.exe")
)
$binary = $binaryCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$stage = Join-Path $projectRoot "dist\windows-stage"
$archive = Join-Path $projectRoot "dist\FastbootEnhance-Windows-x86_64.7z"
$configPath = Join-Path $projectRoot "dist\FastbootEnhance-Windows-sfx.txt"

if (-not $binary) {
    throw "Build output not found. Checked: $($binaryCandidates -join ', ')"
}
if (-not $PlatformTools -or -not (Test-Path (Join-Path $PlatformTools "fastboot.exe"))
    -or -not (Test-Path (Join-Path $PlatformTools "adb.exe"))) {
    throw "Set FASTBOOT_ENHANCE_PLATFORM_TOOLS to a directory containing fastboot.exe and adb.exe."
}

$windeployqt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if (-not $windeployqt) {
    throw "windeployqt.exe was not found in PATH."
}
$sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
if (-not $sevenZip) {
    throw "7z.exe was not found in PATH."
}
$sfx = $env:FASTBOOT_ENHANCE_7Z_SFX
if (-not $sfx) { $sfx = Join-Path (Split-Path $sevenZip.Source) "7z.sfx" }
if (-not (Test-Path $sfx)) {
    throw "7z.sfx was not found. Set FASTBOOT_ENHANCE_7Z_SFX to the SFX module path."
}
if (-not $RuntimeDir) {
    throw "Set FASTBOOT_ENHANCE_RUNTIME_DIR to the directory containing non-Qt runtime DLLs."
}
if (-not (Test-Path $RuntimeDir)) {
    throw "Runtime directory not found: $RuntimeDir"
}

New-Item -ItemType Directory -Force -Path (Split-Path $Output) | Out-Null
Remove-Item $stage, $archive, $configPath, $Output -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage, (Join-Path $stage "tools") | Out-Null

& $windeployqt.Source --release --compiler-runtime --no-translations --dir $stage $binary
Copy-Item (Join-Path $PlatformTools "*") -Destination (Join-Path $stage "tools") -Recurse -Force
Copy-Item (Join-Path $RuntimeDir "*.dll") -Destination $stage -Force
Copy-Item (Join-Path $projectRoot "LICENSE") -Destination $stage -Force
if (Test-Path (Join-Path $PlatformTools "NOTICE.txt")) {
    Copy-Item (Join-Path $PlatformTools "NOTICE.txt") -Destination (Join-Path $stage "Android-Platform-Tools-NOTICE.txt") -Force
}

& $sevenZip.Source a -t7z -mx=9 $archive (Join-Path $stage "*") | Out-Host

$sfxConfig = @"
;!@Install@!UTF-8!
Title="Fastboot Enhance"
RunProgram="FastbootEnhance.exe"
;!@InstallEnd@!
"@
[System.IO.File]::WriteAllText($configPath, $sfxConfig, [System.Text.UTF8Encoding]::new($false))

$outputStream = [System.IO.File]::Open($Output, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
try {
    foreach ($part in @($sfx, $configPath, $archive)) {
        $inputStream = [System.IO.File]::OpenRead($part)
        try { $inputStream.CopyTo($outputStream) } finally { $inputStream.Dispose() }
    }
} finally {
    $outputStream.Dispose()
}

Remove-Item $stage, $archive, $configPath -Recurse -Force -ErrorAction SilentlyContinue
Write-Output "Created $Output"
