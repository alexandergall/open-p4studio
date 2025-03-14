#include "tofino/intrinsic_metadata.p4"

/* Sample P4 program */
header_type ethernet_t {
    fields {
        dstAddr : 48;
        srcAddr : 48;
        etherType : 16;
    }
}

header_type ipv4_t {
    fields {
        version : 4;
        ihl : 4;
        diffserv : 8;
        totalLen : 16;
        identification : 16;
        flags : 3;
        fragOffset : 13;
        ttl : 8;
        protocol : 8;
        hdrChecksum : 16;
        srcAddr : 32;
        dstAddr: 32;
    }
}

header_type vlan_t {
     fields {
         pcp : 3;
         cfi : 1;
         vid : 12;
         etherType : 16;
     }
}

parser start {
    return parse_ethernet;
}

header ethernet_t ethernet;

parser parse_ethernet {
    extract(ethernet);
    return select(ethernet.etherType) {
        0x800 : parse_ipv4;
        0x8100: parse_vlan;
        default: ingress;
    }
}

header ipv4_t ipv4;
header vlan_t vlan;

parser parse_ipv4 {
    extract(ipv4);
    return ingress;
}

parser parse_vlan {
    extract(vlan);
    return ingress;
}


action action_0(my_param_0){
    add(vlan.etherType, vlan.etherType, my_param_0);
    //add(ethernet.etherType, ethernet.etherType, my_param_0);
    //add(ipv4.diffserv, ipv4.diffserv, -1);
}

table table_0 {
   reads {
     ipv4.srcAddr : lpm;
   }       
   actions {
     action_0;
   }
   max_size : 1024;
}


/* Main control flow */

control ingress {
    apply(table_0);
}
