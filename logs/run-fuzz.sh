# I'm using the non-desktop docker
docker context use default
docker info

mkdir -p crash/default/crashes/
docker build -t my-fuzzer .

# I'm not using ramfs any more but I forgot to remove the --shm-size="1g".
docker run -it --name fuzz --shm-size="1g" my-fuzzer:latest
make

make fuzz