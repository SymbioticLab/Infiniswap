#include "dashboard.h"

struct bd_info info;

void add_latency(unsigned long long latency, int write){
    if (write){
        info.write_num++;
        info.avg_write_latency = ((info.avg_write_latency * (info.write_num - 1)) + latency) / info.write_num;
    }
    else {
        info.read_num++;
        info.avg_read_latency = ((info.avg_read_latency * (info.read_num - 1)) + latency) / info.read_num;
    }
}

void add_request(void){
    info.request_num++;
}

void add_remote_request(void){
    //pr_info("add_remote_request\n");
    info.remote_request_num++;
}

void clear_info(void){
    //pr_info("clear info\n");
    info.read_num = 0;
    info.write_num = 0;
    info.request_num = 0;
    info.remote_request_num = 0;
    info.avg_read_latency = 0;
    info.avg_write_latency = 0;
}

int write_to_file(void){
    struct file * fp;
    mm_segment_t fs;
    loff_t pos = 0;
    char content[100];
    sprintf(content, "%u %u %u %u %llu %llu\n", info.read_num, info.write_num,
         info.request_num, info.remote_request_num, info.avg_read_latency, info.avg_write_latency);

    fp = filp_open("/tmp/bd_info", O_RDWR | O_CREAT, 0);
    if (IS_ERR(fp)){
        pr_info("Error: open file\n");
        return -1;
    }

    fs = get_fs();
    set_fs(KERNEL_DS);
    vfs_write(fp, content, sizeof(content), &pos);
    filp_close(fp, NULL);
    set_fs(fs);
    return 0;
}

void write_info(void){
    clear_info();
    while(1){
        ssleep(1);
        write_to_file();
        clear_info();
    }
}
