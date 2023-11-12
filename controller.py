# Configuration START 
RECIRC_PORT = 452 # Recirculation port number. This should be syncronized with the RECIRC_PORT in netclone.p4
# Configuration END

#!/usr/bin/env python3
import sys
import os
sys.path.append(os.path.expandvars('$SDE/install/lib/python3.8/site-packages/tofino/'))
sys.path.append(os.path.expandvars('$SDE/install/lib/python3.8/site-packages/tofino/bfrt_grpc'))
sys.path.append(os.path.expandvars('$SDE/install/lib/python3.8/site-packages/'))
sys.path.append(os.path.expandvars('$SDE/install/lib/python3.8/site-packages/tofinopd/'))
sys.path.append(os.path.expandvars('$SDE/install/lib/python3.8/site-packages/tofino_pd_api/'))
sys.path.append(os.path.expandvars('$SDE/install/lib/python3.8/site-packages/p4testutils'))
import time
import datetime
import grpc
import bfrt_grpc.bfruntime_pb2_grpc as bfruntime_pb2_grpc
import bfrt_grpc.bfruntime_pb2 as bfruntime_pb2
import bfrt_grpc.client as gc
import port_mgr_pd_rpc as mr
from time import sleep
import socket, struct
import binascii
from math import comb
import itertools

NUM_SRV_CTRL = int(sys.argv[1]) # Total number of nodes
NUM_SRV = int(sys.argv[2]) # number of servers
USE_RACKSCHED = int(sys.argv[3]) # use racksched? 1: yes 0: no
NUM_GRP = comb(NUM_SRV, 2)
NUM_CLI = NUM_SRV_CTRL - NUM_SRV - 1 # server start index 

def generate_table_add_calls(num_servers, num_groups):
    server_ids = list(range(num_servers))
    combinations = list(itertools.combinations(server_ids, 2))
    call_index = 1
    for first_server, second_server in combinations:
        if call_index <= num_groups * 2:
            # First call with original order
            table_add(target, get_srvID_table, [("hdr.netclone.grp", call_index)], "get_srvID_action", [("srv1", first_server), ("srv2", second_server)])
            call_index += 1
            
            # Second call with reversed order
            table_add(target, get_srvID_table, [("hdr.netclone.grp", call_index)], "get_srvID_action", [("srv1", second_server), ("srv2", first_server)])
            call_index += 1
            

def table_add(target, table, keys, action_name, action_data=[]):
	keys = [table.make_key([gc.KeyTuple(*f)   for f in keys])]
	datas = [table.make_data([gc.DataTuple(*p) for p in action_data],
								  action_name)]
	table.entry_add(target, keys, datas)

def table_clear(target, table):
	keys = []
	for data,key in table.entry_get(target):
		if key is not None:
			keys.append(key)
	if keys:
		table.entry_del(target, keys)
