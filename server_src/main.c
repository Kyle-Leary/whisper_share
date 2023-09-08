#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "index.html.h"

#define PATH_BUF_SZ 256

typedef enum HTTPMethod {
  POST,
  GET,
  DELETE,
  PUT,
} HTTPMethod;

typedef struct HTTPHeader {
  HTTPMethod method;
  char route[PATH_BUF_SZ];
} HTTPHeader;

void parse_method_line(HTTPHeader *header, char *method_line) {
  int len = strlen(method_line);
  char *method_tok = strtok(method_line, " ");
  char *route_tok = strtok(NULL, " ");
  char *protocol_tok = strtok(NULL, " ");
}

void parse_line(HTTPHeader *header, char *line) {
  int len = strlen(line);
  for (int i = 0; i < len; i++) {
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

void handle_client(int client_socket) {
  char buffer[2048];
  ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

  if (bytes_read < 1) {
    perror("recv");
    close(client_socket);
    return;
  }

  buffer[bytes_read] = '\0';

  printf("Received request:\n%s\n", buffer);
  HTTPHeader h;
  parse_header(&h, buffer);

  char http_response[] =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=UTF-8\r\n\r\n" HTML_INDEX;

  send(client_socket, http_response, sizeof(http_response), 0);
  close(client_socket);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <port>\n", argv[0]);
    exit(1);
  }
  const int port = atoi(argv[1]);

  int server_socket, client_socket;
  struct sockaddr_in server_address, client_address;
  socklen_t client_len = sizeof(client_address);

  server_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (server_socket == -1) {
    perror("socket");
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

    handle_client(client_socket);
  }

  return 0;
}
