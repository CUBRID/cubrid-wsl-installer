# Convert license.txt to license.rtf
# This script creates a properly formatted RTF file from a plain text license file

# File paths
$inputFile = Join-Path $PSScriptRoot "license.txt"
$outputFile = Join-Path $PSScriptRoot "license.rtf"

Write-Host "Converting license.txt to RTF..."
Write-Host "Input:  $inputFile"
Write-Host "Output: $outputFile"

# Check if input file exists
if (-not (Test-Path $inputFile)) {
    Write-Error "Input file not found: $inputFile"
    exit 1
}

# Read input text file
$text = Get-Content -Path $inputFile -Raw -Encoding UTF8

# RTF Header with proper formatting for license documents
$rtf = New-Object System.Text.StringBuilder
[void]$rtf.Append("{\rtf1\ansi\ansicpg1252\deff0\nouicompat\deflang1033")

# Font table - using Courier New for monospace, Arial for headers
[void]$rtf.Append("{\fonttbl{\f0\fnil\fcharset0 Courier New;}{\f1\fnil\fcharset0 Arial;}}")

# Color table
[void]$rtf.Append("{\colortbl ;\red0\green0\blue0;}")

# Document formatting: viewkind4, paragraph spacing, line spacing
[void]$rtf.Append("\viewkind4\uc1")
[void]$rtf.Append("\pard\sa200\sl276\slmult1")
[void]$rtf.Append("\f0\fs20\lang1033 ")

# Escape RTF special characters
$text = $text -replace '\\', '\\\\'     # Backslash must be escaped first
$text = $text -replace '\{', '\{'       # Opening brace
$text = $text -replace '\}', '\}'       # Closing brace

# Convert line breaks to RTF paragraph breaks
$text = $text -replace '\r\n', '\par '  # Windows line endings
$text = $text -replace '\n', '\par '    # Unix line endings
$text = $text -replace '\t', '\tab '    # Tabs

# Add text content
[void]$rtf.Append($text)

# RTF Footer
[void]$rtf.Append("\par }")

# Write to file as ASCII (RTF standard encoding)
try {
    [System.IO.File]::WriteAllText($outputFile, $rtf.ToString(), [System.Text.Encoding]::ASCII)
    Write-Host "Successfully converted to RTF format"
    Write-Host "File size: $((Get-Item $outputFile).Length) bytes"
} catch {
    Write-Error "Failed to write output file: $_"
    exit 1
}

