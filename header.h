#define MAX_SRV 32
#define MAX_CLI 32
/* Configuration START */
char *interface = "enp1s0np0"; // Interface name.
#define NUM_CLI 1 // Number of clients. Need to assign server ID automatically. Also need for LAEDGE.
#define NUM_SRV_LAEDGE 3 // the number of servers (including LAEDGE coordinator node). Need for LAEDGE. Min. number is 3 (1 coordinator and 2 servers)
char* src_ip[MAX_CLI] = {"10.0.1.101"}; // Specify client IP addresses. Need for LAEDGE.
char* dst_ip[MAX_SRV] = {"10.0.1.102","10.0.1.103"}; // Specify server IP addresses. Need for LAEDGE, NoClone, C-Clone.
/*
Comment for LAEDGE. Current codebase only supports one LAEDGE coordinator. Also, it should be the next node to the clients.
e.g., if the last client is .102, the coorinator is .103. other servers are .104 and so on.
The minimum required number of nodes for LAEDGE is 4 (1 client, 1 coordinator, 2 servers).
*/
/* Configuration END */

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
#define BUSY 1
#define IDLE 0
#define NUM_HASHTABLE 2 // Number of filter tables. If you want to modify this, you must modity the switch code as well.
#define NETCLONE_BASE_PORT 1000
#define NOCLONE_BASE_PORT 2000
#define LAEDGE_BASE_PORT 3000
#define NOCLONE 0
#define CLICLONE 1
#define LAEDGE 2
#define NETCLONE 3
#define LAEDGE_COORDINATOR 99 
#define OP_REQ 0
#define OP_RESP 1

pthread_mutex_t lock_filter_read = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_filter = PTHREAD_MUTEX_INITIALIZER;

bool* redundnacy_filter;
bool* redundancy_filters[NUM_CLI];

void initialize_filter_server() {
    for(int i = 0; i < NUM_CLI; i++) {
        redundancy_filters[i] = (bool*) malloc(MAX_REQUESTS * sizeof(bool));
        if (redundancy_filters[i] == NULL) {
            printf("Memory allocation for redundancy filter fails. exit! \n");
            exit(1);
        } 
        for (int j = 0; j < MAX_REQUESTS; j++) {
            redundancy_filters[i][j] = false;
        }
       
    }
}


void initialize_filter_client() {
    redundnacy_filter = (bool*) malloc((MAX_REQUESTS)* sizeof(bool));
}



pthread_mutex_t lock_state[NUM_SRV_LAEDGE] = {PTHREAD_MUTEX_INITIALIZER};
uint32_t SERVER_STATE[NUM_SRV_LAEDGE] = {IDLE};

pthread_mutex_t lock_tid = PTHREAD_MUTEX_INITIALIZER;
int t_id = 0;

pthread_mutex_t lock_counter = PTHREAD_MUTEX_INITIALIZER;
uint32_t global_load_counter  = 0;

pthread_mutex_t lock_laedge = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_jobqueue = PTHREAD_MUTEX_INITIALIZER;

int local_pkt_counter[MAX_WORKERS] = {0};
int global_pkt_counter = 0;

pthread_mutex_t lock_txid = PTHREAD_MUTEX_INITIALIZER;
int tx_id = 0;
pthread_mutex_t lock_rxid = PTHREAD_MUTEX_INITIALIZER;
int rx_id = 0;
pthread_mutex_t lock_create = PTHREAD_MUTEX_INITIALIZER;


#pragma pack(1)
struct netclone_hdr{
    uint32_t op; // Operation type
    uint32_t seq; // Sequence number
    uint32_t grp; // Group ID
    uint32_t sid; // Server ID
    uint32_t load; // Server Load
    uint32_t clo; // Cloned or not
    uint32_t tidx; // Filter table index
    uint64_t latency; // only for stats
    struct sockaddr_in cli_addr; // We can exclude this field in fact, but leave it as is.
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


/* Caculate combination nCk */
int combination(int n, int k) {
  return (int) (round(exp(lgamma(n + 1) - lgamma(k + 1) - lgamma(n - k + 1))));
}

/* Get current time in nanosecond-scale */
uint64_t get_cur_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t t = ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
  return t;
}

/* Get runtime following exponential/bimodal/trimodal distributions */
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

/* Get server ID based on the last digit of the IP address */
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

/* Pin a thread to a specific CPU core */
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

/* Job queue structure */
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