[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$requiredVulkanSdkVersion = '1.4.363.0'
$vulkanSdkRoot = $env:VULKAN_SDK
if ([string]::IsNullOrWhiteSpace($vulkanSdkRoot)) {
    $vulkanSdkRoot = "C:\VulkanSDK\$requiredVulkanSdkVersion"
}

if (-not (Test-Path -LiteralPath $vulkanSdkRoot -PathType Container)) {
    throw "Vulkan SDK $requiredVulkanSdkVersion was not found at '$vulkanSdkRoot'. Set VULKAN_SDK to that SDK installation."
}

$vulkanSdkDirectory = Get-Item -LiteralPath $vulkanSdkRoot
if ($vulkanSdkDirectory.Name -ne $requiredVulkanSdkVersion) {
    throw "Expected Vulkan SDK $requiredVulkanSdkVersion, but VULKAN_SDK points to '$vulkanSdkRoot'."
}

$glslcPath = Join-Path $vulkanSdkRoot 'Bin\glslc.exe'
$vulkanIncludeDirectory = Join-Path $vulkanSdkRoot 'Include'
$vulkanLibraryDirectory = Join-Path $vulkanSdkRoot 'Lib'
$vulkanHeader = Join-Path $vulkanIncludeDirectory 'vulkan\vulkan.h'
$vulkanImportLibrary = Join-Path $vulkanLibraryDirectory 'vulkan-1.lib'

foreach ($requiredFile in @($glslcPath, $vulkanHeader, $vulkanImportLibrary)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required Vulkan SDK file is missing: '$requiredFile'."
    }
}

$glslcVersionOutput = @(& $glslcPath '--version' 2>&1)
$glslcVersionExitCode = $LASTEXITCODE
if ($glslcVersionExitCode -ne 0) {
    throw "Vulkan SDK glslc version check failed with exit code $glslcVersionExitCode."
}
$glslcVersionText = ($glslcVersionOutput | ForEach-Object { "$_" }) -join ' '
if ([string]::IsNullOrWhiteSpace($glslcVersionText)) {
    throw 'Vulkan SDK glslc did not report its version.'
}
Write-Host "Vulkan SDK: $requiredVulkanSdkVersion ($vulkanSdkRoot)"
Write-Host "glslc: $glslcVersionText"

if ($env:VSCMD_ARG_HOST_ARCH -ne 'x64' -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    throw 'Run this script from an x64 Visual Studio Developer environment (host x64, target x64).'
}
if ([string]::IsNullOrWhiteSpace($env:VCToolsInstallDir) -or
    [string]::IsNullOrWhiteSpace($env:VCToolsVersion)) {
    throw 'The x64 MSVC environment is not initialized. Start an x64 Visual Studio Developer PowerShell and retry.'
}
if ([string]::IsNullOrWhiteSpace($env:WindowsSdkDir) -or
    [string]::IsNullOrWhiteSpace($env:WindowsSDKVersion)) {
    throw 'The Windows SDK environment is not initialized. Start an x64 Visual Studio Developer PowerShell and retry.'
}

$windowsSdkVersion = $env:WindowsSDKVersion.TrimEnd([char]'\')
$msvcBinDirectory = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64'
$expectedClPath = Join-Path $msvcBinDirectory 'cl.exe'
$expectedLinkPath = Join-Path $msvcBinDirectory 'link.exe'
$expectedRcPath = Join-Path $env:WindowsSdkDir "bin\$windowsSdkVersion\x64\rc.exe"
$windowsSdkInclude = Join-Path $env:WindowsSdkDir "Include\$windowsSdkVersion\um\Windows.h"
$windowsSdkKernelLibrary = Join-Path $env:WindowsSdkDir "Lib\$windowsSdkVersion\um\x64\kernel32.lib"

function Assert-ToolPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedPath
    )

    if (-not (Test-Path -LiteralPath $ExpectedPath -PathType Leaf)) {
        throw "Expected $Name is missing from the active development environment: '$ExpectedPath'."
    }

    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw "$Name is not on PATH. Start an x64 Visual Studio Developer PowerShell and retry."
    }

    $actualPath = [System.IO.Path]::GetFullPath($command.Source)
    $expectedFullPath = [System.IO.Path]::GetFullPath($ExpectedPath)
    if (-not [string]::Equals(
        $actualPath,
        $expectedFullPath,
        [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Name resolved to '$actualPath', not the expected development-environment tool '$expectedFullPath'."
    }

    return $actualPath
}

$clPath = Assert-ToolPath -Name 'cl.exe' -ExpectedPath $expectedClPath
$linkPath = Assert-ToolPath -Name 'link.exe' -ExpectedPath $expectedLinkPath
$rcPath = Assert-ToolPath -Name 'rc.exe' -ExpectedPath $expectedRcPath

foreach ($requiredFile in @($windowsSdkInclude, $windowsSdkKernelLibrary)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "The active Windows SDK is incomplete or unavailable: '$requiredFile'."
    }
}

