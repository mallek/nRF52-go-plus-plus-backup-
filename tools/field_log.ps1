# Send a field-log command to the XIAO over USB serial and print/save the reply.
#   pwsh -File tools/field_log.ps1                      # dump to console + timestamped file
#   pwsh -File tools/field_log.ps1 -Command clear
#   pwsh -File tools/field_log.ps1 -Port COM8 -OutDir D:\Skunkworks\pgpp-clone\fieldlogs
param(
    [string]$Port = "COM8",
    [ValidateSet("dump", "clear", "flush")]
    [string]$Command = "dump",
    [string]$OutDir = "."
)

$serial = New-Object System.IO.Ports.SerialPort $Port, 115200
$serial.DtrEnable = $true
$serial.ReadTimeout = 500
$serial.NewLine = "`n"
$serial.Open()
try {
    Start-Sleep -Milliseconds 300
    $serial.DiscardInBuffer()
    $serial.WriteLine($Command)

    $lines = New-Object System.Collections.Generic.List[string]
    $deadline = (Get-Date).AddSeconds(15)
    $inDump = $false
    while ((Get-Date) -lt $deadline) {
        try {
            $line = $serial.ReadLine().TrimEnd("`r")
        } catch [System.TimeoutException] {
            continue
        }
        if ($Command -ne "dump") {
            if ($line -like "Feedback log:*") {
                $line
                break
            }
            continue
        }
        if ($line -like "=== FIELD LOG*") {
            $inDump = $true
        }
        if ($inDump) {
            $lines.Add($line)
            $line
        }
        if ($line -like "=== END FIELD LOG*") {
            break
        }
    }

    if ($Command -eq "dump" -and $lines.Count -gt 0) {
        $file = Join-Path $OutDir ("fieldlog-{0:yyyyMMdd-HHmmss}.txt" -f (Get-Date))
        $lines | Set-Content -Path $file
        Write-Host "saved $file"
    }
} finally {
    $serial.Close()
}
