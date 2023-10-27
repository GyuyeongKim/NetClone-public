# Overview
This is the artifact that is used to evaluate NetClone, as described in the paper "NetClone: Fast, Scalable, and Dynamic Request Cloning for Microsecond-Scale RPCs" in ACM SIGCOMM 2023.
NetClone is an in-network dynamic request cloning mechanism for microsecond-scale workloads. With Netclone, the switch dynamically duplicates requests to two idle servers and returns only a faster response to the client.
With this, the client can enjoy low tail latency even if there is unexpected latency variability in servers. 

# Contents

This repository contains the following code segments:

1. Switch data plane code
2. Switch control plane code
3. Client and server applications with synthetic RPC workloads.

# Contents

# Hardware dependencies

- To run experiments using the artifact, at least 3 nodes (1 client and 2 servers) are required. However, it is recommended to use more nodes because the benefit may not be much in a small cluster. 
- Nodes should be equipped with an Nvidia ConnectX-5 NIC or similar NIC supporting Nvidia VMA for kernel-bypass networking. Experiments can still be run without the VMA-capable NICs, but this may result in increased latency and decreased throughput due to the application's reliance on a legacy network stack. 
- A programmable switch with Intel Tofino1 ASIC is needed.

Our artifact is tested on:
- 6 nodes (2 clients and 4 servers) with single-port Nvidia 100GbE MCX515A-CCAT ConnectX-5 NIC
- APS BF6064XT switch with Intel Tofino1 ASIC

# Software dependencies
Our artifact is tested on:

**Clients and servers:**
- Ubuntu 20.04 LTS with Linux kernel 5.15.
- Mellanox OFED drivers for NICs. The version is 5.8-1.2.1 LTS.
- gcc 9.4.0
- libvma 9.4.0 for VMA

**Switch:**
- Ubuntu 20.04 LTS with Linux kernel 5.4.
- python 3.8.10
- Intel P4 Studio SDE 9.7.0 and BSP 9.7.0. 

# Testbed illustration

![Testbed](testbednetclone.png)

# Installation

## Client/Server-side
1. Place `client.c`, `server.c`, and `Makefile` in the home directory (We used `/home/netclone` in the paper).
2. Configure cluster-related details in `client.c` and `server.c`, such as IP and MAC addresses. Note that IP configuration is important in this artifact. Each node should have a linearly-increasing IP address. For example, we use 10.0.1.101 for node1, 10.0.1.102 for node2, and so on. This is because the server program automatically assigns the server ID using the last digit of the IP address. e.g., for 10.0.1.103, the server ID is 3 (cuz the last digit of .103 is 3).

   `client.c`
   
   - Line 26 MAX_SRV // the number of servers
   - Line 34 NUM_CLI // the number of clients
   - Lines 183~211 // src_ip and dst_ip arrays.
   - Lines 442~445 //Please set the interface name correctly. By default, it is set to `enp1s0` or `enp1s0np0`.

    `server.c`
   - Line 35 NUM_CLI // the number of clients
   - Line 36 NUM_SRV // the number of servers. This is only for LAEDGE coordinator.
   - Lines 280~289 // src_ip and dst_ip arrays for LAEDGE coordinator. The reason why `10.0.1.103` (node3) is absent is, because node3 is the coordinator.
   - Lines 707~711 //Please set the interface name correctly. By default, it is set to `enp1s0np0` or `enp1s0np0`.
4. Compile `client.c` and `server.c` using `make`.

## Switch-side
1. Place `controller.py` and `netclone.p4` in the SDE directory.
2. Configure cluster-related information in the `netclone.p4`.
   - Line 10 MAX_SRV // the maximum number of servers in the testbed
   - Line 552 ig_initr_md.ingress_port // we currently use 452 for recirculation. 452 is the recirculation port for pipeline 3 in our APS BF6064XT. Check your switch spec and set it correctly. 
3. Configure cluster-related information in the `controller.py`. This includes IP and MAC addresses, and port-related information.
   - Lines 21~25 // Several cluster-related values
   - Lines 104~137 // IP, Port, MAC information. The last address is the port of the switch control plane (but NetClone does not use it. so you can remove it).
   - Lines 162~484// Cloning-related configuration. The number of entries in the tables depends on the number of servers.
