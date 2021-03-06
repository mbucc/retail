BIND=/usr/local/bin
MAND=/usr/local/share/man/man1
#CC=gcc
CC=clang

all: README retail

retail: retail.c
	${CC} -lz -o $@ $?


#--------------------------------------------------------
#
#                        testing
#
#--------------------------------------------------------

# Check out:
# http://lcamtuf.coredump.cx/afl/
# https://code.google.com/p/address-sanitizer/wiki/AddressSanitizer
lint: retail.c
	${CC} -lz -Weverything -o retail $?

	
test: lint test_retail.sh
	./test_retail.sh


#--------------------------------------------------------
#
#                     documentation
#
#--------------------------------------------------------

README: retail.1
	@# Zap backspace character (^H) and preceding char
	groff -Tascii -man $? | sed -e 's/.//g' >> t
	mv t $@


install: ${BIND}/retail ${MAND}/retail.1

${BIND}/retail: retail
	cp -p $? $@
	chmod 755 $@

${MAND}/retail.1: retail.1
	cp -p $? $@

#--------------------------------------------------------
#
#                        misc
#
#--------------------------------------------------------


clean:
	rm -f retail
	rm -rf testing


#--------------------------------------------------------
#
#                        fuzzing
#
# Commented out, couldn't get it to do anything useful.
# Ran, but got the following warning:
#
#	Instrumentation output varies across runs.
#
#--------------------------------------------------------
# 
# AFLVERS=0.89b
# AFL=afl-${CC}
# 
# afl-${AFLVERS}.tgz:
# 	curl http://lcamtuf.coredump.cx/afl/releases/afl-${AFLVERS}.tgz > t
# 	mv t afl-${AFLVERS}.tgz
# 
# fuzz/afl-${AFLVERS}: afl-${AFLVERS}.tgz
# 	mkdir -p fuzz
# 	tar -C fuzz -xf $?
# 
# fuzz/afl-${AFLVERS}/${AFL}: fuzz/afl-${AFLVERS}
# 	(cd fuzz/afl-${AFLVERS} ; make)
# 	touch $@
# 
# fuzzyretail: fuzz/afl-${AFLVERS}/${AFL} retail.c
# 	AFL_HARDEN=1 $? -lz -o fuzzyretail
# 
# 
# fuzzer: fuzzyretail
# 	fuzz/afl-${AFLVERS}/afl-fuzz -i fuzz/testcases -o fuzz/findings ./fuzzyretail -o @@ testing/1.log
# 
#
