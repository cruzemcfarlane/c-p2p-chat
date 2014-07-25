#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <locale.h>
#include <math.h>
#include "uthash.h"
#include "message.h"
#define MAX_NUM_ROOMS 5
#define MAX_ROOM_SIZE 5

struct peer {
  char ip_and_port[20];
  unsigned int room;
  UT_hash_handle hh;
};

// Globals
struct peer *peers;
int sock;

// Function Prototypes
short parse_args(int argc, char **argv);
void peer_create_room(unsigned int ip, short port);
void peer_join(unsigned int ip, short port, unsigned int room);
void peer_leave(unsigned int ip, short port);
void room_list(unsigned int ip, short port);
void peer_list(unsigned int join_ip, short join_port, unsigned int room);
void send_error(unsigned int ip, short port, char type, char error);
int get_total_num_rooms();
sockaddr_in get_sockaddr_in(unsigned int ip, short port);
unsigned int get_ip(char* ip_port);
short get_port(char* ip_port);
void test_hash_table();

int main(int argc, char **argv){
  //setup hashtable
  peers = NULL;
  fprintf(stderr, "Hashtable created\n");
  //read input
  short port = parse_args(argc, argv);
  fprintf(stderr, "Starting server on port: %d\n", port);

  //setup UDP socket
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    fprintf(stderr, "%s\n", "error - error creating socket.");
    abort();
  }
  struct sockaddr_in self_addr;
  self_addr.sin_family = AF_INET; 
  self_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  self_addr.sin_port = htons(port);
  if (bind(sock, (struct sockaddr *)&self_addr, sizeof(self_addr))) {
    fprintf(stderr, "%s\n", "error - error binding.");
    abort();
  }
  socklen_t addrlen = 10;
  struct sockaddr_in sender_addr;
  packet recv_pkt;
  int recv_status;

  while(1){
    recv_status = recvfrom(sock, &recv_pkt, sizeof(recv_pkt), 0, (struct sockaddr *)&sender_addr, &addrlen);
    if (recv_status == -1) {
      fprintf(stderr, "%s\n", "error - error receiving a packet, ignoring.");
    }else{
      fprintf(stderr, "%s\n", "handle request...");
      unsigned int ip = sender_addr.sin_addr.s_addr;
      short port = htons(sender_addr.sin_port);
      fprintf(stderr, "%c\n", recv_pkt.header.type);
      switch (recv_pkt.header.type) {
        case 'c': 
          peer_create_room(ip, port);
          break;
        case 'j':
          peer_join(ip, port, recv_pkt.header.room);
          break;
        case 'l':
          peer_leave(ip, port);
          break;
        case 'r':
          room_list(ip, port);
          break;
        default:
          fprintf(stderr, "%s\n", "error - received packet type unknown.");
          break;
      }
    }
  }
  return 0;
}

