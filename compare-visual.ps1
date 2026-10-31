param(
    [Parameter(Mandatory=$true)][string]$Legacy,
    [Parameter(Mandatory=$true)][string]$Shader,
    [string]$Difference = "visual-difference.bmp"
)

Add-Type -AssemblyName System.Drawing
$a = [System.Drawing.Bitmap]::new($Legacy)
$b = [System.Drawing.Bitmap]::new($Shader)
if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
    throw "Image dimensions differ: $($a.Width)x$($a.Height) vs $($b.Width)x$($b.Height)"
}
$diff = [System.Drawing.Bitmap]::new($a.Width, $a.Height,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$different = 0
$maxChannel = 0
$sum = [double]0
$channels = $a.Width * $a.Height * 4
for ($y = 0; $y -lt $a.Height; ++$y) {
    for ($x = 0; $x -lt $a.Width; ++$x) {
        $pa = $a.GetPixel($x, $y)
        $pb = $b.GetPixel($x, $y)
        $dr = [math]::Abs([int]$pa.R - [int]$pb.R)
        $dg = [math]::Abs([int]$pa.G - [int]$pb.G)
        $db = [math]::Abs([int]$pa.B - [int]$pb.B)
        $da = [math]::Abs([int]$pa.A - [int]$pb.A)
        $maxChannel = [math]::Max($maxChannel, [math]::Max([math]::Max($dr, $dg), [math]::Max($db, $da)))
        $sum += $dr + $dg + $db + $da
        if ($dr -ne 0 -or $dg -ne 0 -or $db -ne 0 -or $da -ne 0) { ++$different }
        $intensity = [math]::Min(255, 4 * [math]::Max([math]::Max($dr, $dg), [math]::Max($db, $da)))
        $diff.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, $intensity, 0, 0))
    }
}
$diff.Save($Difference, [System.Drawing.Imaging.ImageFormat]::Bmp)
$pixels = $a.Width * $a.Height
[pscustomobject]@{
    Width = $a.Width
    Height = $a.Height
    DifferentPixels = $different
    DifferentPercent = 100.0 * $different / $pixels
    MaxAbsoluteChannelDifference = $maxChannel
    MeanAbsoluteChannelDifference = $sum / $channels
    DifferenceImage = (Resolve-Path $Difference).Path
}
$a.Dispose(); $b.Dispose(); $diff.Dispose()
