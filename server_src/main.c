#include <arpa/inet.h>
#include <bits/pthreadtypes.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define _GNU_SOURCE
#include <execinfo.h>

#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>

#include "whisper/array.h"
#include "whisper/colmap.h"

#include "httpdef.h"

#define PATH_BUF_SZ 256
#define MAX_CLIENTS 512

void clean_main();

void sigsegv(int signal) {
  void *array[10];
  size_t size;

  // Get void*'s for all entries on the stack
  size = backtrace(array, 10);

  fprintf(stderr, "\n\nSEGFAULT sig %d: stacktrace - \n", signal);
  backtrace_symbols_fd(array, size, STDERR_FILENO);

  clean_main();
  exit(1);
}

WColMap method_map;
WColMap route_alias_map;

typedef struct HTTPHeader {
  HTTPMethod method;
  char route_path[PATH_BUF_SZ];
  int connection_close; // 1 if it should close.
} HTTPHeader;

typedef struct Client {
  pthread_t thread;
  int socket;
} Client;

void parse_method_line(HTTPHeader *header, char *method_line) {
  int len = strlen(method_line);

  // parse method
  char *method_tok = strtok(method_line, " ");
  HTTPMethod *m_ptr = (HTTPMethod *)w_cm_get(&method_map, method_tok);
  if (m_ptr) {
    header->method = *m_ptr;
  } else {
    fprintf(stderr, "ERROR: Invalid HTTP method '%s'.\n", method_tok);
  }

  // parse route
  char *route_tok = strtok(NULL, " ");
  strncpy(header->route_path, route_tok, PATH_BUF_SZ);
}

// note; the compiler will run through the strlen on a static string at
// compile-time. doing strlen here is 0 overhead.
#define IS_SAME(str1, str_lit) (strncmp(str1, str_lit, strlen(str_lit)) == 0)

void parse_line(HTTPHeader *header, char *line) {
  int len = strlen(line);
  for (int i = 0; i < len; i++) {
    char *key_tok = strtok(line, " ");

    if (IS_SAME(key_tok, "Connection:")) {
      char *arg_tok = line + strlen(line) + 1;
      if (IS_SAME(arg_tok, "keep-alive")) {
        header->connection_close = 0;
      } else if (IS_SAME(arg_tok, "close")) {
        header->connection_close = 1;
      }
    }
  }
}

void parse_header(HTTPHeader *header, char *request) {
  int len = strlen(request);
  char line[256];
  int line_idx = 0;

  int num_lines_parsed = 0;

  for (int i = 0; i < len; i++) {
    char ch = request[i];
    switch (ch) {
    case '\n': {
      if (request[i + 1] == '\n') {
        break; // the blank line means we're done parsing the header.
      }
      // flush the buffer and parse the line as a single unit.
      line[line_idx++] = '\0';
      line_idx = 0;
      if (num_lines_parsed == 0) {
        // the method line will always be on the first line, as per http1.1
        parse_method_line(header, line);
      } else {
        parse_line(header, line);
      }
      num_lines_parsed++;
    } break;
    default: {
      line[line_idx++] = ch;
    } break;
    }
  }
}

#define RESPONSE_HEADER_BUF_SZ 1024

int status_line(char *response_buf, int buf_sz_remaining,
                HTTPStatusCode status) {
  int n = snprintf(response_buf, buf_sz_remaining, "HTTP/1.1 %d %s\r\n", status,
                   get_reason_phrase(status));
  return n;
}

int content_type(char *response_buf, int buf_sz_remaining, ContentType c_type) {
  int n = snprintf(response_buf, buf_sz_remaining, "Content-Type: %s\r\n",
                   get_content_type_string(c_type));
  return n;
}

int content_length(char *response_buf, int buf_sz_remaining,
                   unsigned long length) {
  int n = snprintf(response_buf, buf_sz_remaining, "Content-Length: %lu\r\n",
                   length);
  return n;
}

int newline(char *response_buf, int buf_sz_remaining) {
  int n = snprintf(response_buf, buf_sz_remaining, "\r\n");
  return n;
}

