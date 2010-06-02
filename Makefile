CFLAGS=-Wall -Wextra -O2 -g
OBJS=ast.o ast-printer.o codebuf.o debugger.o elf-dumper.o \
	parser.o main.o optimizer.o vm.o

all: bfi

bfi: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(OBJS)

distclean: clean
	rm bfi

.PHONY: all clean distclean
