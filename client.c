#define _GNU_SOURCE
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <sched.h>
#include <assert.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdbool.h>

#define NUM_HASHTABLE 2 // number of filtering table
#define MAX_WORKERS 16
#define NETCLONE_BASE_PORT 1000
#define NOCLONE_BASE_PORT 2000
#define LAEDGE_BASE_PORT 3000
#define MAX_SRV 6
#define NOCLONE 0
#define CLICLONE 1 // C-Clone
#define LAEDGE 2
#define NETCLONE 3
#define OP_READ 0
#define OP_R_REPLY 1
#define OP_WRITE 2
#define OP_W_REPLY 3
#define MAX_REQUESTS 100000000
#define NUM_CLI 2
#define COORDINATOR_ID 0 // server ID of LAEDGE coordinator
pthread_mutex_t lock_filter_read = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_filter = PTHREAD_MUTEX_INITIALIZER;
bool* redundnacy_filter;

void initialize_filter() {
    redundnacy_filter = (bool*) malloc((MAX_REQUESTS)* sizeof(bool));
}

int local_pkt_counter[MAX_WORKERS] = {0,};
int global_pkt_counter = 0;

pthread_mutex_t lock_counter = PTHREAD_MUTEX_INITIALIZER;
uint32_t global_load_counter  = 0;

pthread_mutex_t lock_txid = PTHREAD_MUTEX_INITIALIZER;
int tx_id = 0;
pthread_mutex_t lock_rxid = PTHREAD_MUTEX_INITIALIZER;
int rx_id = 0;
pthread_mutex_t lock_create = PTHREAD_MUTEX_INITIALIZER;





int combination(int n, int k) {
  return (int) (round(exp(lgamma(n + 1) - lgamma(k + 1) - lgamma(n - k + 1))));
}

int get_server_id(char *interface){
    int fd;
    struct ifreq ifr;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    /* I want to get an IPv4 IP address */
    ifr.ifr_addr.sa_family = AF_INET;
    /* I want IP address attached to interface */
    strncpy(ifr.ifr_name, interface, IFNAMSIZ-1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);
		/* display only the last number of the IP address */
		char* ip = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
    return (ip[strlen(ip) - 1] - '0');
}

void pin_to_cpu(int core){
	int ret;
	cpu_set_t cpuset;
	pthread_t thread;

	thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(core, &cpuset);
	ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  if (ret != 0){
	    printf("Cannot pin thread. may be too many threads? \n");
			exit(1);
    }
}

uint64_t get_cur_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t t = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
  return t;
}

#pragma pack(1)
struct netclone_hdr{
  uint32_t op;
  uint32_t seq;
  uint32_t grp;
  uint32_t sid;
  uint32_t load;
  uint32_t clo;
  uint32_t tidx;
  uint64_t latency;
	struct sockaddr_in cli_addr;
} __attribute__((packed));

#pragma pack(1)
struct noclone_hdr{
  uint32_t op;
  uint32_t key;
	uint64_t latency;
	struct sockaddr_in cli_addr;
} __attribute__((packed));


#pragma pack(1)
struct cliclone_hdr{
  uint32_t op;
  uint32_t key;
	uint32_t seq;
	uint64_t latency;
	struct sockaddr_in cli_addr;
} __attribute__((packed));

#pragma pack(1)
struct laedge_hdr{
  uint32_t op;
  uint32_t key;
	uint32_t seq;
	uint64_t latency;
  uint32_t cli_id;
  uint32_t srv_id;
  uint16_t cli_port;
  struct sockaddr_in cli_addr;
} __attribute__((packed));


struct arg_t {
  int sock;
  struct sockaddr_in srv_addr;
  uint64_t NUM_SRV;
  uint64_t TARGET_QPS;
  uint64_t NUM_REQUESTS;
  uint64_t NUM_WORKERS;
  uint64_t NUM_WORKERS_SRV;
	int PROTOCOL_ID;
	int SERVER_ID;
	int DIST;
  uint64_t TIME_EXP;
} __attribute__((packed));

