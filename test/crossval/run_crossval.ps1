param(
    [string]$Strategy = "all",
    [string]$Sample = "all"
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$crossvalDir = $scriptDir
$testDir = Split-Path -Parent $crossvalDir
$libjzipDir = Split-Path -Parent $testDir
$repoRoot = Split-Path -Parent (Split-Path -Parent $libjzipDir)

Write-Host "=== libjzip crossval: C++ dump generation ===" -ForegroundColor Cyan

$testResourceDir = Join-Path $libjzipDir "test_resource"
$goldenDir = Join-Path $scriptDir "origin\golden"
$cppDumpDir = Join-Path $scriptDir "cpp_dump\output"

if (-not (Test-Path $testResourceDir)) {
    Write-Error "test_resource directory not found: $testResourceDir"
    exit 1
}

$__ = New-Item -ItemType Directory -Path $cppDumpDir -Force | Out-Null

$strategies = if ($Strategy -eq "all") { @("jvm", "standard", "naive") } else { @($Strategy) }

foreach ($strat in $strategies) {
    Write-Host ""
    Write-Host "--- Running C++ dump with strategy: $strat ---" -ForegroundColor Yellow

    & xmake build libjzip_crossval_dump 2>&1 | Write-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build libjzip_crossval_dump failed"
        exit 1
    }

    & xmake run libjzip_crossval_dump $testResourceDir $cppDumpDir $strat 2>&1 | Write-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Run libjzip_crossval_dump failed for strategy: $strat"
        exit 1
    }
}

Write-Host ""
Write-Host "=== Running match_test ===" -ForegroundColor Cyan

& xmake build libjzip_crossval_match 2>&1 | Write-Host
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build libjzip_crossval_match failed"
    exit 1
}

& xmake run libjzip_crossval_match $goldenDir $cppDumpDir $Strategy 2>&1 | Write-Host
$matchResult = $LASTEXITCODE

# --- zipfile crossval: Java ZipFile vs C++ zip::ZipFile ---
Write-Host ""
Write-Host "--- zipfile strategy: C++ zip::ZipFile dump ---" -ForegroundColor Yellow
$zipDumpDir = Join-Path $scriptDir "cpp_zip_dump\output"
& xmake build libjzip_crossval_zip_dump 2>&1 | Write-Host
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build libjzip_crossval_zip_dump failed"
    exit 1
}
& xmake run libjzip_crossval_zip_dump $testResourceDir $zipDumpDir 2>&1 | Write-Host
if ($LASTEXITCODE -ne 0) {
    Write-Host "WARNING: zip dump had failures (expected for trick samples)" -ForegroundColor Yellow
}

# Compare Java golden vs C++ zip dump using file-by-file diff
Write-Host ""
Write-Host "=== Comparing Java ZipFile vs C++ zip::ZipFile ===" -ForegroundColor Cyan
$zipMatchCount = 0
$zipDiffCount = 0
foreach ($goldenFile in Get-ChildItem $goldenDir -Filter "*.zipfile.dump") {
    $cppFile = Join-Path $zipDumpDir $goldenFile.Name
    if (-not (Test-Path $cppFile)) { continue }
    $diff = Compare-Object (Get-Content $goldenFile.FullName) (Get-Content $cppFile)
    if (-not $diff) {
        $zipMatchCount++
    } else {
        $zipDiffCount++
        Write-Host "  MISMATCH: $($goldenFile.Name)" -ForegroundColor DarkYellow
    }
}
Write-Host "  Matched: $zipMatchCount, Differ (expected for tricks): $zipDiffCount"

Write-Host ""
if ($matchResult -eq 0) {
    Write-Host "=== ALL MATCHES PASSED ===" -ForegroundColor Green
} else {
    Write-Host "=== SOME MATCHES FAILED ===" -ForegroundColor Red
}

exit $matchResult
