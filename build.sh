git clone https://github.com/google/googletest && cd googletest && mkdir build && cd build && cmake .. && make -j && sudo make install
rm -rf build
mkdir build && cd build
cmake ..
make