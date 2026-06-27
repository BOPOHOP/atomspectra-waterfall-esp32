# build_demo_pages.ps1 — copy the firmware Web UI pages verbatim into demo/,
# inject demo-shim.js as the first <head> script, and rewrite the absolute
# nav/links so they resolve under GitHub Pages /demo/. The page bodies are
# otherwise byte-for-byte the firmware UI (no UI reimplementation).
#
# Run:  pwsh -File scripts/build_demo_pages.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$web  = Join-Path $root "web"
$demo = Join-Path $root "demo"
$pages = "index.html","waterfall.html","saved.html","system.html"
$enc = New-Object System.Text.UTF8Encoding($false)
foreach ($p in $pages) {
  $src = Join-Path $web $p
  $dst = Join-Path $demo $p
  $html = [System.IO.File]::ReadAllText($src, $enc)
  # 1) inject shim as the first thing in <head> (runs before page scripts)
  $html = [regex]::Replace($html, "<head([^>]*)>", '<head$1>' + "`n" + '<script src="demo-shim.js"></script>', 1)
  # 2) rewrite absolute nav links to demo-relative files
  $html = $html.Replace('href="/"',          'href="index.html"')
  $html = $html.Replace('href="/waterfall"', 'href="waterfall.html"')
  $html = $html.Replace('href="/saved"',     'href="saved.html"')
  $html = $html.Replace('href="/system"',    'href="system.html"')
  # 3) saved.html overlay jump back to spectrum page
  $html = $html.Replace('location.href="/?overlay="', 'location.href="index.html?overlay="')
  [System.IO.File]::WriteAllText($dst, $html, $enc)
  Write-Output ("wrote: {0} ({1} bytes)" -f $dst, (Get-Item $dst).Length)
}