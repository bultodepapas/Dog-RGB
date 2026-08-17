[CmdletBinding()]
param(
    [string]$Image = "public.ecr.aws/supabase/postgres:17.6.1.158",
    [ValidateRange(1024, 65535)]
    [int]$Port = 56432,
    [switch]$KeepContainer
)

$ErrorActionPreference = "Stop"
$containerName = "dogrgb-phase0-capacity"
$created = $false
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$sqlPath = Join-Path $scriptDirectory "benchmark.sql"

if (-not (Test-Path -LiteralPath $sqlPath)) {
    throw "Missing benchmark SQL: $sqlPath"
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "Docker is required."
}

$collision = docker ps -a --filter "name=^/${containerName}$" --format "{{.ID}}"
if ($LASTEXITCODE -ne 0) {
    throw "Unable to query Docker."
}
if ($collision) {
    throw "Refusing to reuse or remove existing container '$containerName' ($collision)."
}

try {
    $containerId = docker run -d --name $containerName `
        -e POSTGRES_PASSWORD=phase0-local-only `
        -p "127.0.0.1:${Port}:5432" `
        $Image
    if ($LASTEXITCODE -ne 0 -or -not $containerId) {
        throw "Failed to start the capacity container."
    }
    $created = $true

    $ready = $false
    for ($attempt = 0; $attempt -lt 45; $attempt++) {
        docker exec $containerName pg_isready -U postgres 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $ready = $true
            break
        }
        Start-Sleep -Seconds 1
    }
    if (-not $ready) {
        docker logs --tail 100 $containerName
        throw "PostgreSQL did not become ready."
    }

    # pg_isready turns true before the Supabase image has finished installing
    # bundled migrations and performing its initialization restart. Wait until
    # the entrypoint announces completion, then require consecutive healthy
    # checks so DDL cannot race that restart boundary.
    $imageReady = $false
    for ($attempt = 0; $attempt -lt 120; $attempt++) {
        # `docker logs` correctly writes container stderr to the native stderr
        # stream. Under `$ErrorActionPreference = "Stop"`, PowerShell can turn
        # harmless initdb warnings from that stream into terminating records.
        # Capture both streams while still checking Docker's actual exit code.
        $previousErrorAction = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            $containerLogs = docker logs $containerName 2>&1 | Out-String
            $logsExitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $previousErrorAction
        }
        if ($logsExitCode -ne 0) {
            throw "Unable to read PostgreSQL initialization logs."
        }
        if ($containerLogs -match "PostgreSQL init process complete; ready for start up") {
            $imageReady = $true
            break
        }
        Start-Sleep -Seconds 1
    }
    if (-not $imageReady) {
        docker logs --tail 100 $containerName
        throw "Supabase PostgreSQL image initialization did not finish."
    }

    $stableChecks = 0
    for ($attempt = 0; $attempt -lt 30 -and $stableChecks -lt 3; $attempt++) {
        docker exec $containerName pg_isready -U postgres 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $stableChecks++
        }
        else {
            $stableChecks = 0
        }
        Start-Sleep -Seconds 2
    }
    if ($stableChecks -lt 3) {
        docker logs --tail 100 $containerName
        throw "Supabase PostgreSQL image did not remain stable after initialization."
    }

    Get-Content -Raw -LiteralPath $sqlPath |
        docker exec -i $containerName psql -X -v ON_ERROR_STOP=1 -U postgres -d postgres
    if ($LASTEXITCODE -ne 0) {
        throw "Capacity benchmark failed."
    }
}
finally {
    if ($created -and -not $KeepContainer) {
        docker rm -f $containerName | Out-Null
    }
}
