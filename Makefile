ANSIBLE=ansible-playbook -i hosts
E=tinyvz


retail: retail.c
	gcc -o $@ $?

all: touch.ai logtail

touch.ai: ai.yml stoplist.${E} stopfiles.${E} syslog-ng.conf.${E} ai.sh
	${ANSIBLE} ai.yml
	touch touch.ai

lint:
	# +skip-sys-headers, as splint cannot parse _types.h:
        #
        #    /usr/include/sys/_types.h:94:36: Parse Error:
        #    Suspect missing struct or union keyword: __int64_t :
        #    int. (For help on parse errors, see splint -help parseerrors.)
        #    *** Cannot continue.
        #    make: *** [splint] Error 1
	splint +posixlib +skip-sys-headers -I/usr/include -I/usr/local/include retail

clean:
	rm -f touch.*
	rm -f retail
