set currenPath=%cd%
cd ..\..\
python .\waf configure --msvc_target=x64
python .\waf
cd %currenPath%
if not exist lib\ (
	mkdir lib
)
copy ..\..\build\norm*.dll lib
cd lib
del /f norm.dll
ren norm*.dll norm.dll
cd %currenPath%
dotnet pack . -c Release-win-x64