int client_send_file(const char *file_path, int client_socket_fd) {
  int file_fd = open(file_path, O_RDONLY);
  if (file_fd == -1) {
    perror("open");
    // Handle error
    return -1;
  }

  struct stat file_stat;
  if (fstat(file_fd, &file_stat) == -1) {
    perror("fstat");
    // Handle error
    close(file_fd);
    return -1;
  }

  char header_buf[RESPONSE_HEADER_BUF_SZ];
  char *_header = header_buf;
  int remaining = RESPONSE_HEADER_BUF_SZ;

#define APPEND(fn, ...)                                                        \
  {                                                                            \
    int n = fn(_header, remaining, ##__VA_ARGS__);                             \
    _header += n;                                                              \
    remaining -= n;                                                            \
  }

  APPEND(status_line, OK);
  // then, choose a content-type based on the file extension.
  ContentType c_type;
  char *dot = strrchr(file_path, '.');
  if (dot == NULL) {
  } else {
    char *ext = dot + 1;
    if (IS_SAME(ext, "js")) {
      c_type = APPLICATION_JAVASCRIPT;
    } else if (IS_SAME(ext, "json")) {
      c_type = APPLICATION_JSON;
    } else if (IS_SAME(ext, "css")) {
      c_type = TEXT_CSS;
    } else if (IS_SAME(ext, "html")) {
      c_type = TEXT_HTML;
    } else {
      c_type = TEXT_PLAIN;
    }
  }
  APPEND(content_type, c_type);
  APPEND(content_length, file_stat.st_size);
  APPEND(newline);

#undef APPEND

  // we can write to the socket in multiple calls, one for the http header and
  // one for the file. here, make the header.

  // send is basically just write with more stuff around it that makes it better
  // for direct socket communication. for example, it's got the options param as
  // the fourth that allows better control over the actual fd write.
  send(client_socket_fd, header_buf, RESPONSE_HEADER_BUF_SZ - remaining, 0);

  // be super efficient and use the proper linux syscalls instead of reading
  // and writing into a buffer just to send an entire file.
  ssize_t bytes_sent =
      sendfile(client_socket_fd, file_fd, NULL, file_stat.st_size);

  if (bytes_sent == -1) {
    perror("sendfile");
    // Handle error
    return -1;
  }

  return 0;
}

int client_send_error(int client_socket, const char *message) {
  char http_error_response[1024];

  int n = snprintf(http_error_response, sizeof(http_error_response),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/html; charset=UTF-8\r\n\r\n"
                   "<p>ERROR: %s</p>",
                   message);

  ssize_t bytes_sent = send(client_socket, http_error_response, n, 0);

  if (bytes_sent == -1) {
    perror("send");
    return -1;
  }

  return 0;
}

void *handle_client_thread(void *data) {
  Client *c = (Client *)data;

  signal(SIGSEGV, sigsegv);

  for (;;) {
    char buffer[2048];
    ssize_t bytes_read = recv(c->socket, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read < 1) {
      if (bytes_read == 0) {
        printf("Connection closed by client\n");
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        printf("recv() timed out\n");
      } else {
        perror("recv other error");
      }
      break;
    }

    buffer[bytes_read] = '\0';

    printf("Received request:\n%s\n", buffer);
    HTTPHeader h;
    parse_header(&h, buffer);

    // do general header handling.
    switch (h.method) {
    case GET: {
      const char *path;
      printf("GET to '%s'\n", h.route_path);
      path = w_cm_get(&route_alias_map, h.route_path);
      if (path == NULL) {
        path = h.route_path;
      } else {
        printf("aliasing '%s' to '%s'\n", h.route_path, path);
      }

      // otherwise, just send the whole file itself by default.
      // skip the first / and do it relative to the server's pwd.
      if (client_send_file(path + 1, c->socket) != 0) {
        client_send_error(c->socket, "could not send file.");
      }
    } break;
    default: {
      client_send_error(c->socket, "incorrect method.");
    } break;
    }

    // close the connection AFTER the current request has been handled.
    if (h.connection_close) {
      break;
    }
  }

  // always close the connection at the end of the thread.
  close(c->socket);
  return NULL;
}

WArray client_array;

static int server_socket;

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <port>\n", argv[0]);
    exit(1);
  }
  const int port = atoi(argv[1]);

  signal(SIGSEGV, sigsegv);

  w_create_cm(&route_alias_map, PATH_BUF_SZ, 509);

#define INSERT_ALIAS(route_alias_from, route_alias_to)                         \
  { w_cm_insert(&route_alias_map, route_alias_from, route_alias_to); }

  INSERT_ALIAS("/", "/pages/index.html");
  INSERT_ALIAS("/about", "/pages/about.html");

#undef INSERT_ALIAS

  w_create_cm(&method_map, sizeof(HTTPMethod), 509);
#define INSERT_METHOD(method_lit, method_variant)                              \
  {                                                                            \
    HTTPMethod m = method_variant;                                             \
    w_cm_insert(&method_map, method_lit, &m);                                  \
  }

  INSERT_METHOD("GET", GET);

#undef INSERT_METHOD

  w_make_array(&client_array, sizeof(Client), MAX_CLIENTS);

  int client_socket;
  struct sockaddr_in server_address, client_address;
  socklen_t client_len = sizeof(client_address);

  server_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (server_socket == -1) {
    perror("socket");
    exit(1);
  }

  // make it impossible for the server socket to orphan itself and stick on the
  // port.
  int enable = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(int)) < 0) {
    perror("setsockopt(SO_REUSEADDR) failed");
    exit(1);
  }

  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(port);

  if (bind(server_socket, (struct sockaddr *)&server_address,
           sizeof(server_address)) == -1) {
    perror("bind");
    exit(1);
  }

  if (listen(server_socket, 10) == -1) {
    perror("listen");
    exit(1);
  }

  printf("Listening on port %d...\n", port);

  while (1) {
    client_socket =
        accept(server_socket, (struct sockaddr *)&client_address, &client_len);

    if (client_socket == -1) {
      perror("accept");
      exit(1);
    }

    struct timeval timeout;
    timeout.tv_sec = 60; // 60 seconds timeout
    timeout.tv_usec = 0;

    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                   sizeof(timeout)) < 0) {
      perror("setsockopt");
    }

    if (setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                   sizeof(timeout)) < 0) {
      perror("setsockopt");
    }

    Client *c = w_array_alloc(&client_array);
    c->socket = client_socket;
    pthread_create(&c->thread, NULL, handle_client_thread, c);
    handle_client_thread(&c);
  }

  clean_main();

  return 0;
}

static int has_cleaned = 0;

void clean_main() {
  // multiple different threads might call this.
  if (has_cleaned) {
    return;
  }

  has_cleaned++;

  if (close(server_socket) == -1) {
    perror("Failed to clean server socket");
  }

  for (int i = 0; i < client_array.upper_bound; i++) {
    Client *c = w_array_get(&client_array, i);
    if (c)
      close(c->socket);
  }
}
