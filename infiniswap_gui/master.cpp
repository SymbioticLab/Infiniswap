#include "grafana_socket.h"
#include <sys/un.h>
#include "mysql.h"

using namespace std;

const int device_num = 5; // (max) total number of divices
const int delay_time = 5; // process data before delay_time
const string db_tables[] = {"general_info", "block_device", "daemon", "daemon_mem", "daemon_mapping", "bd_mapping"};
const int DELETE_INTERVAL = 300;

mutex db_lock;
mutex info_lock;

class compare_by_time
{
  public:
    bool operator()(request_msg m1, request_msg m2)
    {
        return m1.time > m2.time;
    }
};

// each client's request is put into a priority queue sorted by time
// select infos and delete the old ones when necessary
class all_info
{
  public:
    priority_queue<request_msg, vector<request_msg>, compare_by_time> infos;
    void add_info(request_msg msg)
    {
        info_lock.lock();
        infos.push(msg);
        info_lock.unlock();
    }
    //select infos from time to time+interval
    void select_infos(time_t time, vector<request_msg> *msgs)
    {
        info_lock.lock();
        while (true)
        {
            if (infos.empty())
            {
                break;
            }
            if (infos.top().time < time)
            {
                infos.pop();
            }
            else if (infos.top().time < time + process_interval)
            {
                msgs->push_back(infos.top());
                infos.pop();
            }
            else
            {
                break;
            }
        }
        info_lock.unlock();
    }
};

MYSQL *conn;
all_info infos;

// start the server
static int server_init()
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
    addr.sin_addr.s_addr = inet_addr(server_ip);
    addr.sin_port = htons(hostport);
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    socklen_t length = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &length) == -1)
    {
        exit(1);
    }

    listen(sock, 10);
    return sock;
}

// do mysql instructions
static void put_data_into_mysql(char *str)
{
    db_lock.lock();
    int res = mysql_query(conn, str);
    db_lock.unlock();

    if (!res)
    {
        //printf("Affected %lu rows", (unsigned long)mysql_affected_rows(conn));
    }
    else
    {
        fprintf(stderr, "Mysql error %d: %s\n", mysql_errno(conn),
                mysql_error(conn));
        exit(1);
    }
}

// every $process_interval$ seconds, average the data from each request
// and put the data into mysql
static void process_data()
{
    while (true)
    {
        // calculate the total RAM, average speed and latency
        vector<request_msg> msgs;
        time_t t = time(0);
        t -= (process_interval + delay_time);
        infos.select_infos(t, &msgs);
        ram_t total_ram;
        memset(&total_ram, 0, sizeof(ram_t));
        IO_para total_IO;
        memset(&total_IO, 0, sizeof(IO_para));
        int total_bd = 0, total_daemon = 0;
        unordered_set<string> ips;
        unordered_map<string, int> ip_times;
        unordered_map<string, request_msg> ip_average;
        if (!msgs.empty())
        {
            for (request_msg info : msgs)
            {
                string info_ip = info.ip;
                if (ips.find(info_ip) == ips.end())
                {
                    ips.insert(info_ip);
                    ip_times[info_ip] = 1;
                    ip_average[info_ip] = info;
                    if (info.bd_on)
                    {
                        total_bd++;
                    }
                    if (info.daemon_on)
                    {
                        total_daemon++;
                    }
                }
                else
                {
                    ip_times[info_ip]++;
                    //sum up
                    ip_average[info_ip].ram = ip_average[info_ip].ram + info.ram;
                    ip_average[info_ip].IO = ip_average[info_ip].IO + info.IO;
                }
            }
            for (string ip : ips)
            {
                //get average for each ip
                ip_average[ip].ram = ip_average[ip].ram / ip_times[ip];
                ip_average[ip].IO = ip_average[ip].IO / ip_times[ip];
                total_ram = total_ram + ip_average[ip].ram;
                total_IO = total_IO + ip_average[ip].IO;
            }
            if (total_bd > 1)
            {
                total_IO.pagein_latency /= total_bd;
                total_IO.pageout_latency /= total_bd;
            }
        }
        tm *my_tm = localtime(&t);
        char timestamp[30];
        sprintf(timestamp, "%d-%d-%d %d:%d:%d", 1900 + my_tm->tm_year,
                my_tm->tm_mon, my_tm->tm_mday, my_tm->tm_hour, my_tm->tm_min, my_tm->tm_sec);

        //cout << msgs.size() << endl;
        // put the data into mysql
        char str[500];
        sprintf(str,
                "INSERT INTO general_info (total_IO, remote_IO, pagein_throughput, pageout_throughput, pagein_latency, pageout_latency, time, device_num, bd_num, daemon_num, RAM_free, RAM_filter_free, RAM_allocated, RAM_mapped) VALUES (%d, %d, %d, %d, %d, %d, NOW(), %d, %d, %d, %d, %d, %d, %d)",
                total_IO.total_IO, total_IO.remote_IO, total_IO.pagein_speed, total_IO.pageout_speed, total_IO.pagein_latency, total_IO.pageout_latency,
                (int)ips.size(), total_bd, total_daemon, total_ram.free, total_ram.filter_free, total_ram.allocated_not_mapped, total_ram.mapped);
        cout << str << endl;

        put_data_into_mysql(str);

        char del_str[100];
        sprintf(del_str, "DELETE FROM bd_mapping");
        put_data_into_mysql(del_str);

        sleep(process_interval);
    }
}

