param(
    [Parameter(Mandatory = $true)]
    [string]$VersionString
)

# Remove leading 'v' if present
$cleanVersion = $VersionString -replace '^v', ''

# Extract the numeric part (before any hyphen or other pre-release identifiers)
$numericPart = $cleanVersion -split '[-+]', 2 | Select-Object -First 1

# Split by dots to get major, minor, patch
$parts = $numericPart -split '\.'

if ($parts.Count -lt 3) {
    Write-Error "Version string must be in the format vMAJOR.MINOR.PATCH (e.g., v1.2.3 or 1.2.3-alpha)"
    exit 1
}

$major = $parts[0]
$minor = $parts[1]
$patch = $parts[2]

# Extract pre-release identifier if present
$preRelease = ""
if ($cleanVersion -match '-([A-Za-z0-9\.-]+)') {
    $preRelease = $matches[1]
}

# Compose the version.h content
$content = @"
#pragma once
#define ASSET_COOKER_VER_MAJOR $major
#define ASSET_COOKER_VER_MINOR $minor
#define ASSET_COOKER_VER_PATCH $patch
#define ASSET_COOKER_VER_PRE_RELEASE "$preRelease"
#define ASSET_COOKER_VER_FULL "$cleanVersion"
"@

# Write to version.h
Set-Content -Encoding UTF8 src/Version.h -Value $content

Write-Output "Version.h generated with MAJOR=$major, MINOR=$minor, PATCH=$patch, PRE_RELEASE=$preRelease"