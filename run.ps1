#Requires -Version 5.1
<#
.SYNOPSIS
    Builds (if needed) and runs the cef-egl-demo Docker container, then
    automatically opens the noVNC viewer in the default Windows browser.

.PARAMETER NoBuild
    Skip the `docker build` step (use when the image is already up to date).

.PARAMETER ImageName
    Docker image tag to build/run. Default: cef-egl-demo

.PARAMETER ContainerName
    Name given to the running container. Default: cef-egl-demo

.PARAMETER VncPort
    Host port mapped to raw VNC (5900 inside the container). Default: 5900

.PARAMETER NoVncPort
    Host port mapped to the noVNC web viewer (6080 inside). Default: 6080

.EXAMPLE
    .\run.ps1                   # build + run, open browser automatically
    .\run.ps1 -NoBuild          # skip build, just run + open browser
    .\run.ps1 -NoVncPort 8080   # serve noVNC on host port 8080 instead
#>
param(
    [switch] $NoBuild,
    [string] $ImageName     = "cef-egl-demo",
    [string] $ContainerName = "cef-egl-demo",
    [int]    $VncPort       = 5900,
    [int]    $NoVncPort     = 6080
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# 1. Optional build
# ---------------------------------------------------------------------------
if (-not $NoBuild) {
    Write-Host "[run] Building Docker image '$ImageName'..." -ForegroundColor Cyan
    docker build -t $ImageName .
    if ($LASTEXITCODE -ne 0) { Write-Error "docker build failed"; exit 1 }
}

# ---------------------------------------------------------------------------
# 2. Remove any leftover container with the same name
# ---------------------------------------------------------------------------
$existing = docker ps -aq --filter "name=^${ContainerName}$" 2>$null
if ($existing) {
    Write-Host "[run] Removing existing container '$ContainerName'..." -ForegroundColor Yellow
    docker rm -f $ContainerName | Out-Null
}

# ---------------------------------------------------------------------------
# 3. Start container in the background
# ---------------------------------------------------------------------------
Write-Host "[run] Starting container '$ContainerName'..." -ForegroundColor Cyan
docker run --rm --detach `
    --name $ContainerName `
    -p "${VncPort}:5900" `
    -p "${NoVncPort}:6080" `
    $ImageName | Out-Null

if ($LASTEXITCODE -ne 0) { Write-Error "docker run failed"; exit 1 }

# ---------------------------------------------------------------------------
# 4. Wait until noVNC HTTP is actually accepting connections (max 30 s)
# ---------------------------------------------------------------------------
$url        = "http://localhost:${NoVncPort}/vnc.html"
$viewerUrl  = "${url}?autoconnect=true&resize=scale"
$deadline   = (Get-Date).AddSeconds(30)
$ready      = $false

Write-Host "[run] Waiting for noVNC on port $NoVncPort..." -ForegroundColor Cyan
while ((Get-Date) -lt $deadline) {
    try {
        $resp = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 1 -ErrorAction Stop
        if ($resp.StatusCode -eq 200) { $ready = $true; break }
    } catch { }
    Start-Sleep -Milliseconds 500
}

if (-not $ready) {
    Write-Warning "noVNC did not become ready within 30 s. Opening browser anyway."
}

# ---------------------------------------------------------------------------
# 5. Open the viewer in the default browser
# ---------------------------------------------------------------------------
Write-Host "[run] Opening browser: $viewerUrl" -ForegroundColor Green
Start-Process $viewerUrl

# ---------------------------------------------------------------------------
# 6. Tail the container so Ctrl-C stops it cleanly
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "Container is running. Press Ctrl-C or close this window to stop it." -ForegroundColor Yellow
Write-Host "  Raw VNC : localhost:$VncPort"
Write-Host "  noVNC   : $viewerUrl"
Write-Host ""

try {
    docker logs -f $ContainerName
} finally {
    Write-Host "[run] Stopping container '$ContainerName'..." -ForegroundColor Yellow
    docker stop $ContainerName 2>$null | Out-Null
}
