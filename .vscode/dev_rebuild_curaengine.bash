cd /usr/local/src/CuraEngine
git checkout dev
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
make install

cd /usr/local/src/
mkdir Sundials
mkdir Sundials-build
mkdir examples
cd Sundials-build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local/src/Sundials \
-DEXAMPLES_INSTALL_PATH=/usr/local/src/Sundials/examples \
-DEXAMPLES_ENABLE=ON \
-DEXAMPLES_INSTALL=ON \
~/Downloads/Sundials/
make 
make install
