CC=gcc
CFLAGS=-Wall -rdynamic -ggdb

PORT=8080

LIBWHISPER := deps/libwhisper/libwhisper.a
STATIC := $(LIBWHISPER) 
DEPS := $(STATIC)

SERVER=server
SERVER_DIR=server_src
SERVER_SRCS=$(shell find $(SERVER_DIR) -type f -name "*.c")
SERVER_OBJS=$(patsubst %.c,%.o,$(SERVER_SRCS))
SERVER_SYMBOLS := $(SERVER_OBJS)
SERVER_SYMBOLS += $(STATIC)
SERVER_DEPS := $(SERVER_SYMBOLS) 

INCLUDES=-Ideps/libwhisper/api
LIBS=-lpthread
CFLAGS+=$(INCLUDES)
CFLAGS+=$(LIBS)

all: $(SERVER)

host: $(SERVER) 
	./$(SERVER) $(PORT)

$(SERVER): $(SERVER_DEPS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ 

define create_rule
$(1):
	@echo "Making dependency in $(dir $(1))"
	make -C $(dir $(1))
endef

$(foreach dep,$(DEPS),$(eval $(call create_rule,$(dep))))

clean: 
	rm -f $(SERVER) $(SERVER_OBJS) $(HTML_HEADERS) $(MK_HTML) $(MK_HTML_OBJS)
