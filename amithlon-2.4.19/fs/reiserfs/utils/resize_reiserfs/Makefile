
VPATH = ../bin

OBJS = resize_reiserfs.o fe.o

RESIZER = $(TMPBINDIR)/resize_reiserfs

all: $(RESIZER)

.c.o:
	$(CC) $(CFLAGS) -Wall -g $<

$(RESIZER): $(OBJS) libmisc.a
	$(CC) $(LFLAGS) -o $(RESIZER) $(OBJS) -lmisc

clean:
	rm -f *.o $(RESIZER) *~

dep:
	gcc -MM $(IDIRS) *.c > .depend

install:
	cp -f $(RESIZER) $(SBIN)

uninstall:
	rm -f $(SBIN)/resize_reiserfs


ifeq (.depend,$(wildcard .depend))
include .depend
endif









