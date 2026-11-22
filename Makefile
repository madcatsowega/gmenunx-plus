PLATFORM = retrofw

CFLAGS += -O2 -fomit-frame-pointer -mplt -mno-shared
CFLAGS += -mhard-float -mips32 -mno-mips16

include gmenunx.mk
