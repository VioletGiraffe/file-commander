git pull

#In case there are new submodules that have not yet been cloned
./init_submodules.sh

cd qtutils
git pull
cd ..

cd text-encoding-detector
git pull
cd ..

cd cpputils
git pull
cd ..