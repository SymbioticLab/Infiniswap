#ifndef __GRAFANA_SOCKET_H__
#define __GRAFANA_SOCKET_H__

#include <ctime>
#include <string>
#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <thread>
#include <cstdlib>
#include <unistd.h>
#include <queue>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <mutex>


int hostport = 10219; // port of master (server)
int clientport = 10220; // port of client (worker)
char server_ip[] = "128.110.96.133";
const int send_interval = 1; // the interval (seconds) a client sending message to server
const int process_interval = 10; // the interval (seconds) server process data
const int TOTALRAM = 64; // 64GB
const int MAX_FREE_MEM_GB = 32;
const int max_portal_num = 10; // how many daemon can a block device connects to at most

struct ram_t{
    int mapped;
    int free;
    int filter_free;
    int allocated_not_mapped; 
};

ram_t operator+(const ram_t & a, const ram_t & b){
    ram_t result;
    result.mapped = a.mapped + b.mapped;
    result.free = a.free + b.free;
    result.filter_free = a.filter_free + b.filter_free;
    result.allocated_not_mapped = a.allocated_not_mapped + b.allocated_not_mapped;
    return result;
}

ram_t operator/(const ram_t & a, int b){
    ram_t result;
    result.mapped = a.mapped / b;
    result.free = a.free / b;
    result.filter_free = a.filter_free / b;
    result.allocated_not_mapped = a.allocated_not_mapped / b;
    return result;
}

struct IO_para{
    int pageout_speed;
    int pagein_speed;
    int pageout_latency;
    int pagein_latency;
    int high_pageout_latency;
    int low_pageout_latency;
    int high_pagein_latency;
    int low_pagein_latency;
    int total_IO;
    int remote_IO;
};

IO_para operator+(const IO_para & a, const IO_para & b){
    IO_para result;
    result.pageout_speed = a.pageout_speed + b.pageout_speed;
    result.pagein_speed = a.pagein_speed + b.pagein_speed;
    result.pageout_latency = a.pageout_latency + b.pageout_latency;
    result.pagein_latency = a.pagein_latency + b.pagein_latency;
    result.high_pageout_latency = a.high_pageout_latency + b.high_pageout_latency;
    result.high_pagein_latency = a.high_pagein_latency + b.high_pagein_latency;
    result.low_pageout_latency = a.low_pageout_latency + b.low_pageout_latency;
    result.low_pagein_latency = a.low_pagein_latency + b.low_pagein_latency;
    result.total_IO = a.total_IO + b.total_IO;
    result.remote_IO = a.remote_IO + b.remote_IO;
    return result;
}

IO_para operator/(const IO_para & a,int  b){
    IO_para result = a;
    result.pageout_latency = a.pageout_latency / b;
    result.pagein_latency = a.pagein_latency / b;
    result.high_pageout_latency = a.high_pageout_latency / b;
    result.high_pagein_latency = a.high_pagein_latency / b;
    result.low_pageout_latency = a.low_pageout_latency / b;
    result.low_pagein_latency = a.low_pagein_latency / b;
    result.total_IO = a.total_IO / b;
    result.remote_IO = a.remote_IO / b;
    result.pagein_speed = a.pagein_speed / b;
    result.pageout_speed = a.pageout_speed / b;
    return result;
}

struct map_info{
    char remote_ip[16];
    int remote_chunk_num;
};

struct mapping_relation{
    char mem_status[MAX_FREE_MEM_GB + 1]; // 0 for free, 1 for allocated but not mapped, 2 for mapped
    map_info map_infos[MAX_FREE_MEM_GB];
};

struct bd_mapping{
    char daemon_ip[16];
    int pagein_speed;
    int pageout_speed;
    int pagein_latency;
    int pageout_latency;
};

// client send to server, include the information of the client device
struct request_msg{
    char ip[16];
    bool bd_on;
    bool daemon_on;
    time_t time;
    ram_t ram;
    IO_para IO;
    mapping_relation mapping;
    bd_mapping bd_maps[max_portal_num];
    int bd_portal_num; // the number of daemon the bd has in its portal.list file
};

// server send to client, include the direction from dashboard
struct control_msg{
    char cmd[20];
};

#endif