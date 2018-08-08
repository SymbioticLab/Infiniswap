/*
 * Infiniswap, remote memory paging over RDMA
 * Copyright 2017 University of Michigan, Ann Arbor
 * GPLv2 License
 */

#include "rdma-common.h"

static int on_connect_request(struct rdma_cm_id *id);
static int on_connection(struct rdma_cm_id *id);
static int on_disconnect(struct rdma_cm_id *id);
static int on_event(struct rdma_cm_event *event);
static void usage(const char *argv0);
long page_size;
int running;
int main(int argc, char **argv)
{
  struct sockaddr_in6 addr;
  struct rdma_cm_event *event = NULL;
  struct rdma_cm_id *listener = NULL;
  struct rdma_event_channel *ec = NULL;
  uint16_t port = 0;
  pthread_t free_mem_thread;

  if (argc != 3)
    usage(argv[0]);
  page_size = sysconf(_SC_PAGE_SIZE);

  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, argv[1], &addr.sin6_addr);
  addr.sin6_port = htons(atoi(argv[2]));

  TEST_Z(ec = rdma_create_event_channel());
  TEST_NZ(rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP));
  TEST_NZ(rdma_bind_addr(listener, (struct sockaddr *)&addr));
  TEST_NZ(rdma_listen(listener, 10)); /* backlog=10 is arbitrary */

  port = ntohs(rdma_get_src_port(listener));

  printf("listening on port %d.\n", port);

  //free
  running = 1;
  TEST_NZ(pthread_create(&free_mem_thread, NULL, (void *)free_mem, NULL));
  pthread_create(&control_msg_listen_thread, NULL, (void *)control_msg_listen, NULL);

  while (rdma_get_cm_event(ec, &event) == 0)
  {
    printf("rdma_get_cm_event\n");
    struct rdma_cm_event event_copy;

    memcpy(&event_copy, event, sizeof(*event));
    rdma_ack_cm_event(event);

    if (on_event(&event_copy))
      break;
  }

  rdma_destroy_id(listener);
  rdma_destroy_event_channel(ec);

  return 0;
}

int control_msg_listen()
{
  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock == -1)
  {
    cerr << "cannot open stream socket!\n";
    exit(1);
  }

  int on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  //bind ip and port number to the socket
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(server_ip);
  addr.sin_port = htons(hostport);
  bind(sock, (struct sockaddr *)&addr, sizeof(addr));
  socklen_t length = sizeof(addr);
  if (getsockname(sock, (struct sockaddr *)&addr, &length) == -1)
  {
    exit(1);
  }

  listen(sock, 10);

  while (1)
  {
    int msgsock = accept(sock, (struct sockaddr *)0, (socklen_t *)0);
    if (msgsock == -1)
    {
      printf("Error: accept socket connection\n");
    }
    else
    {
      control_msg msg;
      recv(msgsock, &msg, sizeof(msg), MSG_WAITALL);
      printf("Receive control message: %s\n", msg.cmd);
    }
  }

  return sock;
}

int on_connect_request(struct rdma_cm_id *id)
{
  struct rdma_conn_param cm_params;

  printf("received connection request.\n");
  struct connection *conn = build_connection(id);
  build_params(&cm_params);
  TEST_NZ(rdma_accept(id, &cm_params));

  //get connection ip
  struct sockaddr *dst_addr = rdma_get_peer_addr(id);
  struct sockaddr_in *dst_in = (struct sockaddr_in *)dst_addr;
  char *dst_ip = inet_ntoa(dst_in->sin_addr);
  if (conn)
  {
    printf("conn: %d, ip address: %s\n", conn->conn_index, dst_ip);
    strcpy(conn->bd_ip, dst_ip);
  }

  return 0;
}

int on_connection(struct rdma_cm_id *id)
{
  on_connect(id->context);

  printf("connection build\n");
  /* J: only server send mr, client doesn't */
  send_free_mem_size(id->context);

  return 0;
}

int on_disconnect(struct rdma_cm_id *id)
{
  printf("peer disconnected.\n");

  destroy_connection(id->context);
  return 0;
}

int on_event(struct rdma_cm_event *event)
{
  int r = 0;

  if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST)
    r = on_connect_request(event->id);
  else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
    r = on_connection(event->id);
  else if (event->event == RDMA_CM_EVENT_DISCONNECTED)
    r = on_disconnect(event->id);
  else
    die("on_event: unknown event.");

  return r;
}

void usage(const char *argv0)
{
  fprintf(stderr, "usage: %s ip port\n", argv0);
  exit(1);
}
