#include "dashboard.h"

struct bd_info info;
char* write_latency_files[] = {"/tmp/bd_write_latency_1", "/tmp/bd_write_latency_2", "/tmp/bd_write_latency_3"};
char* read_latency_files[] = {"/tmp/bd_read_latency_1", "/tmp/bd_read_latency_2", "/tmp/bd_read_latency_3"};
char* bd_info_files[] = {"/tmp/bd_info_1", "/tmp/bd_info_2", "/tmp/bd_info_3"};
int file_version = 0;

void add_latency(unsigned long long latency, int write)
{
    pr_info("add_latency\n");
    if (write){
        info.write_latency[info.write_num] = latency / 1000;
        info.write_num++;
    }
    else {
        info.read_latency[info.read_num] = latency / 1000;
        info.read_num++;
    }
}

void add_request(void)
{
    info.request_num++;
}

void add_remote_request(void)
{
    //pr_info("add_remote_request\n");
    info.remote_request_num++;
}

void clear_info(void)
{
    pr_info("clear info\n");
    memset(&info, 0, sizeof(info));
    file_version++;
    file_version %= 3;
}

int write_to_file(void)
{
    pr_info("write_to_file\n");
    int i;
    struct file *fp;
    mm_segment_t fs;
    loff_t pos = 0;
    char content[200];
    //char emptyfile[20 * MAX_RW_SIZE]; 
    char version[20];
    memset(content, '\0', sizeof(content));
    //memset(emptyfile, 0, sizeof(emptyfile));
    memset(version, '\0', sizeof(version));

    pr_info("after memset\n");
    sprintf(content, "%u %u %u %u", info.read_num, info.write_num,
            info.request_num, info.remote_request_num);

    pr_info("after sprintf\n");
    fp = filp_open(bd_info_files[file_version], O_RDWR | O_CREAT, 0);
    pr_info("after flipopen\n");
    if (IS_ERR(fp))
    {
        pr_info("Error: open file\n");
        return -1;
    }

    fs = get_fs();
    set_fs(KERNEL_DS);
    vfs_write(fp, content, sizeof(content), &pos);
    
    pr_info("after filp_close\n");
    filp_close(fp, NULL);
    set_fs(fs);

    pr_info("write second file\n");
    pos = 0;
    fp = filp_open(read_latency_files[file_version], O_RDWR | O_CREAT, 0);
    if (IS_ERR(fp))
    {
        pr_info("Error: open file\n");
        return -1;
    }
    fs = get_fs();
    set_fs(KERNEL_DS);
    //vfs_write(fp, emptyfile, sizeof(emptyfile), &pos);
    for (i = 0; i < info.read_num; i++){
        char buffer[20];
        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, "%llu ", info.read_latency[i]);
        vfs_write(fp, buffer, sizeof(buffer), &pos);
        pos += sizeof(buffer);
    }
    
    filp_close(fp, NULL);
    set_fs(fs);

    pr_info("write third file\n");
    pos = 0;
    fp = filp_open(write_latency_files[file_version], O_RDWR | O_CREAT, 0);
    if (IS_ERR(fp))
    {
        pr_info("Error: open file\n");
        return -1;
    }
    fs = get_fs();
    set_fs(KERNEL_DS);
    //vfs_write(fp, emptyfile, sizeof(emptyfile), &pos);
    for (i = 0; i < info.write_num; i++){
        char buffer[20];
        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, "%llu ", info.write_latency[i]);
        vfs_write(fp, buffer, sizeof(buffer), &pos);
        pos += sizeof(buffer);
    }
    
    filp_close(fp, NULL);
    set_fs(fs);

    pos = 0;
    fp = filp_open("/tmp/bd_version", O_RDWR | O_CREAT, 0);
    if (IS_ERR(fp))
    {
        pr_info("Error: open file\n");
        return -1;
    }
    fs = get_fs();
    set_fs(KERNEL_DS);
    sprintf(version, "%d ", file_version);
    vfs_write(fp, version, sizeof(version), &pos);
    filp_close(fp, NULL);
    set_fs(fs);

    return 0;
}

void write_info(void)
{
    clear_info();
    while (1)
    {
        ssleep(1);
        /*
        int i;
        // test latency calculation
        for (i = 1; i <= 100; i++){
            add_latency(i * 1000, 0);
            add_latency(i * 1000, 1);
        }
        */
        
        write_to_file();
        clear_info();
    }
}
