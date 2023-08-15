#include <core.p4>
#if __TARGET_TOFINO__ == 2
#include <t2na.p4>
#else
#include <tna.p4>
#endif
#define OP_READ 0
#define OP_R_REPLY 1
#define OP_WRITE 2
#define OP_W_REPLY 3
#define MAX_OBJ 131072
#define MAX_SRV 6
#define MAX_SEQ 100000000
#define NETCLONE_PORT_0 1000
#define NETCLONE_PORT_1 1001
#define NETCLONE_PORT_2 1002
#define NETCLONE_PORT_3 1003
#define NETCLONE_PORT_4 1004
#define NETCLONE_PORT_5 1005
#define NETCLONE_PORT_6 1006
#define NETCLONE_PORT_7 1007
#define NETCLONE_PORT_8 1008
#define NETCLONE_PORT_9 1009
#define NETCLONE_PORT_10 1010
#define NETCLONE_PORT_11 1011
#define NETCLONE_PORT_12 1012
#define NETCLONE_PORT_13 1013
#define NETCLONE_PORT_14 1014
#define NETCLONE_PORT_15 1015
/*************************************************************************
*********************** H E A D E R S  ***********************************
*************************************************************************/

typedef bit<16> ether_type_t;
const ether_type_t TYPE_IPV4 = 0x800;
typedef bit<8> trans_protocol_t;
const trans_protocol_t TYPE_TCP = 6;
const trans_protocol_t TYPE_UDP = 17;

header ethernet_h {
    bit<48>   dstAddr;
    bit<48>   srcAddr;
    bit<16>   etherType;
}

header netclone_h {
    bit<32> op;
    bit<32> seq;
    bit<32> grp;
    bit<32> sid;
    bit<32> load;
    bit<32> clo;
    bit<32> tidx;
}

header ipv4_h {
    bit<4>   version;
    bit<4>   ihl;
    bit<6>   dscp;
    bit<2>   ecn;
    bit<16>  totalLen;
    bit<16>  identification;
    bit<3>   flags;
    bit<13>  frag_offset;
    bit<8>   ttl;
    bit<8>   protocol;
    bit<16>  hdrChecksum;
    bit<32>  srcAddr;
    bit<32>  dstAddr;
}

header tcp_h {
    bit<16> srcport;
    bit<16> dstport;
    bit<32> seqNo;
    bit<32> ackNo;
    bit<4> dataOffset;
    bit<3>  res;
    bit<3>  ecn;
    bit<6>  ctrl;
    bit<16> window;
    bit<16> checksum;
    bit<16> urgent_ptr;
}

header udp_h {
    bit<16> srcPort;
    bit<16> dstPort;
    bit<16> len;
    bit<16> checksum;
}
struct header_t {
    ethernet_h ethernet;
    ipv4_h ipv4;
    tcp_h tcp;
    udp_h udp;
    netclone_h netclone;
}

struct metadata_t {
    bit<8> update_result;
    bit<32> hash;
    bit<32> srv1pass;
    bit<32> srv2pass;
    bit<32> srv1id;
    bit<32> srv2id;
    bit<32> racksched;
}

struct custom_metadata_t {

}

struct empty_header_t {
    ethernet_h ethernet;
    ipv4_h ipv4;
    tcp_h tcp;
    udp_h udp;
    netclone_h netclone;
}

struct empty_metadata_t {
    custom_metadata_t custom_metadata;
}


Register<bit<32>,_>(MAX_SRV,0) srv;
Register<bit<32>,_>(MAX_SRV,0) srv2;
Register<bit<32>,_>(MAX_OBJ,0) req;
Register<bit<32>,_>(MAX_OBJ,0) req2;
Register<bit<32>,_>(MAX_OBJ,0) req3;
Register<bit<32>,_>(MAX_OBJ,0) req4;
Register<bit<32>,_>(1,0) seq;
Register<bit<32>,_>(1,0) racksched_reg;
/*************************************************************************
*********************** P A R S E R  ***********************************
*************************************************************************/