void *tx_t(void *arg){
  pthread_mutex_lock(&lock_txid);
	int i = tx_id++;
	pthread_mutex_unlock(&lock_txid);
	pin_to_cpu(i);
  struct arg_t *args = (struct arg_t *)arg;
  struct sockaddr_in srv_addr = args->srv_addr;

  srand(time(NULL)); // rand함수용 시드 초기화
	int cli_addr_len = 0;
  int n = 0;
  uint64_t elapsed_time = get_cur_ns();
	uint64_t temp_time = get_cur_ns();
  uint64_t inter_arrival_time = 0;
	int sock = args->sock;
	printf("Tx Worker %d is running with Socket %d \n",i,sock);
	uint64_t NUM_WORKERS = args->NUM_WORKERS;
	uint64_t NUM_WORKERS_SRV = args->NUM_WORKERS_SRV;
  uint64_t NUM_SRV = args->NUM_SRV;
	uint64_t NUM_REQUESTS = args->NUM_REQUESTS/NUM_WORKERS;
  uint64_t TARGET_QPS = RAND_MAX*args->TARGET_QPS/1000/2/NUM_WORKERS;
  uint64_t counter = 0;
	int PROTOCOL_ID = args->PROTOCOL_ID;
	int DIST = args->DIST;
  uint64_t SERVER_ID = args->SERVER_ID;
  /* Configure IP address */

  char* src_ip[NUM_CLI];
  src_ip[0] = "10.0.1.101";
  src_ip[1] = "10.0.1.102";

  char* dst_ip[MAX_SRV];

  if(PROTOCOL_ID == NETCLONE && NUM_SRV == 5){
    dst_ip[0] = "10.0.1.104";
    dst_ip[1] = "10.0.1.105";
    dst_ip[2] = "10.0.1.106";
    dst_ip[3] = "10.0.1.107";
    dst_ip[4] = "10.0.1.108";
    //dst_ip[5] = "10.0.1.108";
  }
  else if(PROTOCOL_ID == CLICLONE && NUM_SRV == 5){
    dst_ip[0] = "10.0.1.104";
    dst_ip[1] = "10.0.1.105";
    dst_ip[2] = "10.0.1.106";
    dst_ip[3] = "10.0.1.107";
    dst_ip[4] = "10.0.1.108";
    //dst_ip[5] = "10.0.1.108";
  }
  else{
    dst_ip[0] = "10.0.1.103";
    dst_ip[1] = "10.0.1.104";
    dst_ip[2] = "10.0.1.105";
    dst_ip[3] = "10.0.1.106";
    dst_ip[4] = "10.0.1.107";
    dst_ip[5] = "10.0.1.108";
  }

  int FIRST_SRV = 0;
  int SECOND_SRV = 0;
  int NUM_GRP=combination(NUM_SRV,2)*2;
	while(1){
    inter_arrival_time = (uint64_t)(-log(1.0 - ((double)rand() / TARGET_QPS)) * 1000000) ;
    temp_time+=inter_arrival_time;
    while (get_cur_ns() < temp_time)
      ;

    if(PROTOCOL_ID == NOCLONE){

      inet_pton(AF_INET, dst_ip[rand()%NUM_SRV], &srv_addr.sin_addr);
      srv_addr.sin_port = htons(NOCLONE_BASE_PORT);
      struct noclone_hdr SendBuffer={0,};
      SendBuffer.op = htonl(OP_READ);
      SendBuffer.latency = get_cur_ns();

      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));
    }
    else if(PROTOCOL_ID == CLICLONE){
      struct cliclone_hdr SendBuffer={0,};
      SendBuffer.op = htonl(OP_READ);
      pthread_mutex_lock(&lock_counter);
      uint32_t temp = global_load_counter++;
      pthread_mutex_unlock(&lock_counter);
      SendBuffer.seq = temp;
      SendBuffer.latency = get_cur_ns();


      do {
          FIRST_SRV = rand() % NUM_SRV;
          SECOND_SRV = rand() % NUM_SRV;
      } while (FIRST_SRV == SECOND_SRV);

      inet_pton(AF_INET, dst_ip[FIRST_SRV], &srv_addr.sin_addr);
      srv_addr.sin_port = htons(NOCLONE_BASE_PORT);
      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));

      inet_pton(AF_INET, dst_ip[SECOND_SRV], &srv_addr.sin_addr);
      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));

    }
    else if(PROTOCOL_ID == LAEDGE){
      struct laedge_hdr SendBuffer={0,};
      inet_pton(AF_INET, dst_ip[COORDINATOR_ID], &srv_addr.sin_addr);
      srv_addr.sin_port = htons(LAEDGE_BASE_PORT+ rand()%NUM_WORKERS_SRV);
      SendBuffer.cli_id = htonl(SERVER_ID);
      SendBuffer.op = htonl(OP_READ);

      pthread_mutex_lock(&lock_counter);
      uint32_t temp = global_load_counter++;
      SendBuffer.seq = htonl(temp);
      pthread_mutex_unlock(&lock_counter);


      SendBuffer.latency = get_cur_ns();
      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));
    }
    else if(PROTOCOL_ID == NETCLONE){

      inet_pton(AF_INET, dst_ip[rand()%NUM_SRV], &srv_addr.sin_addr);

      srv_addr.sin_port = htons(NETCLONE_BASE_PORT);
      struct netclone_hdr SendBuffer={0,};
      SendBuffer.op = htonl(OP_READ);
      SendBuffer.grp = htonl( rand()%NUM_GRP+1);
      SendBuffer.sid = htonl( rand()%NUM_SRV);
      SendBuffer.tidx = htonl( rand()%NUM_HASHTABLE);

      SendBuffer.latency = get_cur_ns();
      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));

    }
    counter++;
    if(PROTOCOL_ID == CLICLONE){
      counter++;
      if(counter >= NUM_REQUESTS*2) break;
    }
    else{
      if(counter >= NUM_REQUESTS) break;
    }

	}

	double tot_time = (get_cur_ns() - elapsed_time )/1e9;
	double throughput = counter  / tot_time ;
	printf("Tx Worker %d done with %lu reqs, Tx throughput: %d RPS \n",i, counter,(int)throughput);

}

