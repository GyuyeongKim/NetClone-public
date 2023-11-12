#include "header.h"

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


/* Tx-side function */
void *tx_t(void *arg){
  pthread_mutex_lock(&lock_txid);
	int i = tx_id++;
	pthread_mutex_unlock(&lock_txid);
	pin_to_cpu(i);
  struct arg_t *args = (struct arg_t *)arg;
  struct sockaddr_in srv_addr = args->srv_addr;

  srand(time(NULL));
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
  int FIRST_SRV = 0;
  int SECOND_SRV = 0;
  int NUM_GRP=combination(NUM_SRV,2)*2; 
	while(1){
    /* Calculate inter-arrival time following exponential distirbuion */
    inter_arrival_time = (uint64_t)(-log(1.0 - ((double)rand() / TARGET_QPS)) * 1000000) ; 
    temp_time+=inter_arrival_time;
    /* Spins if current time is not enough to calculated inter-arrival time */
    while (get_cur_ns() < temp_time) 
      ;

    /* Baseline. Simply send requests */
    if(PROTOCOL_ID == NOCLONE){
      inet_pton(AF_INET, dst_ip[rand()%NUM_SRV], &srv_addr.sin_addr);
      srv_addr.sin_port = htons(NOCLONE_BASE_PORT);
      struct noclone_hdr SendBuffer={0,};
      SendBuffer.op = htonl(OP_REQ);
      SendBuffer.latency = get_cur_ns();
      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));
    }
    /* C-Clone. Send duplicate requests to two different servers */
    else if(PROTOCOL_ID == CLICLONE){
      struct cliclone_hdr SendBuffer={0,};
      SendBuffer.op = htonl(OP_REQ);
      pthread_mutex_lock(&lock_counter);
      uint32_t temp = global_load_counter++;
      pthread_mutex_unlock(&lock_counter);
      SendBuffer.seq = temp;
      SendBuffer.latency = get_cur_ns();

      /* Find two different servers*/
      do {
          FIRST_SRV = rand() % NUM_SRV;
          SECOND_SRV = rand() % NUM_SRV;
      } while (FIRST_SRV == SECOND_SRV);

      /* First copy sent */
      inet_pton(AF_INET, dst_ip[FIRST_SRV], &srv_addr.sin_addr);
      srv_addr.sin_port = htons(NOCLONE_BASE_PORT);
      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));

      /* Second copy sent */
      inet_pton(AF_INET, dst_ip[SECOND_SRV], &srv_addr.sin_addr);
      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));

    }

    /* LAEDGE. LAEDGE first sends requests to the coordinator. */
    else if(PROTOCOL_ID == LAEDGE){
      struct laedge_hdr SendBuffer={0,};
      inet_pton(AF_INET, dst_ip[0], &srv_addr.sin_addr); // We assume coordinator is only one. So, dst_ip[0] is the coordinator.
      srv_addr.sin_port = htons(LAEDGE_BASE_PORT+ rand()%NUM_WORKERS_SRV);
      SendBuffer.cli_id = htonl(SERVER_ID-1);
      
      SendBuffer.op = htonl(OP_REQ);

      pthread_mutex_lock(&lock_counter);
      uint32_t temp = global_load_counter++;
      SendBuffer.seq = htonl(temp);
      pthread_mutex_unlock(&lock_counter);

      SendBuffer.latency = get_cur_ns();
      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));
    }

    /* NetClone */
    else if(PROTOCOL_ID == NETCLONE){
      inet_pton(AF_INET, dst_ip[rand()%NUM_SRV], &srv_addr.sin_addr);
      srv_addr.sin_port = htons(NETCLONE_BASE_PORT);
      struct netclone_hdr SendBuffer={0,};
      SendBuffer.op = htonl(OP_REQ);
      SendBuffer.grp = htonl( rand()%NUM_GRP+1); // Assigns random group ID that specifies two servers
      SendBuffer.sid = htonl( rand()%NUM_SRV); // Assigns random dst. server ID for load balancing
      SendBuffer.tidx = htonl( rand()%NUM_HASHTABLE); // Assgins random filter table index

      SendBuffer.latency = get_cur_ns();
      sendto(sock, &SendBuffer, sizeof(SendBuffer),  0, (struct sockaddr*)&(srv_addr), sizeof(srv_addr));

    }
    counter++;
    /* Avoids throughput miscalculation when we use C-Clone */
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

