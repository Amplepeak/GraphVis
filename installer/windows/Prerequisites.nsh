!ifndef GRAPHVIS_PREREQUISITES_NSH
!define GRAPHVIS_PREREQUISITES_NSH

Function CheckGraphviz
  ; Graphviz is not required by GraphVis 18.4 today. This function is ready for
  ; a future feature. Enable at compile time with /DGRAPHVIS_REQUIRE_GRAPHVIZ.
  nsExec::ExecToStack 'where.exe dot.exe'
  Pop $0
  Pop $1
  ${If} $0 == 0
    DetailPrint "Graphviz detected: $1"
    Return
  ${EndIf}

  MessageBox MB_ICONEXCLAMATION|MB_YESNO \
    "This GraphVis build uses a feature that requires Graphviz, but Graphviz was not found.$\r$\n$\r$\nWould you like to open the official Graphviz download page now?$\r$\n$\r$\nAfter installing Graphviz, run GraphVis setup again." \
    IDYES +2
  Abort
  ExecShell "open" "https://graphviz.org/download/"
  Abort
FunctionEnd

Function CheckPrerequisites
!ifdef GRAPHVIS_REQUIRE_GRAPHVIZ
  Call CheckGraphviz
!endif
FunctionEnd

!endif
