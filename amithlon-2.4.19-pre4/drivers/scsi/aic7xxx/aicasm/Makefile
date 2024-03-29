PROG=	aicasm

.SUFFIXES= .l .y .c .h

CSRCS=	aicasm.c aicasm_symbol.c
YSRCS=	aicasm_gram.y aicasm_macro_gram.y
LSRCS=	aicasm_scan.l aicasm_macro_scan.l

GENHDRS=	aicdb.h $(YSRCS:.y=.h)
GENSRCS=	$(YSRCS:.y=.c) $(LSRCS:.l=.c)

SRCS=	${CSRCS} ${GENSRCS}
CLEANFILES= ${GENSRCS} ${GENHDRS} $(YSRCS:.y=.output)
# Override default kernel CFLAGS.  This is a userland app.
AICASM_CFLAGS:= -I/usr/include -I. -ldb
YFLAGS= -d

NOMAN=	noman

ifneq ($(HOSTCC),)
AICASM_CC= $(HOSTCC)
else
AICASM_CC= $(CC)
endif

ifdef DEBUG
CFLAGS+= -DDEBUG -g
YFLAGS+= -t -v
LFLAGS= -d
endif

.NOTPARALLEL:

$(PROG):  ${GENHDRS} $(SRCS)
	$(AICASM_CC) $(AICASM_CFLAGS) $(SRCS) -o $(PROG)

aicdb.h:
	@if [ -e "/usr/include/db3/db_185.h" ]; then		\
		echo "#include <db3/db_185.h>" > aicdb.h;	\
	 elif [ -e "/usr/include/db2/db_185.h" ]; then		\
		echo "#include <db2/db_185.h>" > aicdb.h;	\
	 elif [ -e "/usr/include/db/db_185.h" ]; then		\
		echo "#include <db/db_185.h>" > aicdb.h;	\
	 elif [ -e "/usr/include/db_185.h" ]; then		\
		echo "#include <db_185.h>" > aicdb.h;		\
	 else							\
		echo "*** Install db development libraries";	\
	 fi

clean:
	rm -f $(CLEANFILES) $(PROG)

aicasm_gram.c aicasm_gram.h: aicasm_gram.y
	$(YACC) $(YFLAGS) -b $(<:.y=) $<
	mv $(<:.y=).tab.c $(<:.y=.c)
	mv $(<:.y=).tab.h $(<:.y=.h)

aicasm_macro_gram.c aicasm_macro_gram.h: aicasm_macro_gram.y
	$(YACC) $(YFLAGS) -b $(<:.y=) -p mm $<
	mv $(<:.y=).tab.c $(<:.y=.c)
	mv $(<:.y=).tab.h $(<:.y=.h)

aicasm_scan.c: aicasm_scan.l
	$(LEX) $(LFLAGS) -t $< > $@

aicasm_macro_scan.c: aicasm_macro_scan.l
	$(LEX) $(LFLAGS) -t -Pmm $< > $@
