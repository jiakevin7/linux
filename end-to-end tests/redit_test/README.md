sudo apt-get install numactl build-essential git

git clone https://github.com/redis/redis.git
cd redis
make -j

mkdir ~/redis-numa-test
cd ~/redis-numa-test