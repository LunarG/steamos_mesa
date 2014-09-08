tar -zxf ./glslang-r27900.src.tar.gz
mkdir -p LunarGLASS/Core/LLVM
cd LunarGLASS/Core/LLVM 
tar -zxf ../../../llvm-3.4.src.tar.gz
cd ../../..
tar -zxf LunarGLASS-r1050.src.tar.gz
cd glslang
mkdir build
cd build
cmake -D CMAKE_BUILD_TYPE=Release ..
cmake -D CMAKE_BUILD_TYPE=Release ..
make
make install
cd ../../LunarGLASS/Core/LLVM/llvm-3.4
mkdir build
cd build
../configure --enable-terminfo=no --enable-curses=no
REQUIRES_RTTI=1 make -j $(nproc) && make install DESTDIR=`pwd`/install
cd ../../../..
mkdir build
cd build
cmake -D CMAKE_BUILD_TYPE=Release ..
cmake -D CMAKE_BUILD_TYPE=Release ..
make
make install
cd ../..
./autogen.sh --enable-opengl --enable-gles1 --enable-gles2 --enable-egl \
      --enable-dri --enable-glx --enable-shared-glapi --enable-glx-tls \
      --enable-texture-float  --enable-dri3=no --with-dri-drivers=i965,swrast  \
      --with-gallium-drivers=swrast \
      --with-llvm-prefix=./LunarGLASS/Core/LLVM/llvm-3.4/build/install/usr/local \
      --with-lunarglass-prefix=./LunarGLASS \
      --with-glslang-prefix=./glslang \
      --disable-llvm-shared-libs --enable-gallium-llvm --enable-lunarglass 
make -j $(nproc)

