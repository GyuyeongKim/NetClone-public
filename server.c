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
#include <sys/types.h>
#include <netinet/ip.h>
#define MAX_REQUESTS 100000000
#define QUEUE_SIZE 99999999
#define MAX_WORKERS 16
#define NETCLONE_BASE_PORT 1000
#define NOCLONE_BASE_PORT 2000
#define LAEDGE_BASE_PORT 3000
#define NOCLONE 0
#define CLICLONE 1
#define LAEDGE 2
#define NETCLONE 3
#define LAEDGE_COORDINATOR 99 // Coordinator용
#define OP_REQ 0
#define OP_RESP 1
#define NUM_CLI 2
#define NUM_SRV 5 // For Laedge coordination. since coordinator is 1, the remaining servers are 5.
#define BUSY 1
#define IDLE 0

pthread_mutex_t redis_get = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t redis_put = PTHREAD_MUTEX_INITIALIZER;



pthread_mutex_t lock_filter_read = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_filter = PTHREAD_MUTEX_INITIALIZER;
bool* redundnacy_filter1;
bool* redundnacy_filter2;
void initialize_filter() {
    redundnacy_filter1 = (bool*) malloc((MAX_REQUESTS)* sizeof(bool));
    redundnacy_filter2 = (bool*) malloc((MAX_REQUESTS)* sizeof(bool));
}

pthread_mutex_t lock_state[NUM_SRV] = {PTHREAD_MUTEX_INITIALIZER,};
uint32_t SERVER_STATE[NUM_SRV]={IDLE,};

pthread_mutex_t lock_tid = PTHREAD_MUTEX_INITIALIZER;
int t_id = 0;

pthread_mutex_t lock_counter = PTHREAD_MUTEX_INITIALIZER;
uint32_t global_load_counter  = 0;

pthread_mutex_t lock_laedge = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_jobqueue = PTHREAD_MUTEX_INITIALIZER;




double erfinv(double x) {
    double p, q, s;
    p = 0.3275911;
    q = 1.4265591;
    s = 0.180625 - x * x;
    return x * (q + s * (p + s * (q + s * (p + s * (q + s * (p + s * (q + s * (p + s * q))))))));
}

double lognormal_dist(double mean, double stddev) {
    double mu = log(mean) - 0.5 * log(1 + (stddev/mean)*(stddev/mean));
    double sigma = sqrt(log(1 + (stddev/mean)*(stddev/mean)));
    double z = sqrt(2) * erfinv(2 * rand() / RAND_MAX - 1);
    return exp(mu + sigma * z);
}

double fixed_dist(double mean) {
    return mean;
}

double exp_dist(double mean) {
    double random_number = (double)rand() / RAND_MAX;
    return -mean * log(random_number);
}

double bimodal_dist(double ratio_first, double first_job,  double second_job) {
    double random_number = (double)rand() / RAND_MAX;
    if (random_number < ratio_first/100 ) return first_job;
    else return second_job;
}