void *rx_t(void *arg){

  pthread_mutex_lock(&lock_rxid);
	int i = rx_id++;
	pthread_mutex_unlock(&lock_rxid);


  struct arg_t *args = (struct arg_t *)arg;
  uint64_t NUM_WORKERS = args->NUM_WORKERS;
	uint64_t NUM_WORKERS_SRV = args->NUM_WORKERS_SRV;
  uint64_t NUM_SRV = args->NUM_SRV;
	pin_to_cpu(i+NUM_WORKERS);

  struct sockaddr_in cli_addr;
  int cli_addr_len = sizeof(cli_addr);
  int counter=0;
	int sock = args->sock;
	printf("Rx Worker %d is running with Socket %d\n",i,sock);
	uint64_t NUM_REQUESTS = args->NUM_REQUESTS;
  uint64_t TIME_EXP = args->TIME_EXP;
	uint64_t TARGET_QPS = args->TARGET_QPS;
	uint64_t PROTOCOL_ID = args->PROTOCOL_ID;
	uint64_t SERVER_ID = args->SERVER_ID;
	uint64_t DIST = args->DIST;

	char log_file_name[40];
	sprintf(log_file_name,"./log-%lu-%lu-%d-%lu-%lu-%lu-%lu-%lu-%lu.txt",PROTOCOL_ID,SERVER_ID,i,NUM_SRV,NUM_WORKERS_SRV,NUM_WORKERS,DIST,TIME_EXP,TARGET_QPS);  // log-ServerID-ThreadID-Protocol-REQUESTS-QPS
	FILE* fd;
	if ((fd = fopen(log_file_name, "w")) == NULL) {
		exit(1);
	}



  uint64_t elapsed_time = get_cur_ns();

	uint64_t timer = get_cur_ns();
	int n = 0;
  int redundnacy_counter = 0;
  double ssss =0;
  while(1){
		if((get_cur_ns() - timer  ) > 1e9 )  break;

    if(PROTOCOL_ID == NOCLONE){
      struct noclone_hdr RecvBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);
      if(n>0){
        if(ntohl(RecvBuffer.op) == OP_R_REPLY){
          fprintf(fd,"%lu\n",(get_cur_ns() - RecvBuffer.latency)/1000);
          local_pkt_counter[i]++;
          timer = get_cur_ns();
        }
      }
    }
    else if(PROTOCOL_ID == CLICLONE){
      struct cliclone_hdr RecvBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);
      if(n>0){
        if(ntohl(RecvBuffer.op) == OP_R_REPLY){

          pthread_mutex_lock(&lock_filter_read);
          bool redun = redundnacy_filter[RecvBuffer.seq];
          pthread_mutex_unlock(&lock_filter_read);
          if (!redun){

      			fprintf(fd,"%lu\n",(get_cur_ns() - RecvBuffer.latency)/1000);
      			local_pkt_counter[i]++;
      			timer = get_cur_ns();
            pthread_mutex_lock(&lock_filter);
            redundnacy_filter[RecvBuffer.seq] = true;
            pthread_mutex_unlock(&lock_filter);
          } else redundnacy_counter++;
        }
      }
    }
    else if(PROTOCOL_ID == LAEDGE){
      struct laedge_hdr RecvBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);
      if(n>0){
        if(ntohl(RecvBuffer.op) == OP_R_REPLY){
    			fprintf(fd,"%lu\n",(get_cur_ns() - RecvBuffer.latency)/1000);
    			local_pkt_counter[i]++;
    			timer = get_cur_ns();
        }
      }
    }
    else if(PROTOCOL_ID == NETCLONE){
      struct netclone_hdr RecvBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);
      if(n>0){
        if(ntohl(RecvBuffer.op) == OP_R_REPLY){
          RecvBuffer.seq = ntohl(RecvBuffer.seq);
          pthread_mutex_lock(&lock_filter_read);
          bool redun = redundnacy_filter[RecvBuffer.seq];
          pthread_mutex_unlock(&lock_filter_read);
          if (!redun){
      			fprintf(fd,"%lu\n",(get_cur_ns() - RecvBuffer.latency)/1000);
      			local_pkt_counter[i]++;
      			timer = get_cur_ns();
            pthread_mutex_lock(&lock_filter);
            redundnacy_filter[RecvBuffer.seq] = true;
            pthread_mutex_unlock(&lock_filter);
          } else redundnacy_counter++;
        }
      }
    }
  }

	double tot_time = ((get_cur_ns() - elapsed_time )/1e9)-1;
	fprintf(fd,"%f\n",tot_time);
	printf("Rx Worker %d finished with %d redundant replies \n",i,redundnacy_counter);
	close(args->sock);
	fclose(fd);
}

