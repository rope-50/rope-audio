# Generates simple sine-tone WAV files under assets/ for testing playback.
#   powershell -ExecutionPolicy Bypass -File tools/gen_test_wavs.ps1
#
# Produces a mono tone and a stereo chord so the engine's mono+stereo mixing
# path can be exercised:
#   play_wav assets/tone_a4_mono.wav assets/chord_stereo.wav

param([string]$OutDir = "$PSScriptRoot/../assets")

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Write-Wav {
    param([string]$Path,[int]$Rate,[int]$Channels,[double[]]$Freqs,[double]$Seconds,[double]$Amp)
    $n = [int]($Rate * $Seconds)
    $blockAlign = $Channels * 2          # 16-bit PCM
    $dataSize   = $n * $blockAlign
    $fs  = [System.IO.File]::Open($Path,[System.IO.FileMode]::Create)
    $bw  = New-Object System.IO.BinaryWriter($fs)
    $enc = [System.Text.Encoding]::ASCII
    $bw.Write($enc.GetBytes("RIFF")); $bw.Write([int](36 + $dataSize)); $bw.Write($enc.GetBytes("WAVE"))
    $bw.Write($enc.GetBytes("fmt ")); $bw.Write([int]16); $bw.Write([int16]1); $bw.Write([int16]$Channels)
    $bw.Write([int]$Rate); $bw.Write([int]($Rate * $blockAlign)); $bw.Write([int16]$blockAlign); $bw.Write([int16]16)
    $bw.Write($enc.GetBytes("data")); $bw.Write([int]$dataSize)
    $twoPi = [Math]::PI * 2
    for ($i = 0; $i -lt $n; $i++) {
        $env = [Math]::Min(1.0, [Math]::Min($i, $n - $i) / ($Rate * 0.01))  # 10ms fades
        for ($c = 0; $c -lt $Channels; $c++) {
            $f = $Freqs[$c % $Freqs.Length]
            $s = [Math]::Sin($twoPi * $f * $i / $Rate) * $Amp * $env
            $bw.Write([int16]([Math]::Round($s * 32767)))
        }
    }
    $bw.Flush(); $bw.Close(); $fs.Close()
    Write-Output ("  {0}  ({1} Hz, {2} ch, {3:N1}s)" -f $Path, $Rate, $Channels, $Seconds)
}

Write-Output "Generating test WAVs:"
Write-Wav -Path "$OutDir/tone_a4_mono.wav" -Rate 48000 -Channels 1 -Freqs @(440.0)         -Seconds 3 -Amp 0.30
Write-Wav -Path "$OutDir/chord_stereo.wav" -Rate 48000 -Channels 2 -Freqs @(329.63, 392.0) -Seconds 3 -Amp 0.25
