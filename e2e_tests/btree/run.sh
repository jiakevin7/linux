for THREAD_MODE in st # mt
do	
	RES_DIR="${THREAD_MODE}_res"
	mkdir -p $RES_DIR
	
	for SYSCALL in {0..0}
	do
		for PT_MODE in control pt_warm # pte_prefetch pte_prefetch_load
		do
			OUT_FILE="${RES_DIR}/${PT_MODE}_${SYSCALL}.txt"
			> $OUT_FILE
			for i in {1..10}
			do
				echo "running iteration $i" >> $OUT_FILE 
				BTREE_BASE_PAGES=1024 PT_MODE=${PT_MODE} BTREE_TIME_FIRST_K=16 SYSCALL=$SYSCALL "./bin/bench_btree_${THREAD_MODE}" 2>&1 | tee -a $OUT_FILE
			done
		done
	done
done