double trimodal_dist(double ratio_first, double first_job, double ratio_second, double second_job, double third_job) {
    double random_number = (double)rand() / RAND_MAX;
    if (random_number < ratio_first/100 ) return first_job;
    else if (random_number < (ratio_first+ratio_second)/100 ) return second_job;
    else return third_job;
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



struct QueueNode {
  void* data;
  struct QueueNode* next;
};

struct Queue {
  struct QueueNode* head;
  struct QueueNode* tail;
  uint32_t size;
};

void queue_init(struct Queue* q) {
  q->head = NULL;
  q->tail = NULL;
  q->size = 0;

}

uint32_t queue_length(struct Queue* q) {
  return q->size;
}

uint32_t queue_is_empty(struct Queue* q) {
  return q->size == 0;
}

uint32_t queue_is_full(struct Queue* q) {
  return q->size == QUEUE_SIZE;
}

void queue_push(struct Queue* q, void* data, size_t data_size) {
  if (queue_is_full(q)) return;


  struct QueueNode* new_node = (struct QueueNode*)malloc(sizeof(struct QueueNode));
  new_node->data = malloc(data_size);
  memcpy(new_node->data, data, data_size);
  new_node->next = NULL;

  if (queue_is_empty(q)) {
    q->head = new_node;
    q->tail = new_node;
  }
	else {
    q->tail->next = new_node;
    q->tail = new_node;
  }
  q->size++;
}

int queue_pop(struct Queue* q, void* data, size_t data_size) {
  if (queue_is_empty(q)) return 0;

  struct QueueNode* head = q->head;
  memcpy(data, head->data, data_size);
  free(head->data);
  q->head = head->next;
  if (q->head == NULL) q->tail = NULL;
  q->size--;
  return 1;
}

struct Queue q;
struct Queue job_queue;

struct arg_t {
  int sock;
	int PROTOCOL_ID; // thread에 main 함수 argument를 넘기기 위함.
	int SRV_START_IDX;
	int SERVER_ID; // thread에 main 함수 argument를 넘기기 위함.
	int NUM_WORKERS;
  int DIST;
} __attribute__((packed));


void run_work(uint64_t run_ns, double probability, double multiple) {
    uint64_t i = 0;
    if (rand() / (double) RAND_MAX < probability) run_ns = run_ns * multiple;
    do {
        asm volatile ("nop");
        i++;
    } while (i / 0.197 < (double) run_ns);
}


void *coordinator_t(void *arg){
  srand(time(NULL)); // rand함수용 시드 초기화
  char* src_ip[NUM_CLI];
  src_ip[0] = "10.0.1.101";
  src_ip[1] = "10.0.1.102";

  char* dst_ip[NUM_SRV];
  dst_ip[0] = "10.0.1.104";
  dst_ip[1] = "10.0.1.105";
  dst_ip[2] = "10.0.1.106";
  dst_ip[3] = "10.0.1.107";
  dst_ip[4] = "10.0.1.108";

	struct arg_t *args = (struct arg_t *)arg;
	int SRV_START_IDX = args->SRV_START_IDX;
	int PROTOCOL_ID = args->PROTOCOL_ID;
	int SERVER_ID = args->SERVER_ID;
	int NUM_WORKERS = args->NUM_WORKERS;
	pthread_mutex_lock(&lock_tid);
	int i = t_id++;
	pthread_mutex_unlock(&lock_tid);
	pin_to_cpu(i);
	struct sockaddr_in srv_addr;
	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(LAEDGE_BASE_PORT+i);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	int sock;
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("Could not create socket\n");
		exit(1);
	}

	if ((bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) < 0) {
		printf("Could not bind socket\n");
		close(sock);
		exit(1);
	}
	int disable = 1;
	if (setsockopt(sock, IPPROTO_IP, IP_PKTINFO, (void*)&disable, sizeof(disable)) < 0) {
			printf("setsockopt failed\n");
			close(sock);
			exit(1);
	}

  int sock2; // Coordi to Server
	if ((sock2 = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("Could not create socket\n");
		exit(1);
	}

  printf("COORDINATOR %d is running with Socket %d and Port %d\n",i,sock,ntohs(srv_addr.sin_port));
	struct sockaddr_in cli_addr;
  	struct sockaddr_in cli_addr2;
	int cli_addr_len = sizeof(cli_addr);
	int counter = 0;
	int n = 0;
	int load_counter = 0;
	double els_time = 0;
	double throughput = 0;

	char log_file_name[40];
	sprintf(log_file_name,"./log.txt");
	FILE* fd;
	if ((fd = fopen(log_file_name, "w")) == NULL) {
    printf("Failed to open the file\n");
		exit(1);
	}
  int FIRST_SRV = 0;
  int SECOND_SRV = 0;

  struct sockaddr_in srv_addr2;
	memset(&srv_addr2, 0, sizeof(srv_addr2));
	srv_addr2.sin_family = AF_INET;
	srv_addr2.sin_addr.s_addr = htonl(INADDR_ANY);

  uint64_t timer = get_cur_ns();
  char* available_ip[NUM_SRV];
  int available_ip_idx[NUM_SRV];
  int k =0;
  for(k=0;k<NUM_SRV;k++) SERVER_STATE[k] = IDLE;
	while(1){
    	struct laedge_hdr RecvBuffer;
      struct laedge_hdr SendBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);

      if(n>0){

        if(ntohl(RecvBuffer.op) == OP_REQ ){
          int cnt = 0;
          for(k=0;k<NUM_SRV;k++){
            if(SERVER_STATE[k] == IDLE){
              available_ip[cnt] = dst_ip[k];
              available_ip_idx[cnt] = k;
              cnt++;
            }
          }
          RecvBuffer.cli_port = cli_addr.sin_port;

          if(cnt > 1){
            do {
                FIRST_SRV = rand() % cnt;
                SECOND_SRV = rand() % cnt;
            } while (FIRST_SRV == SECOND_SRV); // generate new indices if they are the same

            // First copy sent
            inet_pton(AF_INET, available_ip[FIRST_SRV], &srv_addr2.sin_addr);

            srv_addr2.sin_port = htons(NOCLONE_BASE_PORT);
            sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0, (struct sockaddr *)&(srv_addr2), sizeof(srv_addr2));

            // second copy sent
            inet_pton(AF_INET, available_ip[SECOND_SRV], &srv_addr2.sin_addr);
            sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0, (struct sockaddr *)&(srv_addr2), sizeof(srv_addr2));

            pthread_mutex_lock(&lock_state[available_ip_idx[FIRST_SRV]]);
            SERVER_STATE[available_ip_idx[FIRST_SRV]] = BUSY;
            pthread_mutex_unlock(&lock_state[available_ip_idx[FIRST_SRV]]);

            pthread_mutex_lock(&lock_state[available_ip_idx[SECOND_SRV]]);
            SERVER_STATE[available_ip_idx[SECOND_SRV]] = BUSY;
            pthread_mutex_unlock(&lock_state[available_ip_idx[SECOND_SRV]]);

          }

          else if(cnt == 1){
            inet_pton(AF_INET, available_ip[0], &srv_addr2.sin_addr);
            srv_addr2.sin_port = htons(NOCLONE_BASE_PORT);
            sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0, (struct sockaddr *)&(srv_addr2), sizeof(srv_addr2));

            pthread_mutex_lock(&lock_state[available_ip_idx[0]]);
            SERVER_STATE[available_ip_idx[0]] = BUSY;
            pthread_mutex_unlock(&lock_state[available_ip_idx[0]]);
          }

          else{ // enqueue request to the queue
            pthread_mutex_lock(&lock_laedge);
            queue_push(&q,&RecvBuffer,sizeof(RecvBuffer));
            pthread_mutex_unlock(&lock_laedge);
          }

        }
        else if(ntohl(RecvBuffer.op) == OP_RESP){

          pthread_mutex_lock(&lock_filter_read);
          bool redun = false;
          if (ntohl(RecvBuffer.cli_id) == 0) redun = redundnacy_filter1[ntohl(RecvBuffer.seq)];
          else if (ntohl(RecvBuffer.cli_id) == 1) redun = redundnacy_filter2[ntohl(RecvBuffer.seq)];
          else exit(1);
          pthread_mutex_unlock(&lock_filter_read);
          cli_addr2 = cli_addr;
          uint32_t temp_id = ntohl(RecvBuffer.srv_id);
          pthread_mutex_lock(&lock_state[temp_id]);
          SERVER_STATE[temp_id] = IDLE;
          pthread_mutex_unlock(&lock_state[temp_id]);
          if (!redun){
            pthread_mutex_lock(&lock_filter);
            if (ntohl(RecvBuffer.cli_id) == 0) redundnacy_filter1[ntohl(RecvBuffer.seq)] = true;
            else if (ntohl(RecvBuffer.cli_id) == 1) redundnacy_filter2[ntohl(RecvBuffer.seq)] = true;
            else exit(1);
            pthread_mutex_unlock(&lock_filter);

            inet_pton(AF_INET, src_ip[ntohl(RecvBuffer.cli_id)], &cli_addr2.sin_addr);
            cli_addr2.sin_port = RecvBuffer.cli_port; // 포트를 저장함
            sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0, (struct sockaddr *)&(cli_addr2), sizeof(cli_addr2));
          }

          pthread_mutex_lock(&lock_laedge);
          int not_empty = queue_pop(&q,&SendBuffer,sizeof(SendBuffer));
          pthread_mutex_unlock(&lock_laedge);
          if(not_empty == 1){
            SendBuffer.op = htonl(OP_REQ);

            cli_addr.sin_port = htons(NOCLONE_BASE_PORT);
            sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr *)&(cli_addr), sizeof(cli_addr));
            pthread_mutex_lock(&lock_state[temp_id]);
            SERVER_STATE[temp_id] = BUSY;
            pthread_mutex_unlock(&lock_state[temp_id]);
          }
        }
      }
	}
}