try:
	grpc_addr = "localhost:50052"
	client_id = 0
	device_id = 0
	pipe_id = 0xFFFF
	is_master = True
	client = gc.ClientInterface(grpc_addr, client_id, device_id)
	target = gc.Target(device_id, pipe_id)
	client.bind_pipeline_config("netclone")

	# Configuration Start
	ip_list = [
	    0x0A000165, # node1
	    0x0A000166, # node2
	    0x0A000167,
	    0x0A000168,
		0x0A000169,
		0x0A00016A,
		0x0A00016B,
		0x0A00016C
		]
	port_list = [
	    396, 
	    392,
	    444,
	    440,
		428,
		424,
		412,
		408
	]
	mac_list = [
	    0x0c42a12f12e6,
	    0x0c42a12f11c6,
	    0x1070fd1cd4b8,
	    0x1070fd0dd54c,
		0x1070fd1ccab8,
		0x1070fd1ccab4,
		0x1070fd1cc2c8,
		0x1070fd1cd40c
	]
	# Configuration End

	ipv4_exact = client.bfrt_info_get().table_get("pipe.SwitchIngress.ipv4_exact")
	table_clear(target, ipv4_exact)
	for i in range(NUM_SRV_CTRL):
		table_add(target, ipv4_exact,[("hdr.ipv4.dstAddr", ip_list[i])],"ipv4_forward",[("port",port_list[i])]) 

	RandomLB_table = client.bfrt_info_get().table_get("pipe.SwitchIngress.RandomLB_table")
	table_clear(target, RandomLB_table)
	for i in range(NUM_SRV):
		table_add(target, RandomLB_table,[("ig_md.srv1id", i)],"RandomLB_action",[("dst_addr",ip_list[i+NUM_CLI+1]), ("dst_mac",mac_list[i+NUM_CLI+1])])

	update_dstIP_table = client.bfrt_info_get().table_get("pipe.SwitchIngress.update_dstIP_table")
	table_clear(target, update_dstIP_table)
	for i in range(NUM_SRV):
		table_add(target, update_dstIP_table,[("ig_md.srv1id", i)],"update_dstIP_action",[("dst_addr",ip_list[i+NUM_CLI+1]), ("dst_mac",mac_list[i+NUM_CLI+1])])

	CloneForward_table = client.bfrt_info_get().table_get("pipe.SwitchIngress.CloneForward_table")
	table_clear(target, CloneForward_table)
	for i in range(NUM_SRV):
		table_add(target, CloneForward_table,[("hdr.netclone.sid", i)],"CloneForward_action",[("dst_addr",ip_list[i+NUM_CLI+1]), ("dst_mac",mac_list[i+NUM_CLI+1])])

	get_srvID_table = client.bfrt_info_get().table_get("pipe.SwitchIngress.get_srvID_table")
	table_clear(target, get_srvID_table)
	generate_table_add_calls(NUM_SRV, NUM_GRP)
	# Node_table and mgid_table are need for request cloning
	node_table = client.bfrt_info_get().table_get("$pre.node")
	table_clear(target, node_table)
	# Generate all combinations of server indices
	server_combinations = list(itertools.combinations(range(NUM_SRV), 2))

	for i in range(1, (NUM_GRP * 2) + 1):
		# Calculate the combination index
		combination_index = (i - 1) // 2

		# Get the server indices from the precomputed combinations list
		first_server_index, second_server_index = server_combinations[combination_index]

		# Reverse the order for every alternate group
		if i % 2 == 0:
			first_server_index, second_server_index = second_server_index, first_server_index

		# Make the node_table.entry_add call
		node_table.entry_add(
			target,
			[node_table.make_key([
				gc.KeyTuple('$MULTICAST_NODE_ID', i)])],
			[node_table.make_data([
				gc.DataTuple('$MULTICAST_RID', 1),
				gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
				gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[NUM_CLI + first_server_index+1], RECIRC_PORT])])]
		)

	# Node_table and mgid_table are need for request cloning	
	mgid_table = client.bfrt_info_get().table_get("$pre.mgid")
	table_clear(target, mgid_table)
	for i in range(1,(NUM_GRP*2)+1):
		mgid_table.entry_add(
    		target,
    		[mgid_table.make_key([
    			gc.KeyTuple('$MGID', i)])],
    		[mgid_table.make_data([
    			gc.DataTuple('$MULTICAST_NODE_ID',  int_arr_val=[i]),
    			gc.DataTuple('$MULTICAST_NODE_L1_XID_VALID', bool_arr_val=[0]),
    			gc.DataTuple('$MULTICAST_NODE_L1_XID', int_arr_val=[0])])]
    	)

	# init srv load registers
	SRV= client.bfrt_info_get().table_get("pipe.srv")
	for i in range(NUM_SRV):
		SRV.entry_mod(
			target,
			[SRV.make_key([gc.KeyTuple('$REGISTER_INDEX', i)])],
			[SRV.make_data(
				[gc.DataTuple('srv.f1', 0)])])

	# init srv load registers (shadow table)
	SRV2= client.bfrt_info_get().table_get("pipe.srv2")
	for i in range(NUM_SRV):
		SRV2.entry_mod(
			target,
			[SRV2.make_key([gc.KeyTuple('$REGISTER_INDEX', i)])],
			[SRV2.make_data(
				[gc.DataTuple('srv2.f1', 0)])])

	# use RackSched as well?
	racksched= client.bfrt_info_get().table_get("pipe.racksched_reg")
	racksched.entry_mod(
		target,
		[racksched.make_key([gc.KeyTuple('$REGISTER_INDEX', 0)])],
		[racksched.make_data(
			[gc.DataTuple('racksched_reg.f1', USE_RACKSCHED)])])

finally:
	client.tear_down_stream()
