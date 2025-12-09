
echo "control running"
for i in {1..10}
do
	PT_MODE=control ./test
done

echo "prefetch running"
for i in {1..10}
do
	PT_MODE=prefetch ./test
done
