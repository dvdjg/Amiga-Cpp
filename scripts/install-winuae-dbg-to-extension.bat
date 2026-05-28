@echo off
REM Copia winuae-gdb.exe compilado (WinUAE-DBG) sobre el de la extension Amiga Debug.
setlocal
set "SRC=%~dp0..\..\WinUAE-DBG\bin\winuae-gdb.exe"
set "EXT=%USERPROFILE%\.cursor\extensions\bartmanabyss.amiga-debug-1.8.2\bin\win32\winuae-gdb.exe"
if not exist "%SRC%" (
  echo No existe %SRC%
  echo Compila antes: cd WinUAE-DBG ^&^& build.bat
  exit /b 1
)
if not exist "%EXT%" (
  echo No existe extension en %EXT%
  exit /b 1
)
if not exist "%EXT%.bak" copy /y "%EXT%" "%EXT%.bak" >nul
copy /y "%SRC%" "%EXT%"
echo OK: %SRC% -^> %EXT%
echo Reinicia la sesion de depuracion en Cursor.
