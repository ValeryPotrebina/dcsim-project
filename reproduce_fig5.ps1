# reproduce_fig5.ps1
#
# Sweep: 4 algorithms x 4 flow sizes = 16 simulations on Fat-Tree k=K
# Each combination produces output/k${K}/<alg>_<size>pkt/ with fct.txt + others
# At end, scripts/aggregate.py builds output/k${K}/results.csv
# and scripts/plot.py builds output/k${K}/fig5_reproduction.png
#
# Usage:
#   .\reproduce_fig5.ps1            (default: -K 4, fast)
#   .\reproduce_fig5.ps1 -K 8       (paper-scale, much slower)

param(
    [ValidateSet(4, 8)]
    [int]$K = 4
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

$Algorithms = @("dcsim", "dcpim", "pfabric", "dctcp")
$Sizes      = @(50, 100, 200, 400)
$ConfigsDir = "configs/k$K"
$OutputBase = "output/k$K"
$TmpBase    = "tmp/k$K"

# Sanity: do the configs exist?
if (-not (Test-Path $ConfigsDir)) {
    Write-Host "ERROR: $ConfigsDir not found." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $TmpBase))    { New-Item -ItemType Directory -Path $TmpBase    -Force | Out-Null }
if (-not (Test-Path $OutputBase)) { New-Item -ItemType Directory -Path $OutputBase -Force | Out-Null }

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " Fig. 5(a) reproduction : Fat-Tree k=$K" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host " Configs: $ConfigsDir/"
Write-Host " Output:  $OutputBase/"
Write-Host ""

$Total = $Algorithms.Length * $Sizes.Length
$Done  = 0
$StartTime = Get-Date

foreach ($alg in $Algorithms) {
    foreach ($size in $Sizes) {
        $Done++
        $OutDir     = "$OutputBase/${alg}_${size}pkt"
        $FlowTrace  = "inputs/cdf_${size}pkt.cdf"
        $ConfigPath = "$TmpBase/run_${alg}_${size}pkt.conf"

        Write-Host "[$Done/$Total] $alg @ $size pkt" -ForegroundColor Cyan

        if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

        # Read base config + substitute placeholders.
        # .NET I/O to avoid PowerShell's CRLF mangling that broke the C++ parser.
        $BaseConfig = [System.IO.File]::ReadAllText((Join-Path $ScriptDir "$ConfigsDir/base_$alg.conf"))
        $BaseConfig = $BaseConfig -replace "`r`n", "`n"
        $RunConfig  = $BaseConfig -replace '__FLOW_TRACE__', $FlowTrace -replace '__OUT__', $OutDir
        [System.IO.File]::WriteAllText((Join-Path $ScriptDir $ConfigPath), $RunConfig, [System.Text.UTF8Encoding]::new($false))

        # Forward-slash paths only inside the Docker bash -c string.
        $LogFile = "$OutDir/run.log"
        docker run --rm `
            -v "${ScriptDir}:/workspace" `
            dcsim-env `
            bash -c "cd /workspace && stdbuf -o0 timeout 600 ./simulator 1 $ConfigPath > $LogFile 2>&1" | Out-Null

        $FctPath = Join-Path $OutDir "fct.txt"
        if (Test-Path $FctPath) {
            $LineCount = (Get-Content $FctPath | Measure-Object -Line).Lines
            $LastFct   = (Get-Content $FctPath | Select-Object -Last 1).Split(' ')[1]
            Write-Host "    OK: fct.txt has $LineCount lines; max FCT (last) = $LastFct us" -ForegroundColor Green
        } else {
            Write-Host "    FAIL: no fct.txt produced" -ForegroundColor Red
        }
    }
}

$Duration = (Get-Date) - $StartTime
Write-Host ""
Write-Host "Sweep done in $($Duration.TotalSeconds.ToString('F1'))s. Aggregating..." -ForegroundColor Cyan

# Aggregate to CSV
if (Test-Path "scripts/aggregate.py") {
    python scripts/aggregate.py $OutputBase
} else {
    Write-Host "(scripts/aggregate.py not found - skipping)" -ForegroundColor Yellow
}

# Plot
if (Test-Path "scripts/plot.py") {
    python scripts/plot.py $OutputBase
} else {
    Write-Host "(scripts/plot.py not found - skipping)" -ForegroundColor Yellow
}
