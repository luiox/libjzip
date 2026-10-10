param(
    [string]$Strategy = "all",
    [string]$Sample = "all",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$originDir = $scriptDir
$repoRoot = Resolve-Path (Join-Path $scriptDir "../../../../..")

$javaHome = "C:\Users\Canrad\.jdks\corretto-22.0.2"
$env:JAVA_HOME = $javaHome

$gradlew = Join-Path $repoRoot "gradlew.bat"

if (-not $SkipBuild) {
    Write-Host "Building ZipDump..."
    & $gradlew -p "$originDir" compileJava 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed"
        exit 1
    }
}

Write-Host "Running ZipDump (strategy=$Strategy, sample=$Sample)..."
& $gradlew -p "$originDir" run "-Pstrategy=$Strategy" "-Psample=$Sample" 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Error "Run failed"
    exit 1
}

Write-Host "Golden files written to: $originDir\golden\"
