CC = clang
CFLAGS ?= -g -O3 -flto --std=c17 -pedantic -Wall
LDFLAGS ?=
override LDFLAGS += -luring

srcs := \
	io_uring.c

all_targets :=

targets := $(patsubst %.c,%,$(patsubst %.cc,%,$(srcs)))
all_targets += $(targets)

all: $(targets)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	@rm -f $(all_targets)

.PHONY: all clean
