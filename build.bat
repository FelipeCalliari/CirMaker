@echo off
rem Compila o CirMaker no Windows (Qt 6 + MinGW ou MSVC).
rem
rem   build.bat                 Release em build-win\
rem   build.bat debug           Debug
rem   build.bat test            compila e roda os testes (ctest)
rem   build.bat package         compila e gera dist\CirMaker-<versao>-windows-x64.zip
rem                             (com as DLLs do Qt e o ngspice para a simulacao analogica)
rem   build.bat clean           apaga o diretorio de build antes
rem   (as opcoes podem ser combinadas: build.bat clean package)
rem
rem Variaveis:
rem   QT_DIR       prefixo do Qt, ex. C:\Qt\6.8.3\mingw_64 (obrigatoria se o
rem                qtpaths do Qt nao estiver no PATH)
rem   MINGW_DIR    bin do MinGW, ex. C:\Qt\Tools\mingw1310_64\bin (se nao estiver no PATH)
rem   BUILD_DIR    diretorio de build (padrao build-win)
rem   NGSPICE_VER  versao do ngspice a embutir no pacote (padrao 47)
rem
rem Requer CMake >= 3.21 (vem com o Qt em C:\Qt\Tools\CMake_64\bin). Para usar
rem MSVC, rode a partir do "x64 Native Tools Command Prompt" com um Qt msvc2022_64.
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "BUILD_TYPE=Release"
set "RUN_TESTS=0"
set "PACKAGE=0"
set "CLEAN=0"
:args
if "%~1"=="" goto args_done
if /i "%~1"=="debug"   (set "BUILD_TYPE=Debug"   & shift & goto args)
if /i "%~1"=="test"    (set "RUN_TESTS=1"        & shift & goto args)
if /i "%~1"=="package" (set "PACKAGE=1"          & shift & goto args)
if /i "%~1"=="clean"   (set "CLEAN=1"            & shift & goto args)
if /i "%~1"=="help"    goto usage
if /i "%~1"=="/?"      goto usage
echo opcao desconhecida: %~1
goto usage_error
:args_done

if not defined BUILD_DIR set "BUILD_DIR=build-win"
if not defined NGSPICE_VER set "NGSPICE_VER=47"

rem --- ferramentas
if defined QT_DIR (
    if not exist "%QT_DIR%\bin\qtpaths.exe" if not exist "%QT_DIR%\bin\qtpaths6.exe" (
        echo erro: QT_DIR=%QT_DIR% nao parece um Qt 6 ^(falta bin\qtpaths.exe^)
        exit /b 1
    )
    set "PATH=%QT_DIR%\bin;%PATH%"
)
if defined MINGW_DIR set "PATH=%MINGW_DIR%;%PATH%"

where cmake >nul 2>nul || (
    rem o CMake que vem com o instalador do Qt
    if defined QT_DIR if exist "%QT_DIR%\..\..\Tools\CMake_64\bin\cmake.exe" set "PATH=%QT_DIR%\..\..\Tools\CMake_64\bin;!PATH!"
)
where cmake >nul 2>nul || (echo erro: cmake nao encontrado & exit /b 1)

if not defined QT_DIR (
    for /f "delims=" %%p in ('qtpaths --query QT_INSTALL_PREFIX 2^>nul') do set "QT_DIR=%%p"
)
if not defined QT_DIR (
    echo erro: Qt nao encontrado. Defina QT_DIR, ex.: set QT_DIR=C:\Qt\6.8.3\mingw_64
    exit /b 1
)

rem gerador: Ninja se houver; senao MSVC ^(cl^) ou MinGW Makefiles
set "GENERATOR="
where ninja >nul 2>nul && set "GENERATOR=Ninja"
if not defined GENERATOR if exist "%QT_DIR%\..\..\Tools\Ninja\ninja.exe" (
    set "PATH=%QT_DIR%\..\..\Tools\Ninja;%PATH%"
    set "GENERATOR=Ninja"
)
if not defined GENERATOR (
    where cl >nul 2>nul && set "GENERATOR=NMake Makefiles"
)
if not defined GENERATOR (
    where mingw32-make >nul 2>nul || (
        echo erro: nenhum compilador. Coloque o MinGW no PATH ^(MINGW_DIR^) ou use o prompt do MSVC.
        exit /b 1
    )
    set "GENERATOR=MinGW Makefiles"
)

if "%CLEAN%"=="1" if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

echo ==^> Configurando (%BUILD_TYPE%, %GENERATOR%) em %BUILD_DIR%
if exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_PREFIX_PATH="%QT_DIR%" || exit /b 1
) else (
    cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_PREFIX_PATH="%QT_DIR%" || exit /b 1
)
echo ==^> Compilando
cmake --build "%BUILD_DIR%" --parallel %NUMBER_OF_PROCESSORS% || exit /b 1

if "%RUN_TESTS%"=="1" (
    echo ==^> Testes
    set "QT_QPA_PLATFORM=offscreen"
    ctest --test-dir "%BUILD_DIR%" --output-on-failure || exit /b 1
)

if "%PACKAGE%"=="1" call :package || exit /b 1

echo ==^> Pronto: %BUILD_DIR%\cirmaker.exe
exit /b 0