parser SwitchIngressParser(
        packet_in pkt,
        out header_t hdr,
        out metadata_t ig_md,
        out ingress_intrinsic_metadata_t ig_intr_md) {

    state start {
        pkt.extract(ig_intr_md);
        pkt.advance(PORT_METADATA_SIZE);
        transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition select(hdr.ethernet.etherType) {
            TYPE_IPV4: parse_ipv4;
            default: accept;
        }
    }

    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            TYPE_UDP: parse_udp;
            default: accept;
        }
    }

    state parse_udp {
        pkt.extract(hdr.udp);
        transition select(hdr.udp.dstPort){
            NETCLONE_PORT_0: parse_netclone;
            NETCLONE_PORT_1: parse_netclone;
            NETCLONE_PORT_2: parse_netclone;
            NETCLONE_PORT_3: parse_netclone;
            NETCLONE_PORT_4: parse_netclone;
            NETCLONE_PORT_5: parse_netclone;
            NETCLONE_PORT_6: parse_netclone;
            NETCLONE_PORT_7: parse_netclone;
            NETCLONE_PORT_8: parse_netclone;
            NETCLONE_PORT_9: parse_netclone;
            NETCLONE_PORT_10: parse_netclone;
            NETCLONE_PORT_11: parse_netclone;
            NETCLONE_PORT_12: parse_netclone;
            NETCLONE_PORT_13: parse_netclone;
            NETCLONE_PORT_14: parse_netclone;
            NETCLONE_PORT_15: parse_netclone;
            default: parse_udp2;
        }
    }

    state parse_udp2 {
        //transition parse_netclone;
        transition select(hdr.udp.srcPort){
            NETCLONE_PORT_0: parse_netclone;
            NETCLONE_PORT_1: parse_netclone;
            NETCLONE_PORT_2: parse_netclone;
            NETCLONE_PORT_3: parse_netclone;
            NETCLONE_PORT_4: parse_netclone;
            NETCLONE_PORT_5: parse_netclone;
            NETCLONE_PORT_6: parse_netclone;
            NETCLONE_PORT_7: parse_netclone;
            NETCLONE_PORT_8: parse_netclone;
            NETCLONE_PORT_9: parse_netclone;
            NETCLONE_PORT_10: parse_netclone;
            NETCLONE_PORT_11: parse_netclone;
            NETCLONE_PORT_12: parse_netclone;
            NETCLONE_PORT_13: parse_netclone;
            NETCLONE_PORT_14: parse_netclone;
            NETCLONE_PORT_15: parse_netclone;
            default: accept;
        }
    }
        state parse_netclone {
        pkt.extract(hdr.netclone);
        transition accept;
    }

}

/*************************************************************************
**************  I N G R E S S   P R O C E S S I N G   *******************
*************************************************************************/

