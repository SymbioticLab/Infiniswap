#include "dashboard.h"

struct bd_info info;

void add_latency(unsigned long long latency, int write)
{
    int container_size = MAX_RW_SIZE >> EXCEPTION_RATIO;
    if (write)
    {
        if (info.write_num >= MAX_RW_SIZE - 1)
        {
            pr_info("Error: write number exceed limit\n");
            return;
        }
        int i, j;
        for (i = 0; i < container_size; i++)
        {
            if (latency > info.high_write_latency[i])
            {
                for (j = container_size - 1; j > i; j--)
                {
                    info.high_write_latency[j] = info.high_write_latency[j - 1];
                }
                info.high_write_latency[i] = latency;
                break;
            }
        }
        for (i = 0; i < container_size; i++)
        {
            if (latency < info.low_write_latency[i])
            {
                for (j = container_size - 1; j > i; j--)
                {
                    info.low_write_latency[j] = info.low_write_latency[j - 1];
                }
                info.low_write_latency[i] = latency;
                break;
            }
        }
        info.write_num++;
        info.avg_write_latency = ((info.avg_write_latency * (info.write_num - 1)) + latency) / info.write_num;
    }
    else
    {
        if (info.read_num >= MAX_RW_SIZE - 1)
        {
            pr_info("Error: read number exceed limit\n");
            return;
        }
        int i, j;
        for (i = 0; i < container_size; i++)
        {
            if (latency > info.high_read_latency[i])
            {
                for (j = container_size - 1; j > i; j--)
                {
                    info.high_read_latency[j] = info.high_read_latency[j - 1];
                }
                info.high_read_latency[i] = latency;
                break;
            }
        }
        for (i = 0; i < container_size; i++)
        {
            if (latency < info.low_read_latency[i] || info.low_read_latency[i] == 0)
            {
                for (j = container_size - 1; j > i; j--)
                {
                    info.low_read_latency[j] = info.low_read_latency[j - 1];
                }
                info.low_read_latency[i] = latency;
                break;
            }
        }
        info.read_num++;
        info.avg_read_latency = ((info.avg_read_latency * (info.read_num - 1)) + latency) / info.read_num;
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
    //pr_info("clear info\n");
    memset(&info, 0, sizeof(info));
}

int write_to_file(void)
{
    int read_ex_size = info.read_num >> EXCEPTION_RATIO;
    int write_ex_size = info.write_num >> EXCEPTION_RATIO;
    pr_info("read_ex_size: %d\n", read_ex_size);
    struct file *fp;
    mm_segment_t fs;
    loff_t pos = 0;
    char content[200];
    //pr_info("write content: %u %u %u %u %llu %llu", info.read_num, info.write_num,
    //   info.request_num, info.remote_request_num, info.avg_read_latency, info.avg_write_latency);
    int i;
    int exception_read_tot = 0, exception_write_tot = 0;
    for (i = 0; i < read_ex_size; i++)
    {
        exception_read_tot += (info.high_read_latency[i] + info.low_read_latency[i]);
    }
    for (i = 0; i < write_ex_size; i++)
    {
        exception_write_tot += (info.high_write_latency[i] + info.low_write_latency[i]);
    }
    info.high_ex_read_latency = info.high_read_latency[read_ex_size];
    info.low_ex_read_latency = info.low_read_latency[read_ex_size];
    info.high_ex_write_latency = info.high_write_latency[write_ex_size];
    info.low_ex_write_latency = info.low_write_latency[write_ex_size];
    // calculate the filtered average (without the very high and very low part)
    if (info.read_num){
        info.avg_read_latency = (info.avg_read_latency * info.read_num - exception_read_tot) / (info.read_num - 2 * read_ex_size);
    }
    if (info.write_num){
        info.avg_write_latency = (info.avg_write_latency * info.write_num - exception_write_tot) / (info.write_num - 2 * write_ex_size);
    }
    
    sprintf(content, "%u %u %u %u %llu %llu %llu %llu %llu %llu end", info.read_num, info.write_num,
            info.request_num, info.remote_request_num, info.avg_read_latency, info.avg_write_latency,
            info.high_ex_read_latency, info.low_ex_read_latency, info.high_ex_write_latency, info.low_ex_write_latency);
    pr_info("%u %u %u %u %llu %llu %llu %llu %llu %llu end\n", info.read_num, info.write_num,
            info.request_num, info.remote_request_num, info.avg_read_latency, info.avg_write_latency,
            info.high_ex_read_latency, info.low_ex_read_latency, info.high_ex_write_latency, info.low_ex_write_latency);

    fp = filp_open("/tmp/bd_info", O_RDWR | O_CREAT, 0);
    if (IS_ERR(fp))
    {
        pr_info("Error: open file\n");
        return -1;
    }

    fs = get_fs();
    set_fs(KERNEL_DS);
    vfs_write(fp, content, strlen(content), &pos);
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
        write_to_file();
        clear_info();
    }
}
