# Project sources
SRCS = main.c stm32f4xx_it.c system_stm32f4xx.c syscalls.c utils.c
SRCS += Audio.c adc.c synth.c seq.c accel.c stm32f4_discovery_lis302dl.c

# all the files will be generated with this name (main.elf, main.bin, main.hex, etc)

PROJ_NAME=synth
OUTPATH=build
LOADADDR=0x8000000

###################################################

# Check for valid float argument
# NOTE that you have to run make clan after
# changing these as hardfloat and softfloat are not
# binary compatible
ifneq ($(FLOAT_TYPE), hard)
ifneq ($(FLOAT_TYPE), soft)
override FLOAT_TYPE = hard
#override FLOAT_TYPE = soft
endif
endif

###################################################

BINPREFIX=arm-none-eabi-
CC=$(BINPREFIX)gcc
OBJCOPY=$(BINPREFIX)objcopy
SIZE=$(BINPREFIX)size
STFLASH=st-flash

CFLAGS  = -std=gnu99 -g -O2 -Wall -Wextra -Wno-unused-parameter -Tstm32_flash.ld
CFLAGS += -mlittle-endian -mthumb -mthumb-interwork -nostartfiles -mcpu=cortex-m4

ifeq ($(FLOAT_TYPE), hard)
CFLAGS += -fsingle-precision-constant -Wdouble-promotion
CFLAGS += -mfpu=fpv4-sp-d16 -mfloat-abi=hard
#CFLAGS += -mfpu=fpv4-sp-d16 -mfloat-abi=softfp
else
CFLAGS += -msoft-float
endif

###################################################

vpath %.c src
vpath %.a lib

ROOT=$(shell pwd)

CFLAGS += -Iinc -Ilib -Ilib/inc 
CFLAGS += -Ilib/inc/core -Ilib/inc/peripherals -DHSE_VALUE=8000000

# add startup file to build
SRCS += lib/startup_stm32f4xx.s

# Libraries to use
LIBS = -Llib -lstm32f4 -lm

OBJS = $(SRCS:.c=.o)

###################################################

.PHONY: lib proj

all: lib proj
	$(SIZE) $(OUTPATH)/$(PROJ_NAME).elf

flash: proj
	$(STFLASH) write $(OUTPATH)/$(PROJ_NAME).bin $(LOADADDR)

lib:
	$(MAKE) -C lib FLOAT_TYPE=$(FLOAT_TYPE) BINPREFIX=$(BINPREFIX)

proj: 	$(OUTPATH)/$(PROJ_NAME).elf

$(OUTPATH)/$(PROJ_NAME).elf: $(SRCS) | lib
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)
	$(OBJCOPY) -O ihex $(OUTPATH)/$(PROJ_NAME).elf $(OUTPATH)/$(PROJ_NAME).hex
	$(OBJCOPY) -O binary $(OUTPATH)/$(PROJ_NAME).elf $(OUTPATH)/$(PROJ_NAME).bin

clean:
	rm -f *.o
	rm -f $(OUTPATH)/$(PROJ_NAME).elf
	rm -f $(OUTPATH)/$(PROJ_NAME).hex
	rm -f $(OUTPATH)/$(PROJ_NAME).bin
	$(MAKE) clean -C lib
	
