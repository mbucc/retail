retail: retail.c
	gcc -o $@ $?

test: retail test_retail.sh
	./test_retail.sh

CHECKER_VERSION=276
CHECKER_PREFIX=${HOME}

scan-build: ${CHECKER_PREFIX}/bin/scan-build


${CHECKER_PREFIX}/bin/scan-build: ${CHECKER_PREFIX}/src/checker-${CHECKER_VERSION}/scan-build
	ln -s -f $? $@

${CHECKER_PREFIX}/src/checker-${CHECKER_VERSION}/scan-build: checker-${CHECKER_VERSION}.tar.bz2
	tar -C ${CHECKER_PREFIX}/src -xvf $?
	@# Touch, otherwise make keeps untarring over and over
	@# as timestamp on binary in tarball is older than the
	@# one on the tarball.
	touch $@

CHECKER_URL=http://clang-analyzer.llvm.org/downloads
checker-${CHECKER_VERSION}.tar.bz2:
	curl ${CHECKER_URL}/checker-${CHECKER_VERSION}.tar.bz2 > t.bz2
	mv t.bz2 $@

lint: scan-build
	scan-build make retail

clean:
	rm -f retail
