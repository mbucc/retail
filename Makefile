retail: retail.c
	clang -o $@ $?

lint: retail.c
	clang -Weverything -o retail $?

test: retail test_retail.sh
	./test_retail.sh

clean:
	rm -f retail
