CC ?= cc
AR ?= ar
CPPFLAGS += -Iinclude -Icompat -Ivendor/libmctp -Ivendor/libpldm/include -Ivendor/libpldm/src
CPPFLAGS += -include compat/kit_config.h -DMCTP_DEFAULT_ALLOC -DMCTP_DEFAULT_CLOCK_GETTIME=0
CPPFLAGS += -DMCTP_MAX_MESSAGE_SIZE=512 -DMCTP_MAX_BUSSES=1 -DMCTP_REASSEMBLY_CTXS=2
CFLAGS += -std=gnu11 -O2 -g -Wall -Wextra -ffunction-sections -fdata-sections
CORE = vendor/libmctp/core.c vendor/libmctp/alloc.c vendor/libmctp/log.c vendor/libmctp/control.c vendor/libmctp/crc32.c
PLDM = vendor/libpldm/src/dsp/base.c
SRC = src/chip_endpoint.c src/compat.c $(CORE) $(PLDM)
OBJ = $(SRC:%.c=build/%.o)

.PHONY: all test clean
all: build/libchip_endpoint.a
build/libchip_endpoint.a: $(OBJ)
	$(AR) rcs $@ $^
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/test_endpoint: tests/test_endpoint.c build/libchip_endpoint.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libchip_endpoint.a -Wl,--gc-sections $(LDFLAGS) -o $@
test: build/test_endpoint
	./build/test_endpoint
clean:
	rm -rf build
