CFLAGS ?= -g -O2 -pedantic -Wall
LDFLAGS ?=
override LDFLAGS += -luring

srcs := \
	server.c

all_targets :=

targets := $(patsubst %.c,%,$(patsubst %.cc,%,$(srcs)))
all_targets += $(targets)

all: $(targets)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	@rm -f $(all_targets)

.PHONY: all clean
