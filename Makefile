CC=gcc
CFLAGS=-Wall -rdynamic -ggdb

PORT=8080

LIBWHISPER := deps/libwhisper/libwhisper.a
STATIC := $(LIBWHISPER) 
DEPS := $(STATIC)

HTML_DIR=html
HTML_SRCS=$(shell find $(HTML_DIR) -type f -name "*.html")
HTML_HEADERS=$(patsubst %.html,%.html.h,$(HTML_SRCS))

MK_HTML=mk_html
MK_HTML_DIR=mk_html_src
MK_HTML_SRCS=$(shell find $(MK_HTML_DIR) -type f -name "*.c")
MK_HTML_OBJS=$(patsubst %.c,%.o,$(MK_HTML_SRCS))

SERVER=server
SERVER_DIR=server_src
SERVER_SRCS=$(shell find $(SERVER_DIR) -type f -name "*.c")
SERVER_OBJS=$(patsubst %.c,%.o,$(SERVER_SRCS))
SERVER_SYMBOLS := $(SERVER_OBJS)
SERVER_SYMBOLS += $(STATIC)
SERVER_DEPS := $(HTML_HEADERS) $(SERVER_SYMBOLS) 

INCLUDES=-I$(HTML_DIR) -Ideps/libwhisper/api
LIBS=-lpthread
CFLAGS+=$(INCLUDES)
CFLAGS+=$(LIBS)

all: $(SERVER)

html: $(HTML_HEADERS)

$(MK_HTML): $(MK_HTML_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.html.h: %.html $(MK_HTML)
	$(MK_HTML) $<

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
