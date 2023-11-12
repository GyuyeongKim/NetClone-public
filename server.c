#include "header.h"

/* Argument structure */
struct arg_t {
  int sock;
	int PROTOCOL_ID;
	int SERVER_ID;
	int NUM_WORKERS;
  int DIST;
} __attribute__((packed));


/* LAEDGE coordinator */
void *coordinator_t(void *arg){
  srand(time(NULL));

	struct arg_t *args = (struct arg_t *)arg;
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
	int sock; // Socket between client to coordinator
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("Could not create socket\n");
		exit(1);
	}

	if ((bind(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) < 0) {
		printf("Could not bind socket\n");
		close(sock);
		exit(1);
	}

  int sock2; // Socket between coordinator to server
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
	double throughput = 0;

  int FIRST_SRV = 0;
  int SECOND_SRV = 0;

  struct sockaddr_in srv_addr2;
	memset(&srv_addr2, 0, sizeof(srv_addr2));
	srv_addr2.sin_family = AF_INET;
	srv_addr2.sin_addr.s_addr = htonl(INADDR_ANY);

  uint64_t timer = get_cur_ns();
  char* available_ip[NUM_SRV_LAEDGE]; // Idle server list
  int available_ip_idx[NUM_SRV_LAEDGE];
  int k =0;
  for(k=1;k<NUM_SRV_LAEDGE;k++) SERVER_STATE[k] = IDLE;
	while(1){
    	struct laedge_hdr RecvBuffer;
      struct laedge_hdr SendBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);
      if(n>0){
        if(ntohl(RecvBuffer.op) == OP_REQ ){ // Request arrives
          int cnt = 0;
          /* Check server states */
          for(k=1;k<NUM_SRV_LAEDGE;k++){ 
                if(SERVER_STATE[k] == IDLE){
                    available_ip[cnt] = dst_ip[k]; 
                    available_ip_idx[cnt] = k;
                    cnt++;
                }
            }
          RecvBuffer.cli_port = cli_addr.sin_port;

          /* If there are at least two idle servers, then clone requests */
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
          /* If only one server is idle, then just forward it without cloning */
          else if(cnt == 1){
            inet_pton(AF_INET, available_ip[0], &srv_addr2.sin_addr);
            srv_addr2.sin_port = htons(NOCLONE_BASE_PORT);
            sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0, (struct sockaddr *)&(srv_addr2), sizeof(srv_addr2));

            pthread_mutex_lock(&lock_state[available_ip_idx[0]]);
            SERVER_STATE[available_ip_idx[0]] = BUSY;
            pthread_mutex_unlock(&lock_state[available_ip_idx[0]]);
          }
          /* Otherwise, enqueue the request to the queue */
          else{ 
            pthread_mutex_lock(&lock_laedge);
            queue_push(&q,&RecvBuffer,sizeof(RecvBuffer));
            pthread_mutex_unlock(&lock_laedge);
          }

        }
        /* Response handling */
        else if(ntohl(RecvBuffer.op) == OP_RESP){

          pthread_mutex_lock(&lock_filter_read);
          bool redun = false;
          if (ntohl(RecvBuffer.cli_id) < NUM_CLI) {
              redun = redundancy_filters[ntohl(RecvBuffer.cli_id)][ntohl(RecvBuffer.seq)];
          } else {
              printf("Redundancy filter errors! %u \n", ntohl(RecvBuffer.cli_id));
              exit(1);
          }
          pthread_mutex_unlock(&lock_filter_read);
          cli_addr2 = cli_addr;
          uint32_t temp_id = ntohl(RecvBuffer.srv_id);
          pthread_mutex_lock(&lock_state[temp_id]);
          SERVER_STATE[temp_id] = IDLE;
          pthread_mutex_unlock(&lock_state[temp_id]);

          if (!redun) {
              pthread_mutex_lock(&lock_filter);
              if (ntohl(RecvBuffer.cli_id) < NUM_CLI) {
                  redundancy_filters[ntohl(RecvBuffer.cli_id)][ntohl(RecvBuffer.seq)] = true;
              } else {
                  printf("Redundancy filter errors! %u \n", ntohl(RecvBuffer.cli_id));
                  exit(1);
              }
            pthread_mutex_unlock(&lock_filter);
            inet_pton(AF_INET, src_ip[ntohl(RecvBuffer.cli_id)], &cli_addr2.sin_addr);
            cli_addr2.sin_port = RecvBuffer.cli_port; // save port information
            sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0, (struct sockaddr *)&(cli_addr2), sizeof(cli_addr2));
          }

          pthread_mutex_lock(&lock_laedge);
          int not_empty = queue_pop(&q,&SendBuffer,sizeof(SendBuffer));
          pthread_mutex_unlock(&lock_laedge);
          if(not_empty == 1) {
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
	int PROTOCOL_ID = args->PROTOCOL_ID;
	int SERVER_ID = args->SERVER_ID;
	int NUM_WORKERS = args->NUM_WORKERS;
  int DIST = args->DIST;
  int sock = args->sock;

	pthread_mutex_lock(&lock_tid);
	int i = t_id++;
	pthread_mutex_unlock(&lock_tid);
	pin_to_cpu(i);

	printf("Tx/Rx Worker %d is running with Socket %d  \n",i,sock);

  char* value;
	struct sockaddr_in cli_addr;
	int cli_addr_len = sizeof(cli_addr);
	int counter = 0;
	int n = 0;
	double throughput = 0;
  int clo_counter =0;
  srand(time(NULL));


  int small = 25000; // small runtime 25us
  int medium = 50000; // medium runtime 50us
  int large = small*10; // large runtime 250us
  int vlarge = medium*10; // very large runtime 500us
  double probability = 0.01; // probability for unexpected variability
  int multiple = 15; // latency multiple factor when variability happens

  /* Baseline */
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

          /* Do dummy RPC work*/
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
  /* C-Clone */
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

          /* Do dummy RPC work*/
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
  /* LAEDGE */
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

          /* Do dummy RPC work*/
          uint64_t i = 0;
          do {
              asm volatile ("nop");
              i++;
          } while (i / 0.197 < (double) run_ns);
          RecvBuffer.op = htonl(OP_RESP);
          RecvBuffer.srv_id = htonl(SERVER_ID);
          sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0,  (struct sockaddr *)&(RecvBuffer.cli_addr), sizeof(RecvBuffer.cli_addr));
        }
      }

  }
  /* NetClone */
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

           /* Do dummy RPC work*/
          uint64_t i = 0;
          do {
              asm volatile ("nop");
              i++;
          } while (i / 0.197 < (double) run_ns); 

          RecvBuffer.load = htonl(queue_length(&job_queue));
          RecvBuffer.op = htonl(OP_RESP);
          RecvBuffer.sid = htonl(SERVER_ID - 1);
          sendto(sock, &RecvBuffer, sizeof(RecvBuffer),  0,  (struct sockaddr *)&(RecvBuffer.cli_addr), sizeof(RecvBuffer.cli_addr));
        }
      }
		}
}

