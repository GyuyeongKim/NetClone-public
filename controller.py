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
NUM_SRV_CTRL = 8 # Total number of nodes
NUM_GRP = 30 # 6c2 * 2. number of groups. we have 6 servers and duplicates requsts, 6c2 = 15 * 2. why we doulbe the value is to maintain randomness in load balancing.
NUM_SRV = 6 # number of servers
SRV_START_IDX = 2 # server start index. 0~1 for clients, 2~7 for servers. 
RECIRC_PORT = 452 # Recirculation port number
USE_RACKSCHED = 0 # use racksched? 1: yes 0: no
def convert_to_hex(mac_address):
    return ':'.join(format(s, '02x') for s in mac_address.split(':'))

def hex2ip(hex_ip):
	addr_long = int(hex_ip,16)
	hex(addr_long)
	hex_ip = socket.inet_ntoa(struct.pack(">L", addr_long))
	return hex_ip

# Convert IP to bin
def ip2bin(ip):
	ip1 = ''.join([bin(int(x)+256)[3:] for x in ip.split('.')])
	return ip1

# Convert IP to hex
def ip2hex(ip):
	ip1 = ''.join([hex(int(x)+256)[3:] for x in ip.split('.')])
	return ip1

def table_add(target, table, keys, action_name, action_data=[]):
	keys = [table.make_key([gc.KeyTuple(*f)   for f in keys])]
	datas = [table.make_data([gc.DataTuple(*p) for p in action_data],
								  action_name)]
	table.entry_add(target, keys, datas)

def table_add2(target, table, keys, action_name, action_data=[]):
	keys = []
	datas = [table.make_data([gc.DataTuple(*p) for p in action_data],
								  action_name)]
	table.entry_add(target, keys, datas)
def table_mod(target, table, keys, action_name, action_data=[]):
	keys = [table.make_key([gc.KeyTuple(*f)   for f in keys])]
	datas = [table.make_data([gc.DataTuple(*p) for p in action_data],
								  action_name)]
	table.entry_mod(target, keys, datas)

def table_del(target, table, keys):
	table.entry_del(target, keys)

def table_get_srv(target, table, keys, pipeline):
	keys = [table.make_key([gc.KeyTuple(*f)   for f in keys])]
	for data,key in table.entry_get(target,keys):
		key_fields = key.to_dict()
		data_fields = data.to_dict()
		return data_fields['srv.f1'][pipeline]

def table_get_usecloning(target, table, keys, pipeline):
	keys = [table.make_key([gc.KeyTuple(*f)   for f in keys])]
	for data,key in table.entry_get(target,keys):
		key_fields = key.to_dict()
		data_fields = data.to_dict()
		return data_fields['usecloning.f1'][pipeline]

