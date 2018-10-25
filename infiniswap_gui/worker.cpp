#include <stdio.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include "grafana_socket.h"

using namespace std;

char ip[16];
const char *swap_area = "/dev/infiniswap0";
const char *portal_file_addr = "../setup/portal.list";
int last_version = -1;
const char *write_latency_files[] = {"/tmp/bd_write_latency_1", "/tmp/bd_write_latency_2", "/tmp/bd_write_latency_3"};
const char *read_latency_files[] = {"/tmp/bd_read_latency_1", "/tmp/bd_read_latency_2", "/tmp/bd_read_latency_3"};
const char *bd_info_files[] = {"/tmp/bd_info_1", "/tmp/bd_info_2", "/tmp/bd_info_3"};

int daemon_server_port = 11006;

vector<string> portal_ips;
bool bd_on = false; // record the block device status (reread the portal.list when bd restarts)

class latency_t
{
  public:
    vector<unsigned> read;                   // read latency data
    vector<unsigned> write;                  // write latency data
    vector<vector<unsigned>> sep_read_data;  // read latency data for each daemon this block device is mapping to
    vector<vector<unsigned>> sep_write_data; // write ------

    latency_t()
    {
        sep_read_data.resize(max_portal_num, vector<unsigned>());
        sep_write_data.resize(max_portal_num, vector<unsigned>());
    }

    void sort()
    {
        std::sort(read.begin(), read.end());
        std::sort(write.begin(), write.end());
        for (int i = 0; i < max_portal_num; i++)
        {
            std::sort(sep_read_data[i].begin(), sep_read_data[i].end());
            std::sort(sep_read_data[i].begin(), sep_read_data[i].end());
        }
    }

    // index: the cb_index in portal.list
    void insert(unsigned latency, int index, bool is_write)
    {
        if (is_write)
        {
            write.push_back(latency);
            sep_write_data[index].push_back(latency);
        }
        else
        {
            read.push_back(latency);
            sep_read_data[index].push_back(latency);
        }
    }

    void clear()
    {
        read.resize(0);
        write.resize(0);

        for (int i = 0; i < max_portal_num; i++)
        {
            sep_read_data[i].resize(0);
            sep_write_data[i].resize(0);
        }
    }
};

latency_t all_latency;

// read the portal.list file and
void parse_portal()
{
    portal_ips.resize(0);

    ifstream ifile;
    ifile.open(portal_file_addr);
    int portal_num;
    ifile >> portal_num;
    for (int i = 0; i < portal_num; i++)
    {
        string portal;
        ifile >> portal;                                         //portal is in the form of "ip:port"
        string ip = portal.substr(0, portal.find_first_of(':')); // get the ip address
        portal_ips.push_back(ip);
    }
    ifile.close();
}

void read_latency_file(const char *filename, unsigned size, bool is_write)
{
    ifstream ifile;
    ifile.open(filename);

    for (int i = 0; i < size; i++)
    {
        string s_latency;
        int index;
        ifile >> s_latency >> index;
        size_t pos = s_latency.find_first_not_of('\0');
        string ss_latency = s_latency.substr(pos, s_latency.size());
        all_latency.insert(atoi(ss_latency.c_str()), index, is_write);
    }

    ifile.close();
    remove(filename);
}

//0 < range <= 1
unsigned calculate_proportion(float range, bool is_write)
{
    if (is_write)
    {
        if (all_latency.write.size() == 0)
        {
            return 0;
        }
        int pos = (int)ceil(all_latency.write.size() * range) - 1;
        return all_latency.write[pos];
    }
    else
    {
        if (all_latency.read.size() == 0)
        {
            return 0;
        }
        int pos = (int)ceil(all_latency.read.size() * range) - 1;
        return all_latency.read[pos];
    }
}

int read_tp_and_latency(request_msg &msg)
{
    ifstream ifile;
    int version;

    ifile.open("/tmp/bd_version");
    if (!ifile){
        cerr << "Error: cannot open file bd_version\n";
        return -1;
    }
    ifile >> version;
    ifile.close();
    //cout << "bd file version is: " << version << endl;

    ifile.open(bd_info_files[version]);
    if (!ifile){
        cerr << "Error: cannot open file " << bd_info_files[version] << endl;
        return -1;
    }
    ifile >> msg.IO.pagein_speed >> msg.IO.pageout_speed >> msg.IO.total_IO >> msg.IO.remote_IO;
    ifile.close();

    read_latency_file(read_latency_files[version], msg.IO.pagein_speed, false);
    read_latency_file(write_latency_files[version], msg.IO.pageout_speed, true);

    all_latency.sort();

    msg.IO.pagein_latency = calculate_proportion(0.5, false);
    msg.IO.high_pagein_latency = calculate_proportion(0.9, false);
    msg.IO.low_pagein_latency = calculate_proportion(0.1, false);

    msg.IO.pageout_latency = calculate_proportion(0.5, true);
    msg.IO.high_pageout_latency = calculate_proportion(0.9, true);
    msg.IO.low_pageout_latency = calculate_proportion(0.1, true);

    // get the medium latency for each daemon the block device is mapping to
    msg.bd_portal_num = portal_ips.size();
    for (int i = 0; i < msg.bd_portal_num; i++)
    {
        strcpy(msg.bd_maps[i].daemon_ip, portal_ips[i].c_str());
        msg.bd_maps[i].pagein_speed = all_latency.sep_read_data[i].size();
        if (msg.bd_maps[i].pagein_speed)
        {
            msg.bd_maps[i].pagein_latency = all_latency.sep_read_data[i][msg.bd_maps[i].pagein_speed / 2];
        }
        msg.bd_maps[i].pageout_speed = all_latency.sep_write_data[i].size();
        if (msg.bd_maps[i].pageout_speed)
        {
            msg.bd_maps[i].pageout_latency = all_latency.sep_write_data[i][msg.bd_maps[i].pageout_speed / 2];
        }
    }

    all_latency.clear();
    return 0;
}

