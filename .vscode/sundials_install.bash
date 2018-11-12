tar zxvf sundials-3.2.1.tar.gz
cd /usr/local/src/
mkdir -p Sundials
mkdir -p Sundials-build
mkdir -p examples
cd Sundials-build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local/src/Sundials \
-DEXAMPLES_INSTALL_PATH=/usr/local/src/Sundials/examples \
-DEXAMPLES_ENABLE=ON \
-DEXAMPLES_INSTALL=ON \
/usr/local/src/CuraEngine/.vscode/sundials-3.2.1/
make 
make install
