How to use:
1. Build release or debug
2. Run by provided shortcut
The shortcut must have the command line flag "-path ..\..\..\examples\CommandLine_and_DepFile\"
Where "..\..\..\examples\CommandLine_and_DepFile\" relative path from AssetCooker.exe to this folder, 
absolute path is also supported.

The script combine.bat searches for the text file pairs with postfixes "_r" and "_g" to concatenate files content 
of the files, and store it as a new file with postfix "_out"
The script also outputs a dependency file, which allows file recompilation to be triggered if any of the 
source files "_r" or "_g" are modified.