4. Compile `netclone.p4` using the P4 compiler (we used `p4build.sh` provided by Intel). You can compile it manually with the following commands.
   - `cmake ${SDE}/p4studio -DCMAKE_INSTALL_PREFIX=${SDE_INSTALL} -DCMAKE_MODULE_PATH=${SDE}/cmake -DP4_NAME=netclone -DP4_PATH=${SDE}/netclone.p4`
   - `make`
   - `make install`
   - `${SDE}` and `${SDE_INSTALL}` are path to the SDE. In our testbed, SDE = `/home/admin/bf-sde-9.7.0`  and SDE_INSTALL = `/home/admin/bf-sde-9.7.0/install`.
   - If done well, you should see the following outputs
     ![output](https://github.com/GyuyeongKim/NetClone-public/blob/0d9cb690f693a1b8b876c14cafa6def05713a5e2/output.png)
# Experiment workflow
## Switch-side
1. Open three terminals for the switch control plane. We need them for 1) starting the switch program, 2) port configuration, 3) rule configuration by controller
2. In terminal 1, run NetClone program using `run_switchd.sh -p netclone` in the SDE directory. `run_switch.sh` is included in the SDE by default.
3. In terminal 2, configure ports manually or `run_bfshell.sh`. It is recommended to configure ports to 100Gbps.
 - After starting the switch program, run `./run_bfshell.sh` and type `ucli` and `pm`.
 - You can create ports like `port-add #/- 100G NONE` and `port-enb #/-`. It is recommended to turn off auto-negotiation using `an-set -/- 2`. This part requires knowledge of Intel Tofino-related stuff. You can find more information in the switch manual or on Intel websites.
4. In terminal 3, run the controller using `python3 controller.py` in the SDE directory at the other terminal.

## Client/Server-side
1. Open terminals for each node. For example, we open 8 terminals for 8 nodes (2 clients and 4 servers).
2. Make sure your ARP table and IP configuration are correct. The provided switch code does not concern the network setup of hosts. Therefore, you should do network configuration in hosts manually. Also, please double-check check the cluster-related information in the codes is configured correctly.
   - You can set the arp rule using `arp -s IP_ADDRESS MAC_ADDRESS`. For example, type `arp -s 10.0.1.101 0c:42:a1:2f:12:e6` in node 2~8 for node 1.
3. Make sure each node can communicate by using tools like `ping`. e.g., `ping 10.0.1.101` in other nodes.
4. Configure VMA-related stuffs like socket buffers, hugepages, etc. The following commands must be executed in all nodes. <br>
`sysctl -w net.core.rmem_max=104857600 && sysctl -w net.core.rmem_default=104857600` <br>
`echo 2000000000 > /proc/sys/kernel/shmmax` <br>
`echo 2048 > /proc/sys/vm/nr_hugepages` <br>
`ulimit -l unlimited` <br>

5. Turn on the server program in server nodes by typing the following command<br>
`LD_PRELOAD=libvma.so VMA_THREAD_MODE=2 ./server NUM_WORKERS PROTOCOL_ID DIST` <br>
`NUM_WORKERS`: The number of worker threads.<br>
`PROTOCOL_ID`: The ID of protocols to use. 0 is the baseline (no cloning), 1 is C-Clone (CLICLONE in the code), 2 is LAEDGE, 3 is NetClone.<br>
`DIST`: The distribution of RPC workloads. For example, 0 is exponential (25us), 1 is bimodal (25us,250us), and etc. Check the details in the code.<br>

For example, to reproduce the result of NetClone in Figure 7 (a), use the following command:<br>
`LD_PRELOAD=libvma.so VMA_THREAD_MODE=2 ./server 15 3 0`<br>
Be aware that this command is only valid for the IP address configuration is correct and the server CPU supports more than 15 threads.

To evaluate LAEDGE, one node should be the coordinator. To run the coordinator, set `PROTOCOL_ID` to 99.<br>
`LD_PRELOAD=libvma.so VMA_THREAD_MODE=2 ./server 15 99 0`


6. Turn on the client program in client nodes by using the following command. <br>
`Usage: ./client NUM_SRV Protocol Distribution TIME_EXP TARGET_QPS`<br>
`NUM_SRV`: The number of server nodes.<br>
`Protocol`: The ID of protocols to use. Same as in the server-side one.<br>
`Distribution`: Same as in the server-side one, but this is only for the naming of the log file.<br>
`TIME_EXP`: The experiment time. Set this to more than 20 because there is a warm-up effect at the early phase of the experiment.<br>
`TARGET_QPS`: The target throughput (=Tx throughput).

For example, to reproduce a result of NetClone in Figure 7 (a), use the following command:<br>
`LD_PRELOAD=libvma.so VMA_THREAD_MODE=2 ./client 6 3 0 20 1000000` <br>

7. When the experiment is finished, the clients report Tx/Rx throughput, experiment time, and other related information. Request latency in microseconds is logged as a text file like `log-0-0-0-6-15-1-1-20-103000.txt`. The end line of the log contains the total experiment time. Therefore, when you analyze the log, you should be careful.


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