/* Rx-side function */
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

  /* Log file definition */
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
  while(1){
		if((get_cur_ns() - timer  ) > 1e9 )  break; // If Rx thread does not receive any pkt more than 1 seconds, then terminate the program.

    if(PROTOCOL_ID == NOCLONE){
      struct noclone_hdr RecvBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);
      if(n>0){
        if(ntohl(RecvBuffer.op) == OP_RESP){
          fprintf(fd,"%lu\n",(get_cur_ns() - RecvBuffer.latency)/1000); // Record latency
          local_pkt_counter[i]++; // Increase pkt counter
          timer = get_cur_ns();
        }
      }
    }
    else if(PROTOCOL_ID == CLICLONE){
      struct cliclone_hdr RecvBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);
      if(n>0){
        if(ntohl(RecvBuffer.op) == OP_RESP){
          pthread_mutex_lock(&lock_filter_read);
          bool redun = redundnacy_filter[RecvBuffer.seq]; // Check whether this is duplicate response
          pthread_mutex_unlock(&lock_filter_read);
          if (!redun){ // If not duplicate response, then record latency
      			fprintf(fd,"%lu\n",(get_cur_ns() - RecvBuffer.latency)/1000); // write latency in microseconds
      			local_pkt_counter[i]++;
      			timer = get_cur_ns();
            pthread_mutex_lock(&lock_filter);
            redundnacy_filter[RecvBuffer.seq] = true;
            pthread_mutex_unlock(&lock_filter);
          } else redundnacy_counter++; // Otherwise, just increase redundancy counter.
        }
      }
    }
    else if(PROTOCOL_ID == LAEDGE){
      struct laedge_hdr RecvBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);
      if(n>0){
        if(ntohl(RecvBuffer.op) == OP_RESP){
    			fprintf(fd,"%lu\n",(get_cur_ns() - RecvBuffer.latency)/1000); // write latency in microseconds
    			local_pkt_counter[i]++;
    			timer = get_cur_ns();
        }
      }
    }
    else if(PROTOCOL_ID == NETCLONE){
      struct netclone_hdr RecvBuffer;
      int n = recvfrom(sock, &RecvBuffer, sizeof(RecvBuffer), MSG_DONTWAIT, (struct sockaddr*)&(cli_addr), &cli_addr_len);
      if(n>0){
        if(ntohl(RecvBuffer.op) == OP_RESP){
          RecvBuffer.seq = ntohl(RecvBuffer.seq);
          pthread_mutex_lock(&lock_filter_read);
          bool redun = redundnacy_filter[RecvBuffer.seq];
          pthread_mutex_unlock(&lock_filter_read);
          if (!redun){ // if not redundancy, write latency
      			fprintf(fd,"%lu\n",(get_cur_ns() - RecvBuffer.latency)/1000); // write latency in microseconds
      			local_pkt_counter[i]++;
      			timer = get_cur_ns();
            pthread_mutex_lock(&lock_filter);
            redundnacy_filter[RecvBuffer.seq] = true;
            pthread_mutex_unlock(&lock_filter);
          } else redundnacy_counter++; // if redundancy, drop the pkt and count it.
        }
      }
    }
  }

  /* Finish Rx worker and report stats. */
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
  initialize_filter_client();
  pthread_mutex_lock(&lock_filter);
  for (int i = 0; i < MAX_REQUESTS; i++) redundnacy_filter[i] = false;
  pthread_mutex_unlock(&lock_filter);

	int SERVER_ID = get_server_id(interface);

  if(SERVER_ID > 255 || SERVER_ID == -1 || SERVER_ID == 0){
    printf("Your server ID %d is not normal please check your network and server configuration.\n",SERVER_ID);
    exit(1);
  }
	else printf("Client %d is running \n",SERVER_ID);

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


  /* Launch threads */
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

  /* Report stats */
	for(int i=0;i<NUM_WORKERS;i++) global_pkt_counter += local_pkt_counter[i];
	double tot_time = ((get_cur_ns() - elapsed_time )/1e9)-1;
	double throughput = global_pkt_counter  / tot_time ;
	printf("Total time: %f seconds \n", tot_time);
	printf("Total received pkts: %d \n", global_pkt_counter);
	printf("Rx Throughput: %d RPS \n", (int)throughput);
  free(redundnacy_filter);
	return 0;
}