/* WORKER */
void *worker_t(void *arg){
	struct arg_t *args = (struct arg_t *)arg;
	int SRV_START_IDX = args->SRV_START_IDX;
	int PROTOCOL_ID = args->PROTOCOL_ID;
	int SERVER_ID = args->SERVER_ID;
	int NUM_WORKERS = args->NUM_WORKERS;
  int DIST = args->DIST;
  int sock = args->sock;

	pthread_mutex_lock(&lock_tid);
	int i = t_id++;
	pthread_mutex_unlock(&lock_tid);
	pin_to_cpu(i);


  char log_file_name[40];
  sprintf(log_file_name,"./log.txt");
  FILE* fd;
  if ((fd = fopen(log_file_name, "w")) == NULL) {
    printf("Failed to open the file\n");
    exit(1);
  }

	printf("Tx/Rx Worker %d is running with Socket %d  \n",i,sock);

  char* value;
	struct sockaddr_in cli_addr;
	int cli_addr_len = sizeof(cli_addr);
	int counter = 0;
	int n = 0;
	int load_counter = 0;
	double els_time = 0;
	double throughput = 0;
  int clo_counter =0;
  srand(time(NULL));



  int small = 25000; // 25us
  int medium = 50000; // 50us
  int large = small*10; // 250us
  int vlarge = medium*10; // 500us
  double probability = 0.01;
  //probability = 0.001;
  int multiple = 15;

		if (PROTOCOL_ID == NOCLONE){
				struct noclone_hdr RecvBuffer;
          uint64_t run_ns=0;
        while(1){
          pthread_mutex_lock(&lock_jobqueue);
          n = queue_pop(&job_queue,&RecvBuffer,sizeof(RecvBuffer));
          pthread_mutex_unlock(&lock_jobqueue);
  				if (n > 0) {
            if(DIST==0) run_ns = exp_dist(small);
            else if(DIST==1) run_ns = bimodal_dist(90,small,large);
            else if(DIST==2) run_ns = exp_dist(medium);
            else if(DIST==3) run_ns = bimodal_dist(90,medium,vlarge);

            uint64_t i = 0;
            if (rand() / (double) RAND_MAX < probability) run_ns = run_ns * multiple;
            do {
                asm volatile ("nop");
                i++;
            } while (i / 0.197 < (double) run_ns);
  					RecvBuffer.op = htonl(OP_RESP);
  					sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0,  (struct sockaddr *)&(RecvBuffer.cli_addr), sizeof(RecvBuffer.cli_addr));
  				}
        }
		}
    else if (PROTOCOL_ID == CLICLONE){
        uint64_t run_ns=0;
        struct cliclone_hdr RecvBuffer;
        while(1){
          pthread_mutex_lock(&lock_jobqueue);
          n = queue_pop(&job_queue,&RecvBuffer,sizeof(RecvBuffer));
          pthread_mutex_unlock(&lock_jobqueue);
  				if (n > 0) {
            if(DIST==0) run_ns = exp_dist(small);
            else if(DIST==1) run_ns = bimodal_dist(90,small,large);
            else if(DIST==2) run_ns = exp_dist(medium);
            else if(DIST==3) run_ns = bimodal_dist(90,medium,vlarge);
            if (rand() / (double) RAND_MAX < probability) run_ns = run_ns * multiple;
            uint64_t i = 0;
            do {
                asm volatile ("nop");
                i++;
            } while (i / 0.197 < (double) run_ns);
  					RecvBuffer.op = htonl(OP_RESP);
  					sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0,  (struct sockaddr *)&(RecvBuffer.cli_addr), sizeof(RecvBuffer.cli_addr));

  				}
        }
		}
    else if(PROTOCOL_ID == LAEDGE){
        uint64_t run_ns=0;
				struct laedge_hdr RecvBuffer;
        while(1){
          pthread_mutex_lock(&lock_jobqueue);
          n = queue_pop(&job_queue,&RecvBuffer,sizeof(RecvBuffer));
          pthread_mutex_unlock(&lock_jobqueue);
          if (n > 0) {
            if(DIST==0) run_ns = exp_dist(small);
            else if(DIST==1) run_ns = bimodal_dist(90,small,large);
            else if(DIST==2) run_ns = exp_dist(medium);
            else if(DIST==3) run_ns = bimodal_dist(90,medium,vlarge);
            if (rand() / (double) RAND_MAX < probability) run_ns = run_ns * multiple;
            uint64_t i = 0;
            do {
                asm volatile ("nop");
                i++;
            } while (i / 0.197 < (double) run_ns);
            RecvBuffer.op = htonl(OP_RESP);
            RecvBuffer.srv_id = htonl(SERVER_ID-SRV_START_IDX);
  					sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0,  (struct sockaddr *)&(RecvBuffer.cli_addr), sizeof(RecvBuffer.cli_addr));
  				}
        }

		}
		else if(PROTOCOL_ID == NETCLONE){
        uint64_t run_ns=0;
				struct netclone_hdr RecvBuffer;
        while(1){
          pthread_mutex_lock(&lock_jobqueue);
          n = queue_pop(&job_queue,&RecvBuffer,sizeof(RecvBuffer));
          pthread_mutex_unlock(&lock_jobqueue);
          if (n > 0) {
            if(DIST==0) run_ns = exp_dist(small);
            else if(DIST==1) run_ns = bimodal_dist(90,small,large);
            else if(DIST==2) run_ns = exp_dist(medium);
            else if(DIST==3) run_ns = bimodal_dist(90,medium,vlarge);
            if (rand() / (double) RAND_MAX < probability) run_ns = run_ns * multiple;
            uint64_t i = 0;
            do {
                asm volatile ("nop");
                i++;
            } while (i / 0.197 < (double) run_ns);
            RecvBuffer.load = htonl(queue_length(&job_queue));
  					RecvBuffer.op = htonl(OP_RESP);
  					RecvBuffer.sid = htonl(SERVER_ID - SRV_START_IDX);
  					sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0,  (struct sockaddr *)&(RecvBuffer.cli_addr), sizeof(RecvBuffer.cli_addr));
  				}
        }
		}
}


