param(
    [string]$OutputRoot = (Join-Path $PSScriptRoot "..\assets\items")
)

Add-Type -AssemblyName System.Drawing

function New-Canvas([int]$Size) {
    $bitmap = [System.Drawing.Bitmap]::new(
        $Size,
        $Size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.Clear([System.Drawing.Color]::Transparent)
    return @($bitmap, $graphics)
}

function New-PointArray([object[]]$Coordinates) {
    $points = [System.Drawing.Point[]]::new($Coordinates.Count)
    for ($i = 0; $i -lt $Coordinates.Count; $i++) {
        $points[$i] = [System.Drawing.Point]::new($Coordinates[$i][0], $Coordinates[$i][1])
    }
    return $points
}

function Draw-Potion($g) {
    $outline = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 73, 51, 57), 3)
    $glass = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 220, 91, 108))
    $shine = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(190, 255, 190, 196))
    $cork = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 164, 112, 65))
    $body = New-PointArray @(@(22, 25), @(42, 25), @(48, 35), @(44, 53), @(20, 53), @(16, 35))
    $g.FillPolygon($glass, $body)
    $g.DrawPolygon($outline, $body)
    $g.FillRectangle($glass, 25, 15, 14, 12)
    $g.DrawRectangle($outline, 25, 15, 14, 12)
    $g.FillRectangle($cork, 24, 10, 16, 7)
    $g.DrawRectangle($outline, 24, 10, 16, 7)
    $g.FillEllipse($shine, 23, 31, 7, 14)
    $outline.Dispose(); $glass.Dispose(); $shine.Dispose(); $cork.Dispose()
}

function Draw-Herb($g) {
    $stem = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 53, 95, 49), 3)
    $leafDark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 72, 139, 70))
    $leafLight = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 106, 181, 91))
    $outline = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 41, 79, 42), 2)
    $g.DrawLine($stem, 31, 52, 32, 18)
    $leaves = @(
        @((New-PointArray @(@(31, 41), @(14, 34), @(12, 22), @(27, 26))), $leafDark),
        @((New-PointArray @(@(33, 37), @(50, 29), @(52, 18), @(37, 22))), $leafLight),
        @((New-PointArray @(@(31, 29), @(20, 20), @(24, 11), @(34, 20))), $leafLight),
        @((New-PointArray @(@(32, 48), @(45, 43), @(51, 48), @(39, 54))), $leafDark)
    )
    foreach ($entry in $leaves) {
        $g.FillPolygon($entry[1], $entry[0])
        $g.DrawPolygon($outline, $entry[0])
    }
    $stem.Dispose(); $leafDark.Dispose(); $leafLight.Dispose(); $outline.Dispose()
}

function Draw-Wood($g) {
    $outline = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 82, 53, 35), 3)
    $wood = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 163, 105, 62))
    $cut = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 213, 159, 98))
    $ring = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 129, 82, 49), 2)
    $g.FillRectangle($wood, 13, 20, 35, 14); $g.DrawRectangle($outline, 13, 20, 35, 14)
    $g.FillEllipse($cut, 41, 20, 14, 14); $g.DrawEllipse($outline, 41, 20, 14, 14); $g.DrawEllipse($ring, 45, 24, 6, 6)
    $g.FillRectangle($wood, 9, 35, 38, 14); $g.DrawRectangle($outline, 9, 35, 38, 14)
    $g.FillEllipse($cut, 40, 35, 14, 14); $g.DrawEllipse($outline, 40, 35, 14, 14); $g.DrawEllipse($ring, 44, 39, 6, 6)
    $outline.Dispose(); $wood.Dispose(); $cut.Dispose(); $ring.Dispose()
}

function Draw-Ore($g) {
    $outline = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 61, 69, 73), 3)
    $rock = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 123, 139, 147))
    $light = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 174, 188, 193))
    $metal = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 93, 107, 113))
    $body = New-PointArray @(@(11, 42), @(17, 24), @(29, 13), @(46, 18), @(54, 35), @(43, 51), @(23, 53))
    $g.FillPolygon($rock, $body); $g.DrawPolygon($outline, $body)
    $g.FillPolygon($light, (New-PointArray @(@(20, 27), @(29, 18), @(37, 22), @(30, 32))))
    $g.FillPolygon($metal, (New-PointArray @(@(34, 37), @(46, 27), @(49, 37), @(41, 45))))
    $outline.Dispose(); $rock.Dispose(); $light.Dispose(); $metal.Dispose()
}

function Draw-Coin($g) {
    $outline = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 117, 78, 21), 3)
    $gold = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 229, 184, 54))
    $light = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 255, 226, 111))
    $detail = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 174, 124, 27), 3)
    $g.FillEllipse($gold, 12, 12, 40, 40); $g.DrawEllipse($outline, 12, 12, 40, 40)
    $g.DrawEllipse($detail, 18, 18, 28, 28)
    $g.FillEllipse($light, 20, 18, 9, 14)
    $g.DrawLine($detail, 32, 23, 32, 41); $g.DrawLine($detail, 27, 28, 37, 28); $g.DrawLine($detail, 27, 36, 37, 36)
    $outline.Dispose(); $gold.Dispose(); $light.Dispose(); $detail.Dispose()
}

function Save-ItemTextures([string]$Id, [string]$Kind) {
    $directory = Join-Path $OutputRoot $Id
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    $canvas = New-Canvas 64
    $icon = $canvas[0]
    $graphics = $canvas[1]
    switch ($Kind) {
        "potion" { Draw-Potion $graphics }
        "herb" { Draw-Herb $graphics }
        "wood" { Draw-Wood $graphics }
        "ore" { Draw-Ore $graphics }
        "coin" { Draw-Coin $graphics }
    }
    $icon.Save((Join-Path $directory "icon.png"), [System.Drawing.Imaging.ImageFormat]::Png)

    $worldCanvas = New-Canvas 48
    $world = $worldCanvas[0]
    $worldGraphics = $worldCanvas[1]
    $shadow = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(80, 20, 24, 23))
    $worldGraphics.FillEllipse($shadow, 8, 35, 32, 8)
    $worldGraphics.DrawImage($icon, [System.Drawing.Rectangle]::new(4, 0, 40, 40))
    $world.Save((Join-Path $directory "world.png"), [System.Drawing.Imaging.ImageFormat]::Png)

    $shadow.Dispose()
    $worldGraphics.Dispose(); $world.Dispose()
    $graphics.Dispose(); $icon.Dispose()
}

Save-ItemTextures "small_potion" "potion"
Save-ItemTextures "healing_herb" "herb"
Save-ItemTextures "wood" "wood"
Save-ItemTextures "iron_ore" "ore"
Save-ItemTextures "gold_coin" "coin"

Write-Host "Generated item textures in $([System.IO.Path]::GetFullPath($OutputRoot))"
