PUNIXBASE	= ../../..
PATH := $(TIGCC):$(TIGCC)/bin:$(PATH)

STDINC	= $(PUNIXBASE)/include

CC	= "$(TIGCC)/bin/gcc"
CFLAGS	= -O2 -fomit-frame-pointer -Wall -nostdinc -fno-builtin -mno-bss \
	-I. -I- -I$(STDINC)

AS	= "$(TIGCC)/bin/tigcc"
ASFLAGS	= -c -Wa,--warn #-l

LD	= "$(TIGCC)/bin/ld-tigcc"
LDFLAGS	=

AR	= "$(TIGCC)/bin/ar-tigcc"

all:	libc.a
scratch:
	$(MAKE) clean
	$(MAKE) all

SRC = $(wildcard *.s *.c)
OBJ := $(SRC:.c=.o)
OBJ := $(OBJ:.s=.o)

libc.a: $(OBJ)
	@echo "  AR $@"
	@$(AR) -rc $@ $^
	cp $@ $(PUNIXBASE)/lib/

%.o: %.c
	@echo "  CC $@"
	@$(CC) -c $(CFLAGS) -o $@ $<

%.o: %.s
	@echo "  AS $@"
	@$(AS) -c $(ASFLAGS) -o $@ $<


########################################
# miscellaneous
########################################

DEPEND = .dep


depend $(DEPEND):
	@-$(CC) -M -MG $(CFLAGS) $(SRC) >$(DEPEND) #2>/dev/null

clean:
	rm -f *.bin *.o *.a
	rm -f $(DEPEND)

-include $(DEPEND)

.PHONY: all depend clean scratch
