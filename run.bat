@echo off
set "PATH=E:\qt6\6.12.0\mingw_64\bin;E:\qt6\Tools\mingw1310_64\bin;%PATH%"
start "" "%~dp0build\windows-mingw-debug\prometheus_app.exe" %*
