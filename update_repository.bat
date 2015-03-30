git pull

REM In case there are new submodules that have not yet been cloned
call init_submodules.bat

cd qtutils
git pull
cd ..

cd text-encoding-detector
git pull
cd ..