// process a client request
static void process_request(const request_msg &msg)
{
    infos.add_info(msg);
    if (msg.bd_on)
    {
        char str1[500];
        sprintf(str1,
                "INSERT INTO block_device (dev_ip, total_IO, remote_IO, pagein_throughput, pageout_throughput, pagein_latency, pageout_latency, high_pagein_latency, low_pagein_latency, high_pageout_latency, low_pageout_latency, time) VALUES ('%s', %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, NOW())",
                msg.ip, msg.IO.total_IO, msg.IO.remote_IO, msg.IO.pagein_speed, msg.IO.pageout_speed, msg.IO.pagein_latency, msg.IO.pageout_latency, msg.IO.high_pagein_latency, msg.IO.low_pagein_latency, msg.IO.high_pageout_latency, msg.IO.low_pageout_latency);
        //cout << str1 << endl;
        put_data_into_mysql(str1);

        for (int i = 0; i < msg.bd_portal_num; i++)
        {
            char str2[500];
            sprintf(str2,
                    "INSERT INTO bd_mapping (dev_ip, remote_ip, pagein_throughput, pageout_throughput, pagein_latency, pageout_latency, time) VALUES ('%s', '%s', %d, %d, %d, %d, NOW())",
                    msg.ip, msg.bd_maps[i].daemon_ip, msg.bd_maps[i].pagein_speed, msg.bd_maps[i].pageout_speed, msg.bd_maps[i].pagein_latency, msg.bd_maps[i].pageout_latency);
            //cout << str2 << endl;
            put_data_into_mysql(str2);
        }
    }
    if (msg.daemon_on)
    {
        char str1[500];
        sprintf(str1,
                "INSERT INTO daemon (dev_ip, RAM_free, RAM_filter_free, RAM_mapped, RAM_allocated, time) VALUES ('%s', %d, %d, %d, %d, NOW())",
                msg.ip, msg.ram.free, msg.ram.filter_free, msg.ram.mapped, msg.ram.allocated_not_mapped);
        //cout << str1 << endl;
        put_data_into_mysql(str1);

        char str2[200];
        sprintf(str2,
                "INSERT INTO daemon_mem (dev_ip, mem_status, time) VALUES ('%s', '%s', NOW())",
                msg.ip, msg.mapping.mem_status);
        //cout << str2 << endl;
        put_data_into_mysql(str2);

        for (int i = 0; i < MAX_FREE_MEM_GB; i++)
        {
            // check if the chunk has been mapped
            if (msg.mapping.mem_status[i] == '2')
            {
                char str3[200];
                sprintf(str3,
                        "INSERT INTO daemon_mapping (dev_ip, remote_ip, local_chunk, remote_chunk, time) VALUES ('%s', '%s', %d, %d, NOW())",
                        msg.ip, msg.mapping.map_infos[i].remote_ip, i + 1, msg.mapping.map_infos[i].remote_chunk_num + 1);
                //cout << str3 << endl;
                put_data_into_mysql(str3);
            }
        }
    }
}

// check whether the request is in the valid form
// currently only do very simple check
static bool check_request(const request_msg &msg)
{
    //check ip
    if (msg.ip[0] == '1' && msg.ip[1] == '9' && msg.ip[2] == '2')
        return true;
    return false;
}