void send_to_server(request_msg &msg)
{
    int sock;
    struct sockaddr_in server;
    char buf[1024];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("opening stream socket");
        exit(1);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(server_ip);
    server.sin_port = htons(hostport);
    if (connect(sock, (struct sockaddr *)&server, sizeof server) == -1)
    {
        cerr << "Error: connect server socket " << server_ip << ':' << hostport << endl;
    }
    else
    {
        strcpy(msg.ip, ip);
        msg.time = time(0);

        send(sock, &msg, sizeof(request_msg), 0);
    }

    close(sock);
}

void send_to_daemon(control_msg &msg)
{
    int sock;
    struct sockaddr_in daemon_server;
    char buf[1024];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("opening stream socket");
        exit(1);
    }

    daemon_server.sin_family = AF_INET;
    daemon_server.sin_addr.s_addr = inet_addr("127.0.0.1");
    daemon_server.sin_port = htons(daemon_server_port);
    if (connect(sock, (struct sockaddr *)&daemon_server, sizeof daemon_server) == -1)
    {
        cerr << "Error: connect daemon socket 127.0.0.1:" << daemon_server_port << endl;
    }
    else
    {
        cout << "Send message to daemon: " << msg.cmd << endl;
        send(sock, &msg, sizeof(control_msg), 0);
    }

    close(sock);
}

void read_bd(request_msg &msg)
{
    char cmd[100];
    sprintf(cmd, "swapon -s | grep %s | wc -l > /tmp/bd_on", swap_area);
    system(cmd);
    ifstream ifile;
    ifile.open("/tmp/bd_on");
    ifile >> msg.bd_on;
    ifile.close();
    if (msg.bd_on)
    {
        // renew the portal ips when the bd restarts
        if (!bd_on)
        {
            parse_portal();
        }
        if (read_tp_and_latency(msg) == -1){
            msg.bd_on = false;
            return;
        }
    }
    bd_on = msg.bd_on;
}

void read_daemon(request_msg &msg)
{
    ifstream ifile;
    ifile.open("/tmp/daemon");
    if (!ifile){
        cerr << "Error: cannot open file /tmp/daemon" << endl;
    }
    ifile >> msg.daemon_on;
    if (msg.daemon_on)
    {
        int version;
        ifile >> version;
        //valid data if version is different
        if (version != last_version)
        {
            //cout << version << endl;
            last_version = version;
            ifile >> msg.ram.free >> msg.ram.filter_free >> msg.ram.allocated_not_mapped >>
                msg.ram.mapped >> msg.mapping.mem_status;
            for (int i = 0; i < msg.ram.mapped; i++)
            {
                ifile >> msg.mapping.map_infos[i].remote_ip >> msg.mapping.map_infos[i].remote_chunk_num;
            }
        }
        else
        {
            msg.daemon_on = false;
        }
    }
    ifile.close();
}

// start listening to server's commands
static int worker_init()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == -1)
    {
        cerr << "cannot open stream socket!\n";
        exit(1);
    }

    int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    //bind ip and port number to the socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(clientport);
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    socklen_t length = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &length) == -1)
    {
        exit(1);
    }

    listen(sock, 10);
    return sock;
}

//listen to the server's commands
static void worker_listen(int sock)
{
    while (true)
    {
        int msgsock = accept(sock, (struct sockaddr *)0, (socklen_t *)0);
        if (msgsock == -1)
        {
            cerr << "Error: accept socket connection\n";
        }
        else
        {
            control_msg msg;
            recv(msgsock, &msg, sizeof(msg), MSG_WAITALL);
            cout << "Cmd: " << msg.cmd << endl;
            send_to_daemon(msg);
        }
    }
}

int main(int argc, char **argv)
{
    // set ip address
    strcpy(ip, argv[1]);
    // set host port number and/or client port number if provided
    if (argc > 2)
    {
        hostport = atoi(argv[2]);
        if (argc > 3)
        {
            clientport = atoi(argv[3]);
            if (argc > 4)
            {
                strcpy(server_ip, argv[4]);
            }
        }
    }

    int sock = worker_init();
    thread worker_listen_t(worker_listen, sock);
    worker_listen_t.detach();

    while (true)
    {
        request_msg msg;
        memset(&msg, 0, sizeof(request_msg));
        read_bd(msg);
        read_daemon(msg);
        send_to_server(msg);
        sleep(send_interval);
    }
}