#Requires -Version 5.1
<#
.SYNOPSIS
  Build / flash / debug helpers for esp32-apple-homekit sketches.

.EXAMPLES
  .\scripts\esp32.ps1 ports
  .\scripts\esp32.ps1 flash photo
  .\scripts\esp32.ps1 flash deco
  .\scripts\esp32.ps1 flash switches
  .\scripts\esp32.ps1 monitor
  .\scripts\esp32.ps1 status
  .\scripts\esp32.ps1 unpair
  .\scripts\esp32.ps1 ping
  .\scripts\esp32.ps1 flash photo -Port COM5
#>
[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [ValidateSet('ports', 'build', 'flash', 'monitor', 'status', 'unpair', 'factory', 'ping', 'help')]
  [string]$Command = 'help',

  [Parameter(Position = 1)]
  [ValidateSet('deco', 'photo', 'switches')]
  [string]$Project = 'deco',

  [string]$Port = '',
  [int]$Baud = 115200,
  [int]$MonitorSeconds = 0
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

$Projects = @{
  deco = @{
    Name     = 'ESP32_HomeKit_DecoLight'
    Sketch   = Join-Path $RepoRoot 'ESP32_HomeKit_DecoLight'
    Fqbn     = 'esp32:esp32:esp32:PartitionScheme=min_spiffs'
    PairCode = '466-37-726'
    Hostname = 'Deco-Light.local'
  }
  photo = @{
    Name     = 'ESP32_HomeKit_PhotoLight'
    Sketch   = Join-Path $RepoRoot 'ESP32_HomeKit_PhotoLight'
    Fqbn     = 'esp32:esp32:esp32:PartitionScheme=min_spiffs'
    PairCode = '466-37-726'
    Hostname = 'Photo-Light.local'
  }
  switches = @{
    Name     = 'ESP32_HomeKit_Switches'
    Sketch   = Join-Path $RepoRoot 'ESP32_HomeKit_Switches'
    Fqbn     = 'esp32:esp32:esp32:PartitionScheme=default'
    PairCode = '111-11-111'
    Hostname = 'Akash-Nagpal-Accessory-1.local'
  }
}

function Write-Info([string]$msg) { Write-Host $msg -ForegroundColor Cyan }
function Write-Ok([string]$msg)   { Write-Host $msg -ForegroundColor Green }
function Write-WarnMsg([string]$msg) { Write-Host $msg -ForegroundColor Yellow }
function Write-ErrMsg([string]$msg)  { Write-Host $msg -ForegroundColor Red }

function Get-ArduinoCliPath {
  $cli = Get-Command arduino-cli -ErrorAction SilentlyContinue
  if ($cli) { return $cli.Source }
  $fallback = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
  if (Test-Path $fallback) {
    $env:Path = "C:\Program Files\Arduino CLI;$env:Path"
    return $fallback
  }
  throw 'arduino-cli not found. Install Arduino CLI or add it to PATH.'
}

function Get-EspPort {
  if ($Port) { return $Port }

  $pnp = Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match 'CP210|CH340|USB.?Serial|UART' }
  foreach ($d in $pnp) {
    if ($d.FriendlyName -match '(COM\d+)') {
      return $Matches[1]
    }
  }

  $lines = @(arduino-cli board list 2>$null)
  foreach ($line in $lines) {
    if ($line -match '(COM\d+).*Serial Port \(USB\)') {
      return $Matches[1]
    }
  }
  foreach ($line in $lines) {
    if ($line -match '^(COM\d+)\s') {
      $c = $Matches[1]
      if ($c -notin @('COM3', 'COM4')) { return $c }
    }
  }

  throw 'No ESP32 serial port found. Plug in the board or pass -Port COM5'
}

function Get-ProjectInfo {
  if (-not $Projects.ContainsKey($Project)) {
    throw "Unknown project '$Project'. Use: deco | photo | switches"
  }
  $info = $Projects[$Project]
  if (-not (Test-Path $info.Sketch)) {
    throw "Sketch folder missing: $($info.Sketch)"
  }
  $secrets = Join-Path $info.Sketch 'secrets.h'
  if (-not (Test-Path $secrets)) {
    Write-WarnMsg "WARNING: secrets.h missing in $($info.Name). Copy secrets.example.h to secrets.h"
  }
  return $info
}

function Invoke-SerialCommand {
  param(
    [string]$ComPort,
    [string]$Payload,
    [int]$ReadSeconds = 4
  )

  $pyFile = Join-Path $env:TEMP 'esp32_serial_cmd.py'
  @'
import serial, time, re, sys
port = sys.argv[1]
baud = int(sys.argv[2])
payload = sys.argv[3]
read_s = float(sys.argv[4])
ser = serial.Serial()
ser.port = port
ser.baudrate = baud
ser.timeout = 0.25
ser.dtr = False
ser.rts = False
ser.open()
time.sleep(0.4)
ser.reset_input_buffer()
if payload:
    ser.write((payload + "\n").encode("ascii"))
    time.sleep(0.2)
end = time.time() + read_s
buf = b""
while time.time() < end:
    chunk = ser.read(ser.in_waiting or 1)
    if chunk:
        buf += chunk
ser.close()
text = buf.decode("utf-8", errors="replace")
text = re.sub(r"[^\x09\x0A\x0D\x20-\x7E]", "?", text)
sys.stdout.write(text)
'@ | Set-Content -Path $pyFile -Encoding ASCII

  python $pyFile $ComPort $Baud $Payload $ReadSeconds
}