control SwitchIngress(
        inout header_t hdr,
        inout metadata_t ig_md,
        in ingress_intrinsic_metadata_t ig_intr_md,
        in ingress_intrinsic_metadata_from_parser_t ig_intr_prsr_md,
        inout ingress_intrinsic_metadata_for_deparser_t ig_intr_dprsr_md,
        inout ingress_intrinsic_metadata_for_tm_t ig_tm_md) {

    action drop() {
        ig_intr_dprsr_md.drop_ctl=1;
    }

    action ipv4_forward(bit<9> port) {
        ig_tm_md.ucast_egress_port = port;
    }

    table ipv4_exact {
        key = {
            hdr.ipv4.dstAddr: exact;
        }
        actions = {
            ipv4_forward;
            drop;
            NoAction;
        }
        size = 16;
       default_action = drop();
    }

    RegisterAction<bit<32>, _, bit<32>>(seq) inc_seq = {
        void apply(inout bit<32> reg_value, out bit<32> return_value) {
            if(reg_value == MAX_SEQ) reg_value = 1;
            else reg_value = reg_value + 1;

            return_value = reg_value;
        }
    };

    action inc_seq_action(){
        hdr.netclone.seq = inc_seq.execute(0);
    }

    table inc_seq_table{
        actions = {
            inc_seq_action;
        }
        size = 1;
        default_action = inc_seq_action;
    }

    RegisterAction<bit<32>, _, bit<32>>(req4) update_list4 = {
        void apply(inout bit<32> reg_value, out bit<32> return_value) {
            if (reg_value == hdr.netclone.seq){
                reg_value = 0;
                return_value = 1;
            }
            else{
                reg_value = hdr.netclone.seq;
                return_value = 0;
            }
        }
    };

    action update_list_action4(){
        ig_md.update_result = (bit<8>)update_list4.execute(ig_md.hash);
    }

    table update_list_table4{
        actions = {
            update_list_action4;
        }
        size = 1;
        default_action = update_list_action4;
    }

    RegisterAction<bit<32>, _, bit<32>>(req3) update_list3 = {
        void apply(inout bit<32> reg_value, out bit<32> return_value) {
            if (reg_value == hdr.netclone.seq){
                reg_value = 0;
                return_value = 1;
            }
            else{
                reg_value = hdr.netclone.seq;
                return_value = 0;
            }
        }
    };

    action update_list_action3(){
        ig_md.update_result = (bit<8>)update_list3.execute(ig_md.hash);
    }

    table update_list_table3{
        actions = {
            update_list_action3;
        }
        size = 1;
        default_action = update_list_action3;
    }

    RegisterAction<bit<32>, _, bit<32>>(req2) update_list2 = {
        void apply(inout bit<32> reg_value, out bit<32> return_value) {
            if (reg_value == hdr.netclone.seq){
                reg_value = 0;
                return_value = 1;
            }
            else{
                reg_value = hdr.netclone.seq;
                return_value = 0;
            }
        }
    };

    action update_list_action2(){
        ig_md.update_result = (bit<8>)update_list2.execute(ig_md.hash);
    }

    table update_list_table2{
        actions = {
            update_list_action2;
        }
        size = 1;
        default_action = update_list_action2;
    }

    RegisterAction<bit<32>, _, bit<32>>(req) update_list = {
        void apply(inout bit<32> reg_value, out bit<32> return_value) {
            if (reg_value == hdr.netclone.seq){
                reg_value = 0;
                return_value = 1;
            }
            else{
                reg_value = hdr.netclone.seq;
                return_value = 0;
            }
        }
    };

    action update_list_action(){
        ig_md.update_result = (bit<8>)update_list.execute(ig_md.hash);
    }

    table update_list_table{
        actions = {
            update_list_action;
        }
        size = 1;
        default_action = update_list_action;
    }

    action get_hash_action(){
        ig_md.hash = hdr.netclone.seq%MAX_OBJ;
    }

    table get_hash_table{
        actions = {
            get_hash_action;
        }
        size = 1;
        default_action = get_hash_action;
    }


    action msg_cloning_action(){
        ig_tm_md.rid = 1;
        ig_tm_md.mcast_grp_a = (bit<16>)hdr.netclone.grp;

    }

    table msg_cloning_table{
        actions = {
            msg_cloning_action;
        }
        size = 1;
        default_action = msg_cloning_action;
    }

    RegisterAction<bit<32>, _, bit<32>>(srv) update_srv = {
        void apply(inout bit<32> reg_value) {
            reg_value = hdr.netclone.load;

        }
    };

    action update_srv_action(){
        update_srv.execute(hdr.netclone.sid);
    }

    table update_srv_table{
        actions = {
            update_srv_action;
        }
        size = 1;
        default_action = update_srv_action;
    }


    RegisterAction<bit<32>, _, bit<32>>(srv2) update_srv2 = {
        void apply(inout bit<32> reg_value) {
            reg_value = hdr.netclone.load;

        }
    };

    action update_srv2_action(){
        update_srv2.execute(hdr.netclone.sid);
    }

    table update_srv2_table{
        actions = {
            update_srv2_action;
        }
        size = 1;
        default_action = update_srv2_action;
    }


    RegisterAction<bit<32>, _, bit<32>>(srv) read_srv1 = {
        void apply(inout bit<32> reg_value, out bit<32> return_value) {
            return_value = reg_value;
        }
    };

    action read_srv_action1(){
        ig_md.srv1pass = read_srv1.execute(ig_md.srv1id);
    }


    table read_srv1_table{
        actions = {
            read_srv_action1;
        }
        size = 1;
        default_action = read_srv_action1;
    }


    RegisterAction<bit<32>, _, bit<32>>(srv2) read_srv2_nonzero = {
        void apply(inout bit<32> reg_value, out bit<32> return_value) {
            if (ig_md.srv1pass > reg_value ) return_value = 2;
            else return_value = 1;

        }
    };

    RegisterAction<bit<32>, _, bit<32>>(srv2) read_srv2_zero = {
        void apply(inout bit<32> reg_value, out bit<32> return_value) {
            if (reg_value == 0 ) return_value = 0;
            else return_value = 1;
        }
    };



    action get_srvID_action(bit<32> srv1, bit<32> srv2){
        ig_md.srv1id = srv1;
        ig_md.srv2id = srv2;
    }


    table get_srvID_table{
        key = {
            hdr.netclone.grp: exact;
        }
        actions = {
            get_srvID_action;
        }
        size = 256;
        default_action = get_srvID_action(0,1);
    }


    action RandomLB_action(bit<32> dst_addr,bit<48> dst_mac){
        hdr.ipv4.dstAddr = dst_addr;
        hdr.ethernet.dstAddr = dst_mac;
    }

    table RandomLB_table{
        key = {
            ig_md.srv1id: exact;
        }
        actions = {
            RandomLB_action;
            drop;
        }
        size = MAX_SRV;
        default_action = drop();
    }

    action update_dstIP_action(bit<32> dst_addr,bit<48> dst_mac){
        hdr.ipv4.dstAddr = dst_addr;
        hdr.ethernet.dstAddr = dst_mac;
    }

    table update_dstIP_table{
        key = {
            ig_md.srv1id: exact;
        }
        actions = {
            update_dstIP_action;
            drop;
        }
        size = MAX_SRV;
        default_action = drop();
    }


    action CloneForward_action(bit<32> dst_addr,bit<48> dst_mac){
        hdr.ipv4.dstAddr = dst_addr;
        hdr.ethernet.dstAddr = dst_mac;
    }

    table CloneForward_table{
        key = {
            hdr.netclone.sid: exact;
        }
        actions = {
            CloneForward_action;
            drop;
        }
        size = MAX_SRV;
        default_action = drop();
    }

    RegisterAction<bit<32>, _, bit<32>>(racksched_reg) read_racksched = {
        void apply(inout bit<32> reg_value, out bit<32> return_value) {
            return_value = reg_value;
        }
    };
    apply {
        /*************** NetLR Block START *****************************/
            if (ig_intr_md.ingress_port == 452) { // Cloned recirculated packets
                hdr.netclone.clo = 2;// Second packet
                CloneForward_table.apply();
                ipv4_exact.apply();
            }
            else if (hdr.netclone.isValid()) {
                ig_md.srv1pass = 0;
                ig_md.srv2pass = 0;
                hdr.udp.checksum = 0; // Disable UDP checksum.
                ig_md.racksched = read_racksched.execute(0);
                if(hdr.netclone.op == OP_READ){
                    inc_seq_table.apply();
                    get_srvID_table.apply();
                    read_srv1_table.apply();
                    if(ig_md.racksched == 1){
                        if(ig_md.srv1pass == 0) ig_md.srv2pass = read_srv2_zero.execute(ig_md.srv2id);
                        else ig_md.srv2pass = read_srv2_nonzero.execute(ig_md.srv2id);

                    }
                    else if(ig_md.racksched==0){
                        if(ig_md.srv1pass == 0) ig_md.srv2pass = read_srv2_zero.execute(ig_md.srv2id);
                        else ig_md.srv2pass = 1;

                    }
                    else drop();
                    if(ig_md.srv2pass == 0){
                        update_dstIP_table.apply();
                        hdr.netclone.clo = 1;
                        hdr.netclone.sid = ig_md.srv2id;
                        msg_cloning_table.apply();
                    }
                    else{
                        if(ig_md.srv2pass == 2) ig_md.srv1id = ig_md.srv2id;
                        RandomLB_table.apply();
                        ipv4_exact.apply();
                    }
                }
                else if(hdr.netclone.op == OP_R_REPLY){
                    update_srv_table.apply();
                    update_srv2_table.apply();
                    if(hdr.netclone.clo > 0){ 
                        get_hash_table.apply();
                        if(hdr.netclone.tidx == 0) update_list_table.apply();
                        else if(hdr.netclone.tidx == 1) update_list_table2.apply();
                        //else if(hdr.netclone.tidx == 2) update_list_table3.apply();
                        //else if(hdr.netclone.tidx == 3) update_list_table4.apply();
                        else drop();
                        if(ig_md.update_result == 1) drop();
                        else ipv4_exact.apply();
                    }
                    else ipv4_exact.apply();
                }
                else if(hdr.netclone.op == OP_WRITE || hdr.netclone.op == OP_W_REPLY) ipv4_exact.apply();
                else drop();
            }
            else ipv4_exact.apply();
    }
}



