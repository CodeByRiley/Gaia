$ErrorActionPreference = 'Stop'

$client = [Net.Sockets.TcpClient]::new('127.0.0.1', 9595)

try {
    $stream = $client.GetStream()
    Write-Host 'Connected to Gaia'

    while ($true) {
        $line = Read-Host 'Send'

        if ($line -eq 'exit') {
            break
        }

        $bytes = [Text.Encoding]::UTF8.GetBytes("$line`n")
		$stream.Flush()
        $stream.Write($bytes, 0, $bytes.Length)
    }
}
finally {
    $client.Dispose()
}