function Show-Help {
  Write-Host @"
esp32-apple-homekit helper

Usage:
  .\scripts\esp32.ps1 <command> [project] [-Port COMx]

Commands:
  ports              List serial ports
  build  [deco]      Compile sketch (default: deco)
  flash  [deco]      Compile + upload
  monitor            Serial monitor (Ctrl+C to stop)
  status             HomeSpan 's' status (pairing / clients / IP)
  unpair             HomeSpan 'H' - new Accessory ID, clear pairing
  factory            HomeSpan 'F' - full factory reset
  ping               Ping ESP IP / open status page check
  help               This text

Projects:
  deco       ESP32_HomeKit_DecoLight    (HomeSpan, pair 466-37-726)
  photo      ESP32_HomeKit_PhotoLight   (HomeSpan, pair 466-37-726)
  switches   ESP32_HomeKit_Switches     (ESPHap, pair 111-11-111)

Examples:
  .\scripts\esp32.ps1 flash photo
  .\scripts\esp32.ps1 flash deco
  .\scripts\esp32.ps1 status
  .\scripts\esp32.ps1 monitor -Port COM5
"@
}

switch ($Command) {
  'help' {
    Show-Help
  }

  'ports' {
    Get-ArduinoCliPath | Out-Null
    Write-Info 'arduino-cli board list:'
    arduino-cli board list
    Write-Info ''
    Write-Info 'PnP serial devices:'
    Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue |
      Select-Object FriendlyName | Format-Table -AutoSize
  }

  'build' {
    $cli = Get-ArduinoCliPath
    $info = Get-ProjectInfo
    try { $com = Get-EspPort } catch { $com = '(none)' }
    Write-Info "Building $($info.Name)"
    Write-Host "  FQBN: $($info.Fqbn)"
    Write-Host "  Port: $com"
    & $cli compile --fqbn $info.Fqbn --warnings none $info.Sketch
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
    Write-Ok 'Build OK'
  }

  'flash' {
    $cli = Get-ArduinoCliPath
    $info = Get-ProjectInfo
    $com = Get-EspPort
    Write-Info "Building + flashing $($info.Name) -> $com"
    Write-Host "  Pairing code: $($info.PairCode)"
    & $cli compile --fqbn $info.Fqbn --warnings none $info.Sketch
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
    & $cli upload -p $com --fqbn $info.Fqbn $info.Sketch
    if ($LASTEXITCODE -ne 0) { throw "Upload failed ($LASTEXITCODE)" }
    Write-Ok "Flash OK on $com"
    Write-Host "  Pair in Home app with: $($info.PairCode)"
    Write-Host "  Hostname: $($info.Hostname)"
  }

  'monitor' {
    $cli = Get-ArduinoCliPath
    $com = Get-EspPort
    Write-Info "Serial monitor $com @ $Baud (Ctrl+C to stop)"
    if ($MonitorSeconds -gt 0) {
      Invoke-SerialCommand -ComPort $com -Payload '' -ReadSeconds $MonitorSeconds
    } else {
      & $cli monitor -p $com -c "baudrate=$Baud"
    }
  }

  'status' {
    Get-ArduinoCliPath | Out-Null
    $com = Get-EspPort
    Write-Info "Requesting HomeSpan status on $com ..."
    $out = Invoke-SerialCommand -ComPort $com -Payload 's' -ReadSeconds 5
    Write-Host $out
    if ($out -match 'No Client Connections') {
      Write-WarnMsg ''
      Write-WarnMsg "Paired but no HomeKit session -> Home app will show 'No Response'."
      Write-WarnMsg 'Check same WiFi / disable AP isolation. Test: http://192.168.0.100/status'
    }
    if ($out -match 'Client #\d+.*verified' -or $out -match 'HS_CONNECTED') {
      Write-Ok ''
      Write-Ok 'HomeKit client looks connected.'
    }
  }

  'unpair' {
    $com = Get-EspPort
    Write-WarnMsg "Erasing HomeKit Device ID + pairing (HomeSpan H) on $com ..."
    Invoke-SerialCommand -ComPort $com -Payload 'H' -ReadSeconds 6 | Out-Host
    Write-Ok "Done. Device should reboot unpaired. Pair code: $($Projects.deco.PairCode)"
  }

  'factory' {
    $com = Get-EspPort
    Write-WarnMsg "FACTORY RESET (HomeSpan F) on $com - clears pairing + WiFi NVS ..."
    Invoke-SerialCommand -ComPort $com -Payload 'F' -ReadSeconds 6 | Out-Host
    Write-Ok 'Done. Device rebooting.'
  }

  'ping' {
    $hosts = @('192.168.0.100', 'Deco-Light.local')
    foreach ($h in $hosts) {
      Write-Info "Pinging $h ..."
      ping -n 2 $h
    }
    Write-Info 'HTTP status page:'
    try {
      $r = Invoke-WebRequest -Uri 'http://192.168.0.100/status' -TimeoutSec 4 -UseBasicParsing
      Write-Ok "HTTP $($r.StatusCode) OK - ESP is reachable from this PC"
    } catch {
      Write-ErrMsg "HTTP failed: $($_.Exception.Message)"
      Write-WarnMsg 'If ping/HTTP fail, HomeKit will show No Response (router isolation / wrong network).'
    }
  }
}