static void deal_request(int msgsock)
{
    //receive message
    request_msg msg;
    recv(msgsock, &msg, sizeof(msg), MSG_WAITALL);
    if (check_request(msg))
    {
        process_request(msg);
    }
    close(msgsock);
}

//listen to the clients' requests
static void server_listen(int sock)
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
            thread deal_request_t(deal_request, msgsock);
            deal_request_t.detach();
        }
    }
}

// build mysql connection
static void connect_to_mysql()
{
    conn = mysql_init(NULL);
    if (conn == NULL)
    {
        printf("mysql_init failed!\n");
        exit(EXIT_FAILURE);
    }

    conn = mysql_real_connect(conn, "127.0.0.1", "root", "mysql", "infiniswap",
                              0, NULL, 0);

    if (conn)
    {
        printf("Mysql connection success!\n");
        FILE *fp = NULL;
    }
    else
    {
        printf("Mysql connection failed!\n");
    }
}

// a separate thread that clears the database every $DELETE_INTERVAL$ seconds
static void clear_db()
{
    while (true)
    {
        for (string table : db_tables)
        {
            char str2[200];
            sprintf(str2,
                    "DELETE FROM %s WHERE time < (NOW() - interval 12 hour)",
                    table.c_str());
            cout << str2 << endl;
            put_data_into_mysql(str2);
        }
        sleep(DELETE_INTERVAL);
    }
}

// send control commands to client
void send_to_worker(control_msg &msg, const char *worker_ip)
{
    int sock;
    struct sockaddr_in server;
    char buf[1024];
    /* Create socket. */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("opening stream socket");
        exit(1);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(worker_ip);
    server.sin_port = htons(clientport);
    if (connect(sock, (struct sockaddr *)&server, sizeof server) == -1)
    {
        cerr << "Error: connect worker socket " << worker_ip << ":" << clientport << endl;
    }
    else
    {
        send(sock, &msg, sizeof(request_msg), 0);
    }

    close(sock);
}

void listen_to_cmds()
{
    cout << "enter control function" << endl;
    const char *socket_path = "/tmp/icp-test";
    struct sockaddr_un addr;
    char buf[100];
    int fd, cl, rc;

    // if (argc > 1)
    //     socket_path = argv[1];

    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    {
        perror("socket error");
        exit(-1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (*socket_path == '\0')
    {
        *addr.sun_path = '\0';
        strncpy(addr.sun_path + 1, socket_path + 1, sizeof(addr.sun_path) - 2);
    }
    else
    {
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        unlink(socket_path);
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("bind error");
        exit(-1);
    }

    if (listen(fd, 5) == -1)
    {
        perror("listen error");
        exit(-1);
    }

    while (1)
    {
        if ((cl = accept(fd, NULL, NULL)) == -1)
        {
            perror("accept error");
            continue;
        }

        while ((rc = read(cl, buf, sizeof(buf))) > 0)
        {
            printf("read %u bytes: %.*s\n", rc, rc, buf);
            string message(buf);
            int first_index = message.find_first_of("/");
            if (first_index == string::npos)
            {
                cerr << "invalid cmd: " << message << endl;
                continue;
            }
            int second_index = message.find_first_of("/", first_index + 1);
            string ip = message.substr(first_index, second_index - 1);
            if (ip.substr(0, 3) != "192")
            {
                cerr << "invalid ip address: " << ip << endl;
            }
            string cmd = message.substr(second_index);
            control_msg msg;
            strcpy(msg.cmd, cmd.c_str());
            send_to_worker(msg, ip.c_str());
        }
        if (rc == -1)
        {
            perror("read");
        }
        else if (rc == 0)
        {
            printf("EOF\n");
        }
        close(cl);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1)
    {
        hostport = atoi(argv[1]);
        if (argc > 2)
        {
            clientport = atoi(argv[2]);
            if (argc > 3)
            {
                strcpy(server_ip, argv[3]);
            }
        }
    }

    connect_to_mysql();
    int sock = server_init();
    thread dataprocessing_t(process_data);
    dataprocessing_t.detach();
    thread dbclear_t(clear_db);
    dbclear_t.detach();
    thread listen_to_cmds_t(listen_to_cmds);
    listen_to_cmds_t.detach();

    server_listen(sock);

    return 0;
}