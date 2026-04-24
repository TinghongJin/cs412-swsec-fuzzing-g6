patch -p0 < changes.patch

cd /libpng-1.6.15

CC=afl-clang-fast \
CXX=afl-clang-fast++ \
CFLAGS="-fsanitize=address -g -O1" \
LDFLAGS="-fsanitize=address" \
./configure --disable-shared --prefix=$(pwd)/install
make -j$(nproc)
make install

