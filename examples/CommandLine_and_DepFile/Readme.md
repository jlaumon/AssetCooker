How to use:
1. Build Release or Debug
2. Run the corresping bat file (StartAssetCookerDebug or StartAssetCookerRelease)

The script combine.bat searches for the text file pairs with postfixes "_r" and "_g" to concatenate files content 
of the files, and store it as a new file with postfix "_out"
The script also outputs a dependency file, which allows file recompilation to be triggered if any of the 
source files "_r" or "_g" are modified.