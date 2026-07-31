[CmdletBinding()]
param(
  [ValidateSet('prepare', 'lint', 'test', 'suite')]
  [string]$Action = 'prepare',

  [string]$Scenario = 'wokwi/boot.test.yaml',

  [ValidateSet('auto', 'full', 'gnss')]
  [string]$CaptureProfile = 'auto',

  [ValidateRange(5000, 300000)]
  [int]$TimeoutMs = 60000
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Import-DotEnv {
  param([string]$Path)

  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    return
  }
  foreach ($rawLine in Get-Content -LiteralPath $Path) {
    if ($rawLine -notmatch '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)\s*$') {
      continue
    }
    $name = $matches[1]
    $value = $matches[2].Trim()
    if ($value.Length -ge 2 -and
        (($value.StartsWith('"') -and $value.EndsWith('"')) -or
         ($value.StartsWith("'") -and $value.EndsWith("'")))) {
      $value = $value.Substring(1, $value.Length - 2)
    }
    if (-not [Environment]::GetEnvironmentVariable($name, 'Process')) {
      Set-Item -LiteralPath "Env:$name" -Value $value
    }
  }
}

function Resolve-Executable {
  param(
    [string]$CommandName,
    [string[]]$Candidates,
    [string]$InstallHint
  )

  $command = Get-Command $CommandName -ErrorAction SilentlyContinue
  if ($null -ne $command) {
    return $command.Source
  }
  foreach ($candidate in $Candidates) {
    if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }
  throw "$CommandName was not found. $InstallHint"
}

function Invoke-Native {
  param(
    [string]$Executable,
    [string[]]$Arguments,
    [string]$Description
  )

  Write-Host "==> $Description"
  & $Executable @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Description failed with exit code $LASTEXITCODE."
  }
}

$pio = Resolve-Executable `
  -CommandName 'pio' `
  -Candidates @((Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe')) `
  -InstallHint 'Install PlatformIO Core: https://docs.platformio.org/en/latest/core/installation/'

$wokwiCli = Resolve-Executable `
  -CommandName 'wokwi-cli' `
  -Candidates @(
    (Join-Path $env:USERPROFILE '.wokwi\bin\wokwi-cli.exe'),
    (Join-Path $env:USERPROFILE '.wokwi\bin\wokwi-cli')
  ) `
  -InstallHint 'Install the official CLI: https://docs.wokwi.com/wokwi-ci/cli-usage'

$python = Resolve-Executable `
  -CommandName 'python' `
  -Candidates @() `
  -InstallHint 'Install Python 3 and make the python command available.'

Import-DotEnv -Path (Join-Path $projectRoot '.env')

function Invoke-Lint {
  Invoke-Native -Executable $wokwiCli -Arguments @('lint', '.') `
    -Description 'Lint Wokwi diagram'
}

function Invoke-Prepare {
  if (-not (Test-Path -LiteralPath 'artifacts')) {
    New-Item -ItemType Directory -Path 'artifacts' | Out-Null
  }
  Invoke-Native -Executable $wokwiCli `
    -Arguments @('chip', 'compile', 'chips/nmea-gps.chip.c', '-o', 'chips/nmea-gps.chip.wasm') `
    -Description 'Compile NMEA custom chip'
  Invoke-Native -Executable $pio -Arguments @('run', '-e', 'wokwi') `
    -Description 'Build Wokwi firmware'
  Invoke-Lint
}

function Invoke-Scenario {
  param(
    [string]$ScenarioPath,
    [int]$ScenarioTimeoutMs,
    [string]$RequestedCaptureProfile
  )

  if (-not (Test-Path -LiteralPath $ScenarioPath -PathType Leaf)) {
    throw "Scenario not found: $ScenarioPath"
  }
  $scenarioName = [System.IO.Path]::GetFileNameWithoutExtension($ScenarioPath)
  $serialLog = Join-Path 'artifacts' "$scenarioName.serial.log"
  $vcdLog = Join-Path 'artifacts' "$scenarioName.vcd"
  $analysisLog = Join-Path 'artifacts' "$scenarioName.analysis.json"
  $diagramFile = Join-Path 'artifacts' "$scenarioName.diagram.json"
  $effectiveCaptureProfile = $RequestedCaptureProfile
  if ($effectiveCaptureProfile -eq 'auto') {
    $effectiveCaptureProfile = if ($scenarioName -eq 'boot.test') { 'full' } else { 'gnss' }
  }
  Invoke-Native -Executable $python `
    -Arguments @(
      'tools/wokwi_diagram.py', '--profile', $effectiveCaptureProfile,
      '--input', 'diagram.json', '--output', $diagramFile
    ) `
    -Description "Generate $effectiveCaptureProfile instrumentation diagram"
  Invoke-Native -Executable $wokwiCli `
    -Arguments @(
      '.', '--scenario', $ScenarioPath,
      '--timeout', "$ScenarioTimeoutMs", '--timeout-exit-code', '1',
      '--serial-log-file', $serialLog, '--vcd-file', $vcdLog,
      '--diagram-file', $diagramFile
    ) `
    -Description "Run Wokwi scenario $ScenarioPath"
  Invoke-Native -Executable $python `
    -Arguments @(
      'tools/analyze_wokwi.py', '--serial', $serialLog, '--vcd', $vcdLog,
      '--output', $analysisLog, '--capture-profile', $effectiveCaptureProfile
    ) `
    -Description "Analyze Wokwi logs for $ScenarioPath"
  Write-Host "Artifacts: $serialLog, $vcdLog, $analysisLog"
}

Push-Location $projectRoot
try {
  switch ($Action) {
    'prepare' {
      Invoke-Prepare
    }
    'lint' {
      Invoke-Lint
    }
    'test' {
      if (-not $env:WOKWI_CLI_TOKEN) {
        throw 'WOKWI_CLI_TOKEN is not set. Create a personal token at https://wokwi.com/dashboard/ci and set it only in your environment.'
      }
      Invoke-Prepare
      if (-not (Test-Path -LiteralPath 'artifacts')) {
        New-Item -ItemType Directory -Path 'artifacts' | Out-Null
      }
      Invoke-Scenario -ScenarioPath $Scenario -ScenarioTimeoutMs $TimeoutMs `
        -RequestedCaptureProfile $CaptureProfile
    }
    'suite' {
      if (-not $env:WOKWI_CLI_TOKEN) {
        throw 'WOKWI_CLI_TOKEN is not set. Create a personal token at https://wokwi.com/dashboard/ci and set it only in your environment.'
      }
      Invoke-Prepare
      $scenarios = @(Get-ChildItem -LiteralPath 'wokwi' -Filter '*.test.yaml' -File | Sort-Object Name)
      if ($scenarios.Count -eq 0) {
        throw 'No Wokwi scenarios found in wokwi/*.test.yaml.'
      }
      foreach ($scenarioFile in $scenarios) {
        Invoke-Scenario -ScenarioPath $scenarioFile.FullName -ScenarioTimeoutMs $TimeoutMs `
          -RequestedCaptureProfile $CaptureProfile
      }
      Write-Host "Wokwi suite passed: $($scenarios.Count)/$($scenarios.Count) scenarios"
    }
  }
} finally {
  Pop-Location
}
