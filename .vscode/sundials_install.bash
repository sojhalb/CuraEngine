cd /usr/local/src/
wget https://computation.llnl.gov/projects/sundials/download/sundials-3.2.1.tar.gz
tar zxvf sundials-3.2.1.tar.gz
mkdir -p Sundials
mkdir -p Sundials-build
mkdir -p examples
cd Sundials-build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local/src/Sundials \
-DEXAMPLES_INSTALL_PATH=/usr/local/src/Sundials/examples \
-DEXAMPLES_ENABLE=ON \
-DEXAMPLES_INSTALL=ON \
/usr/local/src/sundials-3.2.1/
make 
make install
rm -rf *.tar.gz
