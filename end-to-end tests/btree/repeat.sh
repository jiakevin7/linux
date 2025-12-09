for Mode in baseline warm prefetch; do
File=$Mode.txt
> $File
THREADED=st
	for i in {1}; do
		BTREE_BASE_PAGES=1024 PT_MODE=$Mode BTREE_TIME_FIRST_K=100 ./bin/bench_btree_$THREADED >> $File
	done
done
