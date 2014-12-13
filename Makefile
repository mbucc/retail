
all: README retail

retail: retail.c
	clang -lz -o $@ $?

lint: retail.c
	clang -lz -Weverything -o retail $?

	

test: retail test_retail.sh
	./test_retail.sh


README: retail.3
	@# Zap backspace character (^H) and preceding char
	groff -Tascii -man $? | sed -e 's/.//g' >> t
	mv t $@


BIND=/usr/local/bin
MAND=/usr/share/man/man3
install: ${BIND}/retail ${MAND}/retail.3

${BIND}/retail: retail
	cp -p $? $@

${MAND}/retail.3: retail.3
	cp -p $? $@

clean:
	rm -f retail
	rm -rf testing