def get_port_status(target, table, keys):
	keys = [table.make_key([gc.KeyTuple(*f)   for f in keys])]
	for data,key in table.entry_get(target,keys):
		key_fields = key.to_dict()
		data_fields = data.to_dict()
		return data_fields[b'$PORT_UP']

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

	ip_list = [
	    0x0A000165,
	    0x0A000166,
	    0x0A000167,
	    0x0A000168,
		0x0A000169,
		0x0A00016A,
		0x0A00016B,
		0x0A00016C,
		0x0A00016D
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

	ipv4_exact = client.bfrt_info_get().table_get("pipe.SwitchIngress.ipv4_exact")
	table_clear(target, ipv4_exact)
	for i in range(NUM_SRV_CTRL):
		table_add(target, ipv4_exact,[("hdr.ipv4.dstAddr", ip_list[i])],"ipv4_forward",[("port",port_list[i])]) # 101

	RandomLB_table = client.bfrt_info_get().table_get("pipe.SwitchIngress.RandomLB_table")
	table_clear(target, RandomLB_table)
	for i in range(NUM_SRV):
		table_add(target, RandomLB_table,[("ig_md.srv1id", i)],"RandomLB_action",[("dst_addr",ip_list[i+SRV_START_IDX]), ("dst_mac",mac_list[i+SRV_START_IDX])])

	update_dstIP_table = client.bfrt_info_get().table_get("pipe.SwitchIngress.update_dstIP_table")
	table_clear(target, update_dstIP_table)
	for i in range(NUM_SRV):
		table_add(target, update_dstIP_table,[("ig_md.srv1id", i)],"update_dstIP_action",[("dst_addr",ip_list[i+SRV_START_IDX]), ("dst_mac",mac_list[i+SRV_START_IDX])])

	CloneForward_table = client.bfrt_info_get().table_get("pipe.SwitchIngress.CloneForward_table")
	table_clear(target, CloneForward_table)
	for i in range(NUM_SRV):
		table_add(target, CloneForward_table,[("hdr.netclone.sid", i)],"CloneForward_action",[("dst_addr",ip_list[i+SRV_START_IDX]), ("dst_mac",mac_list[i+SRV_START_IDX])])


	port_table = client.bfrt_info_get().table_get("$PORT")

	node_table = client.bfrt_info_get().table_get("$pre.node")
	table_clear(target, node_table)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 1)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+0],RECIRC_PORT])])]
	)

	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 2)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+0],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 3)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+0],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 4)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+0],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 5)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+0],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 6)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+1],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 7)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+1],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 8)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+1],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 9)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+1],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 10)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+2],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 11)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+2],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 12)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+2],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 13)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+3],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 14)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+3],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 15)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+4],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 16)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+1],RECIRC_PORT])])]
	)

	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 17)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+2],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 18)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+3],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 19)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+4],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 20)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+5],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 21)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+2],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 22)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+3],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 23)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+4],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 24)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+5],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 25)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+3],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 26)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+4],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 27)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+5],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 28)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+4],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 29)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+5],RECIRC_PORT])])]
	)
	node_table.entry_add(
		target,
		[node_table.make_key([
			gc.KeyTuple('$MULTICAST_NODE_ID', 30)])],
		[node_table.make_data([
			gc.DataTuple('$MULTICAST_RID', 1),
			gc.DataTuple('$MULTICAST_LAG_ID', int_arr_val=[]),
			gc.DataTuple('$DEV_PORT', int_arr_val=[port_list[SRV_START_IDX+5],RECIRC_PORT])])]
	)

	mgid_table = client.bfrt_info_get().table_get("$pre.mgid")
	table_clear(target, mgid_table)
	for i in range(1,NUM_GRP+1): # node table and mgid table must be syncronized for cloning.
		mgid_table.entry_add(
    		target,
    		[mgid_table.make_key([
    			gc.KeyTuple('$MGID', i)])],
    		[mgid_table.make_data([
    			gc.DataTuple('$MULTICAST_NODE_ID',  int_arr_val=[i]),
    			gc.DataTuple('$MULTICAST_NODE_L1_XID_VALID', bool_arr_val=[0]),
    			gc.DataTuple('$MULTICAST_NODE_L1_XID', int_arr_val=[0])])]
    	)


	get_srvID_table = client.bfrt_info_get().table_get("pipe.SwitchIngress.get_srvID_table")
	table_clear(target, get_srvID_table)
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 1)],"get_srvID_action",[("srv1",0), ("srv2",1)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 2)],"get_srvID_action",[("srv1",0), ("srv2",2)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 3)],"get_srvID_action",[("srv1",0), ("srv2",3)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 4)],"get_srvID_action",[("srv1",0), ("srv2",4)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 5)],"get_srvID_action",[("srv1",0), ("srv2",5)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 6)],"get_srvID_action",[("srv1",1), ("srv2",2)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 7)],"get_srvID_action",[("srv1",1), ("srv2",3)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 8)],"get_srvID_action",[("srv1",1), ("srv2",4)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 9)],"get_srvID_action",[("srv1",1), ("srv2",5)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 10)],"get_srvID_action",[("srv1",2), ("srv2",3)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 11)],"get_srvID_action",[("srv1",2), ("srv2",4)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 12)],"get_srvID_action",[("srv1",2), ("srv2",5)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 13)],"get_srvID_action",[("srv1",3), ("srv2",4)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 14)],"get_srvID_action",[("srv1",3), ("srv2",5)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 15)],"get_srvID_action",[("srv1",4), ("srv2",5)])

	# we need reverse form of the above table entries to maintain randomness.
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 16)],"get_srvID_action",[("srv1",1), ("srv2",0)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 17)],"get_srvID_action",[("srv1",2), ("srv2",0)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 18)],"get_srvID_action",[("srv1",3), ("srv2",0)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 19)],"get_srvID_action",[("srv1",4), ("srv2",0)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 20)],"get_srvID_action",[("srv1",5), ("srv2",0)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 21)],"get_srvID_action",[("srv1",2), ("srv2",1)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 22)],"get_srvID_action",[("srv1",3), ("srv2",1)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 23)],"get_srvID_action",[("srv1",4), ("srv2",1)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 24)],"get_srvID_action",[("srv1",5), ("srv2",1)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 25)],"get_srvID_action",[("srv1",3), ("srv2",2)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 26)],"get_srvID_action",[("srv1",4), ("srv2",2)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 27)],"get_srvID_action",[("srv1",5), ("srv2",2)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 28)],"get_srvID_action",[("srv1",4), ("srv2",3)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 29)],"get_srvID_action",[("srv1",5), ("srv2",3)])
	table_add(target, get_srvID_table,[("hdr.netclone.grp", 30)],"get_srvID_action",[("srv1",5), ("srv2",4)])

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
