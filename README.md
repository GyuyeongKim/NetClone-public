# Overview
This is the artifact that is used to evaluate NetClone, as described in the paper "NetClone: Fast, Scalable, and Dynamic Request Cloning for Microsecond-Scale RPCs" in ACM SIGCOMM 2023.

# Contents

This repository contains the following code segments:

1. Switch data plane code
2. Switch control plane code
3. Client and server applications with synthetic RPC workloads.

# Hardware dependencies

- To run experiments using the artifact, at least 3 nodes (1 client and 2 servers) are required. However, it is recommended to use more nodes because the benefit may not be much in a small cluster. The experiments in the paper were conducted using 8 nodes (2 clients and 6 servers). 
- Nodes should be equipped with an Nvidia ConnectX-5 NIC or similar NIC supporting Nvidia VMA. Experiments can still be run without the CX5 NICs, but this may result in increased latency and decreased throughput due to the application's reliance on a legacy network stack.
- A programmable switch with Intel Tofino ASIC is needed.

# Software dependencies
Our artifact is tested on:
- Ubuntu 20.04 LTS with Linux kernel 5.15.
- Mellanox OFED drivers for NICs. The version is 5.8-1.2.1 LTS.
- libvma 9.4.0 for VMA
- Intel P4 Studio SDE 9.7.0 and BSP 9.7.0.

# Installation

## Client/Server-side
1. Place `client.c`, `server.c`, and `Makefile` in the home directory (We used /home/netclone in the paper).
2. Configure cluster-related details in `cleint.c` and `server.c`, such as IP and MAC addresses. Note that IP configuration is important in this artifact. Each node should have a linearly-increasing IP address. For example, we use 10.0.1.101 for node1, 10.0.1.102 for node2, ... See Client/Server-side in the Experiment workflow section for more detail.
3. Compile `client.c` and `server.c` using `make`.

## Switch-side
1. Place `controller.py` and `netclone.p4` in the SDE directory.
2. Compile `netclone.p4` using the P4 compiler (we used `p4build.sh` provided by Intel).
3. Configure cluster-related information in the `netclone.p4`.
4. Configure cluster-related information in the `controller.py`. This includes IP and MAC addresses, and port-related information. 

# Experiment workflow
## Switch-side
1. Run NetClone program using `run_switchd.sh -p netclone` in the SDE directory. `run_switch.sh` is included in the SDE by default.
2. Configure ports manually or run_bfshell.sh in the other terminal. It is recommended to configure ports to 100Gbps.
3. Run the controller using `python3 controller.py` in the SDE directory.

## Client/Server-side
1. Make sure your ARP table and IP configuration are correct. The provided switch code does not concern the network setup of hosts. Therefore, you should do network configuration in hosts manually. Also, please double check the cluster-related information in the codes is configured correctly.
2. Make sure each node can communicate by using tools like `ping`.
3. Configure VMA-related stuffs like socket buffers, hugepages, etc. The following commands must be executed in all nodes. <br>
`sysctl -w net.core.rmem_max=104857600 && sysctl -w net.core.rmem_default=104857600` <br>
`echo 2000000000 > /proc/sys/kernel/shmmax` <br>
`echo 2048 > /proc/sys/vm/nr_hugepages` <br>
`ulimit -l unlimited` <br>

5. Turn on the server program in server nodes by typing the following command<br>
`LD_PRELOAD=libvma.so VMA_THREAD_MODE=2 ./server SRV_START_IDX NUM_WORKERS PROTOCOL_ID DIST` <br>
`SRV_START_IDX`: The start index of the server. This is needed to get the server index switch data plane. Sorry for this ugly stuff. Let me explain. In the server program, the server ID is automatically assigned by parsing the IP address. In the cluster used in the paper, node1 has 10.0.1.101, node2 has 10.0.1.102, node3 has 10.0.1.103, ... so on. node1 and node2 are clients and node3~node8 are servers. So, we set `SRV_START_IDX` to 1 so that node3 gets the index in the switch as 2 (3 - 1) where 3 comes from the last digit of the IP address and 1 is the SRV_START_IDX. In the switch data plane, 0 is node1, 1 is node2, 2 is node 3, .. so on. You may remove this argument and manually assign the server index in the switch data plane like `RecvBuffer.srv_id=Your own index`. Note that this is for the convenience of experiments, not the core part of NetClone. <br>
`NUM_WORKERS`: The number of worker threads.<br>
`PROTOCOL_ID`: The ID of protocols to use. 0 is the baseline (no cloning), 1 is C-Clone (CLICLONE in the code), 2 is LAEDGE, 3 is NetClone.<br>
`DIST`: The distribution of RPC workloads. For example, 0 is exponential (25us), 1 is bimodal (25us,250us), and etc. Check the detail in the code.<br>

For example, to reproduce the result of NetClone in Figure 7 (a), use the following command:<br>
`LD_PRELOAD=libvma.so VMA_THREAD_MODE=2 ./server 1 15 3 0`<br>
Be aware that this command is only valid for the IP address configuration is correct and the server CPU supports more than 15 threads.

To evaluate LAEDGE, one node should be the coordinator. To run the coordinator, set `PROTOCOL_ID` to 99.<br>
`LD_PRELOAD=libvma.so VMA_THREAD_MODE=2 ./server 2 15 99 0`


7. Turn on the client program in client nodes by using the following command. <br>
`Usage: ./client NUM_SRV Protocol Distribution TIME_EXP TARGET_QPS`<br>
`NUM_SRV`: The number of server nodes.<br>
`Protocol`: The ID of protocols to use. Same as in the server-side one.<br>
`Distribution`: Same as in the server-side one, but this is only for the naming of the log file.<br>
`TIME_EXP`: The experiment time. Set this to more than 20 because there is a warm-up effect at the early phase of the experiment.<br>
`TARGET_QPS`: The target throughput (=Tx throughput).

For example, to reproduce a result of NetClone in Figure 7 (a), use the following command:<br>
`LD_PRELOAD=libvma.so VMA_THREAD_MODE=2 ./client 6 3 0 20 1000000` <br>

8. When the experiment is finished, the clients report Tx/Rx throughput, experiment time, and other related information. Request latency is logged as a text file like `log-0-0-0-6-15-1-1-20-103000.txt`. The end line of the log contains the total experiment time. Therefore, when you analyze the log, you should be careful.


# Citation

Please cite this work if you refer to or use any part of this artifact for your research. 

BibTex:

      @inproceedings {netclone,
         author = {Gyuyeong Kim},
         title = {NetClone: Fast, Scalable, and Dynamic Request Cloning for Microsecond-Scale RPCs},
         booktitle = {Proc. of ACM SIGCOMM},
         year = {2023},
         address = {New York, NY, USA},
         month = sep,
         publisher = {Association for Computing Machinery},
         numpages = {13},
         pages ={195–207},
      } 
