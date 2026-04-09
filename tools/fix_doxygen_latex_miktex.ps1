$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Resolve-Path (Join-Path $root "..")
$latexDir = Join-Path $proj "docs\latex"
$tabu = Join-Path $latexDir "tabu_doxygen.sty"
$doxygenSty = Join-Path $latexDir "doxygen.sty"
$refman = Join-Path $latexDir "refman.tex"

if (-not (Test-Path $tabu)) {
  Write-Host "Fichier introuvable: $tabu"
  Write-Host "Lance d'abord: doxygen Doxyfile"
  exit 1
}

$content = Get-Content -Raw -LiteralPath $tabu
$old = "\RequirePackage{array}[2008/09/09]"
$new = "\RequirePackage{array}"

if ($content -notmatch [regex]::Escape($old)) {
  Write-Host "Aucune modification nécessaire (pattern non trouvé)."
  exit 0
}

$fixed = $content.Replace($old, $new)
Set-Content -LiteralPath $tabu -Value $fixed -NoNewline
Write-Host "OK: patch appliqué sur docs\latex\tabu_doxygen.sty"

if (Test-Path $doxygenSty) {
  $sty = Get-Content -Raw -LiteralPath $doxygenSty

  # Remplacement ciblé de DoxyEnumFields : longtabu* -> longtable (MiKTeX/LaTeX récents)
  $needle = '\newenvironment{DoxyEnumFields}[2][]{%'
  if ($sty -match [regex]::Escape($needle) -and $sty -match 'begin\{longtabu\*\}') {
    $oldBlock = @'
\newenvironment{DoxyEnumFields}[2][]{%
    \tabulinesep=1mm%
    \par%
    \ifthenelse{\equal{#1}{2}}%
      {\begin{longtabu*}spread 0pt [l]{|X[-1,r]|X[-1,l]|}}%
      {\begin{longtabu*}spread 0pt [l]{|X[-1,l]|X[-1,r]|X[-1,l]|}}% with init value
    \multicolumn{2}{l}{\hspace{-6pt}\bfseries\fontseries{bc}\selectfont\color{darkgray} #2}\\[1ex]%
    \hline%
    \endfirsthead%
    \multicolumn{2}{l}{\hspace{-6pt}\bfseries\fontseries{bc}\selectfont\color{darkgray} #2}\\[1ex]%
    \hline%
    \endhead%
}{%
    \end{longtabu*}%
    \vspace{6pt}%
}
'@

    $newBlock = @'
\newenvironment{DoxyEnumFields}[2][]{%
    % NOTE (MiKTeX / LaTeX récents) :
    % longtabu* (tabu) est fragile et peut générer des erreurs du type
    %   Undefined control sequence ... \insert@pcolumn
    % puis une cascade d'erreurs d'alignement.
    % On force ici une implémentation robuste basée sur longtable.
    \par%
    \ifthenelse{\equal{#1}{2}}%
      {%
        \begin{longtable}{|p{0.30\linewidth}|p{0.64\linewidth}|}%
        \multicolumn{2}{l}{\hspace{-6pt}\bfseries\fontseries{bc}\selectfont\color{darkgray} #2}\\[1ex]%
        \hline%
        \endfirsthead%
        \multicolumn{2}{l}{\hspace{-6pt}\bfseries\fontseries{bc}\selectfont\color{darkgray} #2}\\[1ex]%
        \hline%
        \endhead%
      }%
      {%
        \begin{longtable}{|p{0.24\linewidth}|p{0.18\linewidth}|p{0.52\linewidth}|}%
        \multicolumn{3}{l}{\hspace{-6pt}\bfseries\fontseries{bc}\selectfont\color{darkgray} #2}\\[1ex]%
        \hline%
        \endfirsthead%
        \multicolumn{3}{l}{\hspace{-6pt}\bfseries\fontseries{bc}\selectfont\color{darkgray} #2}\\[1ex]%
        \hline%
        \endhead%
      }%
}{%
    \end{longtable}%
    \vspace{6pt}%
}
'@

    if ($sty -match [regex]::Escape($oldBlock)) {
      $sty2 = $sty.Replace($oldBlock, $newBlock)
      Set-Content -LiteralPath $doxygenSty -Value $sty2 -NoNewline
      Write-Host "OK: patch appliqué sur docs\latex\doxygen.sty (DoxyEnumFields -> longtable)"
    } else {
      Write-Host "Attention: pattern DoxyEnumFields inattendu, patch non appliqué."
    }
  }
}

if (Test-Path $refman) {
  $tex = Get-Content -Raw -LiteralPath $refman
  if ($tex -match "\\usepackage\{newunicodechar\}" -and $tex -notmatch "U\+2500") {
    $insertAfter = "\doxynewunicodechar{³}{`${}^{3}$}% Superscript three"
    $mapping = @"
  % Box drawing characters (ASCII-art diagrams / trees) that may appear in generated docs.
  % Map them to simple TeX constructs to keep pdfLaTeX happy.
  \doxynewunicodechar{─}{\rule[0.55ex]{1.6em}{0.12ex}}% U+2500
  \doxynewunicodechar{│}{\textbar}% U+2502
  \doxynewunicodechar{┌}{+}% U+250C
  \doxynewunicodechar{┐}{+}% U+2510
  \doxynewunicodechar{└}{+}% U+2514
  \doxynewunicodechar{┘}{+}% U+2518
  \doxynewunicodechar{├}{+}% U+251C
  \doxynewunicodechar{┤}{+}% U+2524
  \doxynewunicodechar{┬}{+}% U+252C
  \doxynewunicodechar{┴}{+}% U+2534
  \doxynewunicodechar{┼}{+}% U+253C
"@
    if ($tex -match [regex]::Escape($insertAfter)) {
      $tex2 = $tex.Replace($insertAfter, $insertAfter + "`r`n" + $mapping.TrimEnd())
      Set-Content -LiteralPath $refman -Value $tex2 -NoNewline
      Write-Host "OK: patch appliqué sur docs\latex\refman.tex (mapping Unicode box drawing)"
    } else {
      Write-Host "Attention: point d'insertion newunicodechar inattendu, mapping non appliqué."
    }
  }
}

