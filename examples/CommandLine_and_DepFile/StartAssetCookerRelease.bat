@echo off

:: Change current directory just to have an excuse for using -working_dir
cd ..\..
start bin\x64\Release\AssetCooker.exe -working_dir examples\CommandLine_and_DepFile

:: This would do exactly the same
:: ..\..bin\x64\Release\AssetCooker.exe