void *dispatcher_t(void *arg){
	struct arg_t *args = (struct arg_t *)arg;
	int PROTOCOL_ID = args->PROTOCOL_ID;
  int sock = args->sock;
  pthread_mutex_lock(&lock_tid);
	int i = t_id++;
	pthread_mutex_unlock(&lock_tid);
	pin_to_cpu(i);
	struct sockaddr_in cli_addr;
	int cli_addr_len = sizeof(cli_addr);
	int n = 0;
	printf("The dispatcher is running\n");
  if(PROTOCOL_ID == NOCLONE){
    struct noclone_hdr RecvBuffer;
    while (1) {
  		n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), 0, (struct sockaddr *)&(cli_addr), &cli_addr_len);
  		if (n > 0) {
  			RecvBuffer.cli_addr = cli_addr;
  			pthread_mutex_lock(&lock_jobqueue);
  			queue_push(&job_queue,&RecvBuffer, sizeof(RecvBuffer));
  			pthread_mutex_unlock(&lock_jobqueue);
  		}
  	}
  }
  else if(PROTOCOL_ID == CLICLONE){
    struct cliclone_hdr RecvBuffer;
    while (1) {
  		n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), 0, (struct sockaddr *)&(cli_addr), &cli_addr_len);
  		if (n > 0) {
  			RecvBuffer.cli_addr = cli_addr;
  			pthread_mutex_lock(&lock_jobqueue);
  			queue_push(&job_queue,&RecvBuffer, sizeof(RecvBuffer));
  			pthread_mutex_unlock(&lock_jobqueue);
  		}
  	}
  }
  else if(PROTOCOL_ID == LAEDGE){
    struct laedge_hdr RecvBuffer;
    while (1) {
  		n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), 0, (struct sockaddr *)&(cli_addr), &cli_addr_len);
  		if (n > 0) {
  			RecvBuffer.cli_addr = cli_addr;
  			pthread_mutex_lock(&lock_jobqueue);
  			queue_push(&job_queue,&RecvBuffer, sizeof(RecvBuffer));
  			pthread_mutex_unlock(&lock_jobqueue);
  		}
  	}
  }
  else if(PROTOCOL_ID == NETCLONE){
    struct netclone_hdr RecvBuffer;
    int cnt =0;
    while (1) {

  		n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), 0, (struct sockaddr *)&(cli_addr), &cli_addr_len);
  		if (n > 0) {

        if(!(ntohl(RecvBuffer.clo) == 2 && queue_length(&job_queue) > 0)){
          RecvBuffer.cli_addr = cli_addr;
          pthread_mutex_lock(&lock_jobqueue);
          queue_push(&job_queue,&RecvBuffer, sizeof(RecvBuffer));
          pthread_mutex_unlock(&lock_jobqueue);

        }



  		}
  	}
  }
}