void peer_create_room(unsigned int ip, short port){
  //check if room limit reached
  int number_of_rooms = get_total_num_rooms();
  if(number_of_rooms>=MAX_NUM_ROOMS){
    send_error(ip, port, 'c', 'o');
    perror("Peer create room failed - max number of rooms reached.\n");
    return;
  }

  //get a room number
  struct peer *s;
  int room_taken = 0;
  unsigned int room;
  for(room=1; room<MAX_NUM_ROOMS*2; room++){
    for(s=peers; s != NULL; s=(struct peer *)s->hh.next){
      if(s->room == room){
        room_taken=1;
        break;
      }
    }
    if(room_taken==0){
      break;
    }else{
      room_taken=0;
    }
  }

  //create entry
  struct peer *new_peer;
  new_peer = (struct peer *)malloc(sizeof(struct peer));
  char* ip_and_port_format = (char *)"%d:%d";
  sprintf(new_peer->ip_and_port, ip_and_port_format, ip, port);
  new_peer->room = room;

  //check if peer in a room
  HASH_FIND_STR(peers, (new_peer->ip_and_port), s);
  if(s!=NULL){
    free(new_peer);
    perror("Peer create room failed - already in a room.\n");
    send_error(ip, port, 'c', 'e');
  }else{
    HASH_ADD_STR( peers, ip_and_port, new_peer );  //create room - add peer
    fprintf(stderr, "%s created %d\n", new_peer->ip_and_port, room);

    packet pkt;
    pkt.header.type = 'c';
    pkt.header.error = '\0';
    pkt.header.room = room;
    pkt.header.payload_length = 0;
    struct sockaddr_in peer_addr = get_sockaddr_in(ip, port);
    int status = sendto(sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    if (status == -1) {
      fprintf(stderr, "%s\n", "error - error sending packet to peer");
    }
  }
}

void peer_join(unsigned int ip, short port, unsigned int room){
  struct peer *s;
  int r=0;
  int room_exists = 0;
  for(s=peers; s != NULL; s=(struct peer *)s->hh.next){
    if(s->room == room){
      r = r+1;
      if(r>=MAX_ROOM_SIZE){
        perror("Peer join failed - room full.\n");
        send_error(ip, port, 'j', 'f');
        return;
      }
      room_exists = 1;
    }
  }
  if (room_exists==0){
    perror("Peer join failed - room does not exist.\n");
    send_error(ip, port, 'j', 'e');
    return;
  }
  
  //setup entry
  struct peer *new_peer;
  new_peer = (struct peer *)malloc(sizeof(struct peer));
  char* ip_and_port_format = (char *)"%d:%d";
  sprintf(new_peer->ip_and_port, ip_and_port_format, ip, port);
  new_peer->room = room;
  

  HASH_FIND_STR(peers, (new_peer->ip_and_port), s);
  if(s!=NULL && s->room==room){
    perror("Peer join failed - already in room.\n");
    send_error(ip, port, 'j', 'a');
    return;
  }
  int old_room_update = -1;
  if(s==NULL){ 
    //peer not found - join

    HASH_ADD_STR( peers, ip_and_port, new_peer );
  }else{
    //peer found - switch
    old_room_update = s->room;
    HASH_REPLACE_STR( peers, ip_and_port, new_peer, s );
  }
  if(old_room_update!=-1){
    fprintf(stderr, "%s peer switched from %d to %d.\n", new_peer->ip_and_port, old_room_update, room);
    peer_list(ip, port, room);
    peer_list(0, -1, old_room_update);
  }else{
    fprintf(stderr, "%s joined %d\n", new_peer->ip_and_port, room);
    peer_list(ip, port, room);
  }
}

void peer_leave(unsigned int ip, short port){
  char ip_port[20];
  memset(ip_port, 0, sizeof(ip_port));
  char* ip_and_port_format = (char *)"%d:%d";
  sprintf(ip_port, ip_and_port_format, ip, port);
  struct peer *s;
  HASH_FIND_STR(peers, ip_port, s);
  if(s!=NULL){
    unsigned int left_room = s->room;
    HASH_DEL( peers, s);
    free(s);
    fprintf(stderr, "%s left %d\n", ip_port, left_room);

    packet pkt;
    pkt.header.type = 'l';
    pkt.header.error = '\0';
    pkt.header.payload_length = 0;
    struct sockaddr_in peer_addr = get_sockaddr_in(ip, port);
    int status = sendto(sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    if (status == -1) {
      fprintf(stderr, "%s\n", "error - error sending packet to peer");
    }
    peer_list(0, -1, left_room);
  }else{
    send_error(ip, port, 'l', 'e');
  }
}

void room_list(unsigned int ip, short port){
  int number_of_rooms = get_total_num_rooms();
  unsigned int room_indexed[number_of_rooms];
  memset(room_indexed, 0, number_of_rooms * sizeof(room_indexed[0]));
  unsigned int room_nums[number_of_rooms];
  memset(room_nums, 0, number_of_rooms * sizeof(room_nums[0]));
  int room_stats[number_of_rooms];
  memset(room_stats, 0, number_of_rooms * sizeof(room_stats[0]));
  struct peer *s;
  int max_occupants=0;
  unsigned int max_room_number =0;
  int num_rooms_indexed = 0;
  for(s=peers; s != NULL; s=(peer *)s->hh.next){
    int room_index=-1;
    unsigned int a;
    for(a=0; a<sizeof(room_nums)/sizeof(room_nums[0]); a++){
      if(room_nums[a]==s->room){
        room_index=a;
        break;
      }
    }
    if(room_indexed[room_index]==0){
      room_index = num_rooms_indexed;
      room_nums[room_index] = s->room;
      room_stats[room_index] = 1;
      room_indexed[room_index]= 1;
      num_rooms_indexed = num_rooms_indexed+1;
    }else{
      room_stats[room_index] = room_stats[room_index]+1;
    }
    if(room_stats[room_index]>max_occupants){
      max_occupants=room_stats[room_index];
    }
    if(room_nums[room_index]>max_room_number){
      max_room_number=room_nums[room_index];
    }
  }

  int max_room_num_len;
  if(max_room_number==0){
    max_room_num_len=1;
  }else{
    max_room_num_len=(int)floor(log10((float)max_room_number)) + 1;
  }
  int max_room_size_len = (int)floor(log10((float)MAX_ROOM_SIZE)) + 1;
  int max_num_room_len = (int)floor(log10((float)max_occupants)) + 1;
  char *list_entry_format = (char *)"room: %d - %d/%d\n";
  int list_entry_size = max_room_num_len+max_num_room_len+max_room_size_len+strlen(list_entry_format);
  int list_size = max_num_room_len*list_entry_size;
  char *list_entry = (char *)malloc(list_entry_size);
  char *list = (char *)malloc(list_size);
  unsigned int i;
  char *list_i = list;
  for(i=0; i<sizeof(room_stats)/sizeof(room_stats[0]); i++){
    sprintf(list_entry, list_entry_format, room_nums[i], room_stats[i], MAX_ROOM_SIZE);
    strcpy(list_i, list_entry);
    list_i += strlen(list_entry);
  }
  if(number_of_rooms==0){
    list=(char*)"There are no chatrooms\n";
  }
  fprintf(stderr, "room list\n%s\n", list);

  packet pkt;
  pkt.header.type = 'r';
  pkt.header.error = '\0';
  pkt.header.payload_length = list_size;
  strcpy(pkt.payload, list);
  struct sockaddr_in peer_addr = get_sockaddr_in(ip, port);
  int status = sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
  if (status == -1) {
    fprintf(stderr, "%s\n", "error - error sending packet to peer");
  }
}

//when you need to handle a join reply, use the params join_ip and join_port
//otherwise, join_ip=0 and join_port=-1
void peer_list(unsigned int join_ip, short join_port, unsigned int room){
  //create payload for join and update replies
  struct peer *s;
  int num_in_room = 0;
  for(s=peers; s != NULL; s=(peer *)s->hh.next){
    if(s->room==room){
      num_in_room = num_in_room+1;
    }
  }
  char *list = (char *)malloc(num_in_room * sizeof(struct sockaddr_in));
  char *list_i = list;
  for(s=peers; s != NULL; s=(peer *)s->hh.next){
    if(s->room==room){
      unsigned int peer_ip = get_ip(s->ip_and_port);
      short peer_port = get_port(s->ip_and_port);
      struct sockaddr_in peer_info = get_sockaddr_in(peer_ip, peer_port);
      memcpy(list_i, (char*)&peer_info, sizeof(struct sockaddr_in));
      list_i += sizeof(struct sockaddr_in);
    }
  }

  packet update_pkt;
  update_pkt.header.type = 'u';
  update_pkt.header.error = '\0';
  update_pkt.header.payload_length = num_in_room * sizeof(struct sockaddr_in);
  strcpy(update_pkt.payload, list);
  for(s=peers; s != NULL; s=(peer *)s->hh.next){
    if(s->room==room){
      unsigned int peer_ip = get_ip(s->ip_and_port);
      short peer_port = get_port(s->ip_and_port);
      if(join_port!=-1 and join_ip!=0 and peer_ip == join_ip and peer_port==join_port){
        //send join
        packet join_pkt;
        join_pkt.header.type = 'j';
        join_pkt.header.error = '\0';
        join_pkt.header.payload_length = num_in_room * sizeof(struct sockaddr_in);
        strcpy(join_pkt.payload, list);
        struct sockaddr_in peer_addr = get_sockaddr_in(peer_ip, peer_port);
        int status = sendto(sock, &join_pkt, sizeof(join_pkt), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
        if (status == -1) {
          fprintf(stderr, "%s\n", "error - error sending packet to peer");
        }
      }else{
        struct sockaddr_in peer_addr = get_sockaddr_in(peer_ip, peer_port);
        int status = sendto(sock, &update_pkt, sizeof(update_pkt), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
        if (status == -1) {
          fprintf(stderr, "%s\n", "error - error sending packet to peer");
        }
      }
    }
  }
}

short parse_args(int argc, char **argv){
  if (argc < 2)
  {
    return 8080;
  }
  else
  {
    errno = 0;
    char *endptr = NULL;
    unsigned long ulPort = strtoul(argv[1], &endptr, 10);

    if (0 == errno)
    {
      // If no other error, check for invalid input and range
      if ('\0' != endptr[0])
        errno = EINVAL;
      else if (ulPort > USHRT_MAX)
        errno = ERANGE;
    }
    if (0 != errno)
    {
      // Report any errors and abort
      fprintf(stderr, "Failed to parse port number \"%s\": %s\n",
              argv[1], strerror(errno));
      abort();
    }
    return ulPort;
  }
}

int get_total_num_rooms(){
  int total = 0;
  unsigned int room_found[MAX_NUM_ROOMS];
  memset(room_found, 0, MAX_NUM_ROOMS * sizeof(room_found[0]));
  unsigned int rooms[MAX_NUM_ROOMS];
  memset(rooms, 0, MAX_NUM_ROOMS * sizeof(rooms[0]));

  struct peer *s;
  unsigned int a;
  for(s=peers; s != NULL; s=(peer *)s->hh.next){
    int found = 0;
    for(a=0; a<sizeof(rooms)/sizeof(rooms[0]); a++){
      if(rooms[a]==s->room && room_found[a]==1){
        found=1;
        break;
      }
    }
    if(found==0){
      room_found[total]=1;
      rooms[total]=s->room;
      total=total+1;
    }
  }

  return total;
}

void send_error(unsigned int ip, short port, char type, char error){
  packet pkt;
  pkt.header.type = type;
  pkt.header.error = error;
  pkt.header.payload_length = 0;

  struct sockaddr_in peer_addr = get_sockaddr_in(ip, port);

  int status = sendto(sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
  if (status == -1) {
    fprintf(stderr, "%s\n", "error - error sending packet to peer");
  }
}

sockaddr_in get_sockaddr_in(unsigned int ip, short port){
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ip;
  addr.sin_port = htons(port);
  return addr;
}

unsigned int get_ip(char* ip_port){
  int i;
  for(i=0; i<20; i++){
    if(ip_port[i]==':'){
      break;
    }
  }
  char char_ip[i+1];
  strncpy(char_ip, ip_port, i);
  char_ip[i] = '\0';

  return (unsigned int)strtoul(char_ip, NULL, 0);
}

short get_port(char* ip_port){
  int i;
  int start=-1;
  int end=-1;
  for(i=0; i<20; i++){
    if(start==-1 && ip_port[i]==':'){
      start=i+1;
    }
    if(ip_port[i]=='\0'){
      end=i;
      break;
    }
  }

  char char_short[end-start+1];
  strncpy(char_short, ip_port+start, end-start);
  char_short[end-start] = '\0';
  
  return (short)atoi(char_short);
}