int main(int argc, char *argv[]) {
	if ( argc <6){
	 printf("%s Usage: ./client NUM_SRV Protocol Distribution TIME_EXP TARGET_QPS \n", argv[0]);
	 exit(1);
	}

  int NUM_SRV = atoi(argv[1]);
	int NUM_WORKERS_SRV =15;
  int NUM_WORKERS = 1;
	int PROTOCOL_ID = atoi(argv[2]);
	int DIST = atoi(argv[3]);
  int TIME_EXP = atoi(argv[4]);

  uint64_t TARGET_QPS = atoi(argv[5]);
  int NUM_REQUESTS = TARGET_QPS*TIME_EXP;
  if(DIST > 3){
    printf("Distribution cannot exceed 3.\n");
    exit(1);
  }
  initialize_filter();
  pthread_mutex_lock(&lock_filter);
  for (int i = 0; i < MAX_REQUESTS; i++) redundnacy_filter[i] = false;
  pthread_mutex_unlock(&lock_filter);
	char *interface = "enp1s0";
	int SERVER_ID = get_server_id(interface) - 1 ;
  if(SERVER_ID == -1){
    interface = "enp1s0np0";
    SERVER_ID = get_server_id(interface) - 1 ;
  }
  if(SERVER_ID > 255){
    printf("Your server ID is not normal please check your network and server configuration.");
    exit(1);
  }
	else printf("Server %d is running \n",SERVER_ID);

  struct sockaddr_in srv_addr;
  memset(&srv_addr, 0, sizeof(srv_addr)); // Initialize memory space with zeros
  srv_addr.sin_family = AF_INET; // IPv4
	srv_addr.sin_port = htons(NETCLONE_BASE_PORT);

  struct arg_t args[MAX_WORKERS];
	int sock[MAX_WORKERS];
  for(int i=0;i<NUM_WORKERS;i++){
		if ((sock[i] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
			printf("Could not create socket\n");
			exit(1);
		}

	  args[i].sock = sock[i];
	  args[i].srv_addr = srv_addr;
	  args[i].TARGET_QPS = TARGET_QPS;
	  args[i].NUM_REQUESTS = NUM_REQUESTS;
    args[i].NUM_SRV = NUM_SRV;
	  args[i].NUM_WORKERS = NUM_WORKERS;
	  args[i].NUM_WORKERS_SRV = NUM_WORKERS_SRV;
	  args[i].PROTOCOL_ID = PROTOCOL_ID;
	  args[i].SERVER_ID = SERVER_ID;
	  args[i].DIST = DIST;
    args[i].TIME_EXP=TIME_EXP;
	}


  pthread_t tx[MAX_WORKERS],rx[MAX_WORKERS];
	uint64_t elapsed_time = get_cur_ns();
  for(int i=0;i<NUM_WORKERS;i++){
		pthread_mutex_lock(&lock_create);
    pthread_create(&rx[i],NULL, rx_t ,(void *)&args[i]);
		pthread_create(&tx[i],NULL, tx_t ,(void *)&args[i]);
		pthread_mutex_unlock(&lock_create);
  	}

	for(int i=0;i<NUM_WORKERS;i++){
		pthread_join(rx[i], NULL);
		pthread_join(tx[i], NULL);
	}

	for(int i=0;i<NUM_WORKERS;i++) global_pkt_counter += local_pkt_counter[i];
	double tot_time = ((get_cur_ns() - elapsed_time )/1e9)-1;
	double throughput = global_pkt_counter  / tot_time ;
	printf("Total time: %f seconds \n", tot_time);
	printf("Total received pkts: %d \n", global_pkt_counter);
	printf("Rx Throughput: %d RPS \n", (int)throughput);
  free(redundnacy_filter);
	return 0;
}