int main(int argc, char *argv[]) {
	if ( argc < 4 ){
	 printf("Input : %s SRV_START_IDX NUM_WORKERS PROTOCOL_ID WORKLOAD \n", argv[0]);
	 exit(1);
	}


  int SRV_START_IDX = atoi(argv[1]);
	int NUM_WORKERS = atoi(argv[2]);
	int PROTOCOL_ID = atoi(argv[3]);
  int DIST = atoi(argv[4]);
  if(DIST > 3){
    printf("Distribution cannot exceed 3. \n");
    exit(1);
  }


  initialize_filter();
  pthread_mutex_lock(&lock_filter);
  for (int i = 0; i < MAX_REQUESTS; i++) redundnacy_filter1[i] = false;
  for (int i = 0; i < MAX_REQUESTS; i++) redundnacy_filter2[i] = false;
  pthread_mutex_unlock(&lock_filter);

	char *interface = "enp1s0";
	int SERVER_ID = get_server_id(interface) ;

  if(SERVER_ID == -1 ||SERVER_ID == 0){
    interface = "enp1s0np0";
    SERVER_ID = get_server_id(interface) - 2 ;
  }
  else{
    SERVER_ID = SERVER_ID -2;
  }
	if(SERVER_ID > 255){
    printf("Your server ID is not normal please check your network and server configuration.");
    exit(1);
  }
	printf("Server %d is running\n",SERVER_ID);

  printf("You set SRV_START_ID to %d, which means Server Index in Switch is %d. Be careful\n",SRV_START_IDX,(SERVER_ID - SRV_START_IDX));

  struct sockaddr_in srv_addr;
	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	if(PROTOCOL_ID == NOCLONE ||PROTOCOL_ID == CLICLONE || PROTOCOL_ID == LAEDGE ) srv_addr.sin_port = htons(NOCLONE_BASE_PORT); // Base port number + i, thread 2개면 1000 1001 이런식임.
	else if(PROTOCOL_ID == NETCLONE) srv_addr.sin_port = htons(NETCLONE_BASE_PORT);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	// Creat a socket
	int sock;
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("Could not create socket\n");
		exit(1);
	}
	if ((bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) < 0) {
		printf("Could not bind socket\n");
		close(sock);
		exit(1);
	}
	int disable = 1;
	if (setsockopt(sock, IPPROTO_IP, IP_PKTINFO, (void*)&disable, sizeof(disable)) < 0) {
			printf("setsockopt failed\n");
			close(sock);
			exit(1);
	}


	struct arg_t args;
  args.sock = sock;
	args.PROTOCOL_ID = PROTOCOL_ID;
	args.SRV_START_IDX = SRV_START_IDX;
	args.SERVER_ID = SERVER_ID;
	args.NUM_WORKERS = NUM_WORKERS;
	args.DIST = DIST;


  queue_init(&job_queue);

	pthread_t dispatcher,worker[MAX_WORKERS];

  if(PROTOCOL_ID != LAEDGE_COORDINATOR) pthread_create(&dispatcher,NULL, dispatcher_t ,(void *)&args);
	for(int i=0;i<NUM_WORKERS;i++){
    if(PROTOCOL_ID == LAEDGE_COORDINATOR) pthread_create(&worker[i],NULL, coordinator_t ,(void *)&args);
    else pthread_create(&worker[i],NULL, worker_t ,(void *)&args);
	}

	for(int i=0;i<NUM_WORKERS;i++) pthread_join(worker[i], NULL);
  pthread_join(dispatcher,NULL);
  close(sock);
	return 0;
}