$clVersion = (Get-Item -LiteralPath $clPath).VersionInfo.FileVersion
$linkVersion = (Get-Item -LiteralPath $linkPath).VersionInfo.FileVersion
$rcVersion = (Get-Item -LiteralPath $rcPath).VersionInfo.FileVersion
Write-Host "MSVC: $($env:VCToolsVersion), cl.exe $clVersion, link.exe $linkVersion ($msvcBinDirectory)"
Write-Host "Windows SDK: $windowsSdkVersion, rc.exe $rcVersion ($expectedRcPath)"

$projectRoot = $PSScriptRoot
$outputDirectory = Join-Path $projectRoot 'build\Release\x64'
$shaderSources = @(
    'vulkan_circle.vert',
    'vulkan_circle.frag',
    'vulkan_canvas.vert',
    'vulkan_canvas.frag'
)
$requiredSources = @(
    'main.cpp',
    'vulkan_renderer.cpp',
    'vulkan_renderer.h',
    'resource.h',
    'vulkan_shaders.rc'
) + $shaderSources

foreach ($sourceName in $requiredSources) {
    $sourcePath = Join-Path $projectRoot $sourceName
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Required project input is missing: '$sourcePath'."
    }
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$executablePath = Join-Path $outputDirectory 'multi_benchmark.exe'
$generatedOutputs = @(
    (Join-Path $outputDirectory 'vulkan_circle.vert.spv'),
    (Join-Path $outputDirectory 'vulkan_circle.frag.spv'),
    (Join-Path $outputDirectory 'vulkan_canvas.vert.spv'),
    (Join-Path $outputDirectory 'vulkan_canvas.frag.spv'),
    (Join-Path $outputDirectory 'vulkan_shaders.res'),
    (Join-Path $outputDirectory 'main.obj'),
    (Join-Path $outputDirectory 'vulkan_renderer.obj'),
    $executablePath
)

foreach ($generatedOutput in $generatedOutputs) {
    if (Test-Path -LiteralPath $generatedOutput -PathType Leaf) {
        Remove-Item -LiteralPath $generatedOutput -Force
    }
}

function Invoke-CheckedTool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string[]]$ToolArgs,
        [Parameter(Mandatory = $true)]
        [string]$Operation
    )

    & $Path @ToolArgs
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$Operation failed with exit code $exitCode."
    }
}

foreach ($shaderSource in $shaderSources) {
    $sourcePath = Join-Path $projectRoot $shaderSource
    $spirvPath = Join-Path $outputDirectory "$shaderSource.spv"
    Invoke-CheckedTool -Path $glslcPath `
        -ToolArgs @($sourcePath, '-o', $spirvPath) `
        -Operation "glslc.exe compiling $shaderSource"
}

Push-Location $outputDirectory
try {
    Invoke-CheckedTool -Path $rcPath `
        -ToolArgs @('/I', $projectRoot, '/fo', (Join-Path $outputDirectory 'vulkan_shaders.res'), (Join-Path $projectRoot 'vulkan_shaders.rc')) `
        -Operation 'rc.exe compiling vulkan_shaders.rc'
}
finally {
    Pop-Location
}

$compileOptions = @(
    '/nologo',
    '/std:c++17',
    '/EHsc',
    '/O2',
    '/DNDEBUG',
    '/MD',
    '/DUNICODE',
    '/D_UNICODE',
    "/I$projectRoot",
    "/I$vulkanIncludeDirectory",
    '/c'
)

Invoke-CheckedTool -Path $clPath `
    -ToolArgs ($compileOptions + @("/Fo$(Join-Path $outputDirectory 'main.obj')", (Join-Path $projectRoot 'main.cpp'))) `
    -Operation 'cl.exe compiling main.cpp'

Invoke-CheckedTool -Path $clPath `
    -ToolArgs ($compileOptions + @("/Fo$(Join-Path $outputDirectory 'vulkan_renderer.obj')", (Join-Path $projectRoot 'vulkan_renderer.cpp'))) `
    -Operation 'cl.exe compiling vulkan_renderer.cpp'

Invoke-CheckedTool -Path $linkPath `
    -ToolArgs @(
        '/MACHINE:X64',
        '/SUBSYSTEM:WINDOWS',
        "/OUT:$executablePath",
        (Join-Path $outputDirectory 'main.obj'),
        (Join-Path $outputDirectory 'vulkan_renderer.obj'),
        (Join-Path $outputDirectory 'vulkan_shaders.res'),
        "/LIBPATH:$vulkanLibraryDirectory",
        'vulkan-1.lib'
    ) `
    -Operation 'link.exe linking the Vulkan canvas executable'

foreach ($generatedOutput in $generatedOutputs) {
    if (-not (Test-Path -LiteralPath $generatedOutput -PathType Leaf)) {
        throw "Build output was not generated: '$generatedOutput'."
    }
    if ((Get-Item -LiteralPath $generatedOutput).Length -le 0) {
        throw "Build output is empty: '$generatedOutput'."
    }
}

Write-Host "Release x64 build succeeded: $executablePath"
