CC=gcc
CFLAGS=-ggdb

PORT=8080

SERVER=server
SERVER_DIR=server_src
SERVER_SRCS=$(shell find $(SERVER_DIR) -type f -name "*.c")
SERVER_OBJS=$(patsubst %.c,%.o,$(SERVER_SRCS))

HTML_DIR=html
HTML_SRCS=$(shell find $(HTML_DIR) -type f -name "*.html")
HTML_HEADERS=$(patsubst %.html,%.html.h,$(HTML_SRCS))

MK_HTML=mk_html
MK_HTML_DIR=mk_html_src
MK_HTML_SRCS=$(shell find $(MK_HTML_DIR) -type f -name "*.c")
MK_HTML_OBJS=$(patsubst %.c,%.o,$(MK_HTML_SRCS))

INCLUDES=-I$(HTML_DIR)
LIBS=
CFLAGS+=$(INCLUDES)
CFLAGS+=$(LIBS)

all: $(SERVER)

html: $(HTML_HEADERS)

$(MK_HTML): $(MK_HTML_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.html.h: %.html $(MK_HTML)
	$(MK_HTML) $<

host: html $(SERVER) 
	./$(SERVER) $(PORT)

$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ 

clean: 
	rm -f $(SERVER) $(SERVER_OBJS) $(HTML_HEADERS) $(MK_HTML) $(MK_HTML_OBJS)