/* Dispatcher  */
/* Dispatcher recevies the request and enqueues it to the job queue  */
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
        //printf("%d\n",n);
        if(!(ntohl(RecvBuffer.clo) == 2 && queue_length(&job_queue) > 0)){ // If cloned request arrives but the server is not idle, then drop the request.
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
	 printf("Input : %s NUM_WORKERS PROTOCOL_ID WORKLOAD\n", argv[0]);
	 exit(1);
	}

	int NUM_WORKERS = atoi(argv[1]);
	int PROTOCOL_ID = atoi(argv[2]);
  int DIST = atoi(argv[3]);

  pthread_mutex_lock(&lock_filter);
  initialize_filter_server();
  pthread_mutex_unlock(&lock_filter);

  /* Automatic server ID assignment */
	int SERVER_ID = get_server_id(interface) ;
  SERVER_ID = SERVER_ID - NUM_CLI;
  if(PROTOCOL_ID==LAEDGE) SERVER_ID = SERVER_ID - 1;
	if(SERVER_ID > 255 || SERVER_ID == -1 ||SERVER_ID == 0){
    printf("Your server ID %d is not normal please check your network and server configuration.\n",SERVER_ID);
    exit(1);
  }
	printf("Server %d is running\n",SERVER_ID);
  printf("Server Index in Switch is %d.\n",SERVER_ID - 1);

  struct sockaddr_in srv_addr;
	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	if(PROTOCOL_ID == NOCLONE ||PROTOCOL_ID == CLICLONE || PROTOCOL_ID == LAEDGE ) srv_addr.sin_port = htons(NOCLONE_BASE_PORT); 
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

	struct arg_t args;
  args.sock = sock;
	args.PROTOCOL_ID = PROTOCOL_ID;
	args.SERVER_ID = SERVER_ID;
	args.NUM_WORKERS = NUM_WORKERS;
	args.DIST = DIST;


  queue_init(&job_queue);

  /* Launch threads */
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
