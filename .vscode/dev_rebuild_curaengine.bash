cd /usr/local/src/CuraEngine
git checkout dev
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
make install
