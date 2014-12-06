retail: retail.c
	clang -Weverything -o $@ $?

test: retail test_retail.sh
	./test_retail.sh

clean:
	rm -f retail
