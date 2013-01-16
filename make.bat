REM @echo OFF
set ERRORLEVEL=
set DDKBUILDENV=
set DDKDIR=C:\winDDK\7600.16385.1
set CROSS_CERT=globalsign_cross.crt
set CERTID=""
set PATH=%DDKDIR%;C:\Windows\System;C:\Windows;C:\Windows\System32
for /f "tokens=*" %%d in ('cd') do set OLDPWD=%%d

echo "building x86 driver"

call %DDKDIR%\bin\setenv.bat %DDKDIR% fre x86 WIN7
IF ERRORLEVEL 1 goto ERROR

cd %OLDPWD%
IF ERRORLEVEL 1 goto ERROR

%OLDPWD:~0,2%
IF ERRORLEVEL 1 goto ERROR

build /gwcf
IF ERRORLEVEL 1 goto ERROR

echo "build x64 driver"
set DDKBUILDENV=
call %DDKDIR%\bin\setenv.bat %DDKDIR% fre x64 WIN7
IF ERRORLEVEL 1 goto ERROR

cd %OLDPWD%
IF ERRORLEVEL 1 goto ERROR

%OLDPWD:~0,2%
IF ERRORLEVEL 1 goto ERROR

build /gwcf
IF ERRORLEVEL 1 goto ERROR

copy /y /b objfre_win7_amd64\amd64\usbchief.sys x64
IF ERRORLEVEL 1 goto ERROR

copy /y /b objfre_win7_x86\i386\usbchief.sys x86
IF ERRORLEVEL 1 goto ERROR

Inf2Cat /driver:x64 /os:7_x64
IF ERRORLEVEL 1 goto ERROR

SignTool.exe sign /ac %CROSS_CERT% /n %CERTID% /t http://timestamp.verisign.com/scripts/timestamp.dll x64/usbchief.sys x64/usbchief.cat
IF ERRORLEVEL 1 goto ERROR

signtool verify /kp x64/usbchief.sys
IF ERRORLEVEL 1 goto ERROR

signtool verify /kp /c x64/usbchief.cat x64/usbchief.sys
IF ERRORLEVEL 1 goto ERROR

Inf2Cat /driver:x86 /os:7_x86
IF ERRORLEVEL 1 goto ERROR

SignTool.exe sign /ac %CROSS_CERT% /n %CERTID% /t http://timestamp.verisign.com/scripts/timestamp.dll x86/usbchief.sys x86/usbchief.cat
IF ERRORLEVEL 1 goto ERROR

signtool verify /kp x86/usbchief.sys
IF ERRORLEVEL 1 goto ERROR

signtool verify /kp /c x86/usbchief.cat x86/usbchief.sys
IF ERRORLEVEL 1 goto ERROR


:ERROR
