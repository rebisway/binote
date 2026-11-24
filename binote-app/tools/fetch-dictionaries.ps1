# Binote - www.rebisway.com - contact@rebisway.com
# Copyright (C) 2026 Thomas Gourgnier
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Downloads the Hunspell dictionaries of the languages Binote supports (LibreOffice dictionaries, github.com/LibreOffice/dictionaries) into the
# folder given by -Output (the "dict" folder beside binote.exe), with their licenses in "licenses". They are not stored in the repository.
# The files which are already there are not downloaded again. Binote reads UTF-8 words, so the dictionaries in ISO 8859 are converted to UTF-8.
# A failure (no network) is a warning, never an error: the build goes on, with the dictionaries which are present (French is in the repository).
#
#   powershell -ExecutionPolicy Bypass -File fetch-dictionaries.ps1 -Output ..\bin\Release\dict

param([Parameter(Mandatory = $true)][string]$Output)

$base = 'https://raw.githubusercontent.com/LibreOffice/dictionaries/master'

# Folder of the repository, name of the files, name given here, encoding of the files
$dictionaries = @(
    @('en', 'en_US', 'en_US', 'utf-8'), @('en', 'en_GB', 'en_GB', 'utf-8'),
    @('es', 'es_ES', 'es_ES', 'utf-8'), @('es', 'es_AR', 'es_AR', 'utf-8'),
    @('de', 'de_DE_frami', 'de_DE', 'iso-8859-1'),
    @('it_IT', 'it_IT', 'it_IT', 'utf-8'),
    @('pt_BR', 'pt_BR', 'pt_BR', 'utf-8'), @('pt_PT', 'pt_PT', 'pt_PT', 'utf-8'),
    @('pl_PL', 'pl_PL', 'pl_PL', 'iso-8859-2'),
    @('cs_CZ', 'cs_CZ', 'cs_CZ', 'utf-8'), @('ru_RU', 'ru_RU', 'ru_RU', 'utf-8')
)

# Folder, file, name of the copy
$licenses = @(
    @('en', 'README_en_US.txt', 'en_US-README.txt'), @('en', 'README_en_GB.txt', 'en_GB-README.txt'), @('en', 'license.txt', 'en-LICENSE.txt'),
    @('es', 'README_hunspell_es.txt', 'es-README.txt'), @('es', 'LICENSE.md', 'es-LICENSE.md'),
    @('de', 'README_de_DE_frami.txt', 'de_DE-README.txt'), @('de', 'COPYING_GPLv2', 'de_DE-COPYING_GPLv2.txt'), @('de', 'COPYING_GPLv3', 'de_DE-COPYING_GPLv3.txt'),
    @('it_IT', 'README_it_IT.txt', 'it_IT-README.txt'),
    @('pt_BR', 'README_pt_BR.txt', 'pt_BR-README.txt'),
    @('pt_PT', 'README_pt_PT.txt', 'pt_PT-README.txt'), @('pt_PT', 'LICENSES.txt', 'pt_PT-LICENSES.txt'),
    @('pl_PL', 'README_pl_PL.txt', 'pl_PL-README.txt'),
    @('cs_CZ', 'README_en.txt', 'cs_CZ-README.txt'),
    @('ru_RU', 'README_ru_RU.txt', 'ru_RU-README.txt')
)

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$client = New-Object Net.WebClient
$utf8 = New-Object Text.UTF8Encoding($false)

function Get-Text([string]$folder, [string]$file, [string]$encoding)
{
    $bytes = $client.DownloadData("$base/$folder/$file")
    $text = [Text.Encoding]::GetEncoding($encoding).GetString($bytes)
    return $text.TrimStart([char]0xFEFF)
}

try
{
    New-Item -ItemType Directory -Force -Path $Output | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $Output 'licenses') | Out-Null
}
catch
{
    Write-Warning "Dictionaries: cannot create $Output : $_"
    exit 0
}

$done = 0
foreach ($d in $dictionaries)
{
    $folder, $source, $name, $encoding = $d
    $dic = Join-Path $Output "$name.dic"
    $aff = Join-Path $Output "$name.aff"
    if ((Test-Path $dic) -and (Test-Path $aff))
    {
        continue
    }
    try
    {
        $affText = Get-Text $folder "$source.aff" $encoding
        $dicText = Get-Text $folder "$source.dic" $encoding
        # The dictionary is now UTF-8
        if ($affText -match '(?m)^SET\s+\S+')
        {
            $affText = [regex]::Replace($affText, '(?m)^SET\s+\S+', 'SET UTF-8', 1)
        }
        else
        {
            $affText = "SET UTF-8`n" + $affText
        }
        [IO.File]::WriteAllText($aff, $affText, $utf8)
        [IO.File]::WriteAllText($dic, $dicText, $utf8)
        $done++
        Write-Host "Dictionary $name downloaded"
    }
    catch
    {
        Write-Warning "Dictionary $name : $_"
        Remove-Item -ErrorAction SilentlyContinue $dic, $aff
    }
}

foreach ($l in $licenses)
{
    $folder, $file, $copy = $l
    $target = Join-Path (Join-Path $Output 'licenses') $copy
    if (Test-Path $target)
    {
        continue
    }
    try
    {
        $client.DownloadFile("$base/$folder/$file", $target)
    }
    catch
    {
        Write-Warning "License $copy : $_"
        Remove-Item -ErrorAction SilentlyContinue $target
    }
}

if ($done -gt 0)
{
    Write-Host "$done dictionaries downloaded into $Output"
}
exit 0