rem ---------------------------------------------------------------------------
:package
set "VERSION="
for /f "tokens=2 delims==" %%v in ('findstr /b /c:"CMAKE_PROJECT_VERSION:STATIC=" "%BUILD_DIR%\CMakeCache.txt"') do set "VERSION=%%v"
if defined CM_VERSION set "VERSION=%CM_VERSION%"
set "NAME=CirMaker-%VERSION%-windows-x64"
set "STAGE=dist\%NAME%"
echo ==^> Empacotando %STAGE%.zip
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%STAGE%.zip" del /q "%STAGE%.zip"
mkdir "%STAGE%" || exit /b 1
copy /y "%BUILD_DIR%\cirmaker.exe" "%STAGE%\" >nul || exit /b 1
copy /y "%BUILD_DIR%\libcmcosim.dll" "%STAGE%\" >nul || exit /b 1
copy /y README.md "%STAGE%\" >nul
if exist LICENSE copy /y LICENSE "%STAGE%\" >nul

rem DLLs do Qt, plugins e as traducoes do Qt; o shim de co-simulacao usa QtCore
set "DEPLOY_MODE=--release"
if /i "%BUILD_TYPE%"=="Debug" set "DEPLOY_MODE=--debug"
windeployqt %DEPLOY_MODE% --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "%STAGE%\cirmaker.exe" "%STAGE%\libcmcosim.dll" || exit /b 1

call :ngspice || exit /b 1

rem o zip contem a pasta CirMaker-<versao>-windows-x64\
tar -a -c -f "%STAGE%.zip" -C dist "%NAME%" || exit /b 1
echo     %STAGE%.zip
exit /b 0

rem ---------------------------------------------------------------------------
rem ngspice compartilhado (simulacao analogica e mista): baixa os pacotes
rem oficiais uma vez para dist\ngspice-<versao>\ e copia para o pacote:
rem   ngspice.dll e dependencias ao lado do cirmaker.exe,
rem   ngspice\lib\*.cm (code models XSPICE) e ngspice\scripts\spinit
:ngspice
set "NG_CACHE=dist\ngspice-%NGSPICE_VER%"
set "NG_URL=https://sourceforge.net/projects/ngspice/files/ng-spice-rework/%NGSPICE_VER%"
if not exist "%NG_CACHE%\Spice64_dll\dll-vs\ngspice.dll" (
    echo     baixando ngspice %NGSPICE_VER% ^(DLL^)
    mkdir "%NG_CACHE%" 2>nul
    curl -fL --retry 3 -o "%NG_CACHE%\dll.7z" "%NG_URL%/ngspice-%NGSPICE_VER%_dll_64.7z/download" || exit /b 1
    call :extract "%NG_CACHE%\dll.7z" "%NG_CACHE%" || exit /b 1
)
if not exist "%NG_CACHE%\Spice64\bin\sndfile.dll" (
    rem a DLL depende de sndfile.dll e samplerate.dll, que so vem no pacote de console
    echo     baixando ngspice %NGSPICE_VER% ^(console, pelas DLLs de audio^)
    curl -fL --retry 3 -o "%NG_CACHE%\con.7z" "%NG_URL%/ngspice-%NGSPICE_VER%_64.7z/download" || exit /b 1
    call :extract "%NG_CACHE%\con.7z" "%NG_CACHE%" || exit /b 1
)
copy /y "%NG_CACHE%\Spice64_dll\dll-vs\ngspice.dll" "%STAGE%\" >nul || exit /b 1
copy /y "%NG_CACHE%\Spice64_dll\dll-vs\libomp140.x86_64.dll" "%STAGE%\" >nul || exit /b 1
copy /y "%NG_CACHE%\Spice64\bin\sndfile.dll" "%STAGE%\" >nul || exit /b 1
copy /y "%NG_CACHE%\Spice64\bin\samplerate.dll" "%STAGE%\" >nul || exit /b 1
mkdir "%STAGE%\ngspice\lib" "%STAGE%\ngspice\scripts"
copy /y "%NG_CACHE%\Spice64_dll\lib\ngspice\*.cm" "%STAGE%\ngspice\lib\" >nul || exit /b 1
copy /y "%NG_CACHE%\Spice64_dll\lib\ngspice\ivlng.dll" "%STAGE%\ngspice\lib\" >nul
rem o spinit oficial carrega os code models de C:\Spice64; o CirMaker os
rem carrega da pasta do programa, entao saem as linhas codemodel e osdi
findstr /v /i /r /c:"^ *codemodel " /c:"^ *osdi " "%NG_CACHE%\Spice64_dll\share\ngspice\scripts\spinit" > "%STAGE%\ngspice\scripts\spinit"
exit /b 0

:extract
rem 7-Zip se houver; senao o tar do Windows 10+ (libarchive le .7z)
where 7z >nul 2>nul && (7z x -y -bd "-o%~2" "%~1" >nul & exit /b !errorlevel!)
tar -x -f "%~1" -C "%~2"
exit /b %errorlevel%

:usage
findstr /b "rem" "%~f0" | more +1
exit /b 0
:usage_error
echo uso: build.bat [debug] [test] [package] [clean]
exit /b 2
