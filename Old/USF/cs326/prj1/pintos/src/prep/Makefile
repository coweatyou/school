
all: test_mem listtest

LIBDIR=../lib
KERNELDIR=$(LIBDIR)/kernel
SOURCES=memalloc.c $(KERNELDIR)/list.c
SOURCES1=test_mem.c $(SOURCES)
SOURCES2=listtest.c $(KERNELDIR)/list.c $(LIBDIR)/string.c

all: test_mem listtest

test_mem: $(SOURCES1)
	$(CC) -m32 -g -Wall -I$(LIBDIR) -I$(KERNELDIR) -o $@ $(SOURCES1) -lpthread


listtest: $(SOURCES2)
	$(CC) -m32 -g -Wall -I$(LIBDIR) -I$(KERNELDIR) -o $@ $(SOURCES2) -lpthread

clean:
	/bin/rm -f test_mem listtest *.o
