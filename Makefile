retail: retail.c
	clang -lz -o $@ $?

lint: retail.c
	clang -lz -Weverything -o retail $?

test: retail test_retail.sh
	./test_retail.sh

clean:
	rm -f retail