/*************************************************************************
***********************  D E P A R S E R  *******************************
*************************************************************************/
control SwitchIngressDeparser(
        packet_out pkt,
        inout header_t hdr,
        in metadata_t ig_md,
        in ingress_intrinsic_metadata_for_deparser_t ig_intr_dprsr_md) {

    Checksum() ipv4_checksum;

    apply {

        hdr.ipv4.hdrChecksum = ipv4_checksum.update(
                        {hdr.ipv4.version,
                         hdr.ipv4.ihl,
                         hdr.ipv4.dscp,
                         hdr.ipv4.ecn,
                         hdr.ipv4.totalLen,
                         hdr.ipv4.identification,
                         hdr.ipv4.flags,
                         hdr.ipv4.frag_offset,
                         hdr.ipv4.ttl,
                         hdr.ipv4.protocol,
                         hdr.ipv4.srcAddr,
                         hdr.ipv4.dstAddr});


        pkt.emit(hdr);
    }
}

/*************************************************************************
****************  E G R E S S   P R O C E S S I N G   *******************
*************************************************************************/
parser SwitchEgressParser(
        packet_in pkt,
        out empty_header_t hdr,
        out empty_metadata_t eg_md,
        out egress_intrinsic_metadata_t eg_intr_md) {
    state start {
        pkt.extract(eg_intr_md);
        transition accept;
    }
}

control SwitchEgressDeparser(
        packet_out pkt,
        inout empty_header_t hdr,
        in empty_metadata_t eg_md,
        in egress_intrinsic_metadata_for_deparser_t ig_intr_dprs_md) {
    apply {
        pkt.emit(hdr);
    }
}

control SwitchEgress(
        inout empty_header_t hdr,
        inout empty_metadata_t eg_md,
        in egress_intrinsic_metadata_t eg_intr_md,
        in egress_intrinsic_metadata_from_parser_t eg_intr_md_from_prsr,
        inout egress_intrinsic_metadata_for_deparser_t ig_intr_dprs_md,
        inout egress_intrinsic_metadata_for_output_port_t eg_intr_oport_md) {
    apply {

    }
}
/*************************************************************************
***********************  S W I T C H  *******************************
*************************************************************************/
Pipeline(SwitchIngressParser(),
         SwitchIngress(),
         SwitchIngressDeparser(),
         SwitchEgressParser(),
         SwitchEgress(),
         SwitchEgressDeparser()) pipe;

Switch(pipe) main;
