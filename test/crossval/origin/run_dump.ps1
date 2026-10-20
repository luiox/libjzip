param(
    [string]$Strategy = "all",
    [string]$Sample = "all",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$originDir = $scriptDir

# JDK 22 由 JAVA_HOME 或系统已安装的 toolchain 提供；Gradle 从 PATH 查找。
$gradle = Get-Command gradle -ErrorAction SilentlyContinue
if (-not $gradle) {
    Write-Error "gradle not found in PATH (JDK 22 required by the toolchain)"
    exit 1
}
$gradleExe = $gradle.Source

if (-not $SkipBuild) {
    Write-Host "Building ZipDump..."
    & $gradleExe -p "$originDir" compileJava 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed"
        exit 1
    }
}

Write-Host "Running ZipDump (strategy=$Strategy, sample=$Sample)..."
& $gradleExe -p "$originDir" run "-Pstrategy=$Strategy" "-Psample=$Sample" 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Error "Run failed"
    exit 1
}

Write-Host "Golden files written to: $originDir\golden\"
