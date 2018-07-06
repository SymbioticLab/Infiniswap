#include "dashboard.h"

struct bd_info info;

void add_latency(unsigned long long latency, int write)
{
    int container_size = EXCEPTION_RATIO * MAX_RW_SIZE;
    if (write)
    {
        if (info.write_num >= MAXSIZE - 1)
        {
            pr_error("Error: write number exceed limit");
            return;
        }
        int i;
        for (i = 0; i < container_size; i++)
        {
            if (latency > info.high_write_latency[i])
            {
                int j = container_size - 1;
                for (j; j > i; j--)
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
                int j = container_size - 1;
                for (j; j > i; j--)
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
        if (info.read_num >= MAXSIZE - 1)
        {
            return;
            pr_error("Error: write number exceed limit");
        }
        int i;
        for (i = 0; i < container_size; i++)
        {
            if (latency > info.high_read_latency[i])
            {
                int j = container_size - 1;
                for (j; j > i; j--)
                {
                    info.high_read_latency[j] = info.high_read_latency[j - 1];
                }
                info.high_read_latency[i] = latency;
                break;
            }
        }
        for (i = 0; i < container_size; i++)
        {
            if (latency < info.low_read_latency[i])
            {
                int j = container_size - 1;
                for (j; j > i; j--)
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
    int container_size = EXCEPTION_RATIO * MAX_RW_SIZE;
    struct file *fp;
    mm_segment_t fs;
    loff_t pos = 0;
    char content[100];
    //pr_info("write content: %u %u %u %u %llu %llu", info.read_num, info.write_num,
    //   info.request_num, info.remote_request_num, info.avg_read_latency, info.avg_write_latency);
    int i;
    int exception_read_tot = 0, exception_write_tot = 0;
    for (i = 0; i < info.read_num * EXCEPTION_RATIO; i++)
    {
        exception_read_tot += (info.high_read_latency[i] + info.low_read_latency[i])
            exception_write_tot += (info.high_write_latency[i] + info.low_write_latency[i]);
    }
    info.high_ex_read_latency = info.high_read_latency[(int)(info.read_num * EXCEPTION_RATIO)];
    info.low_ex_read_latency = info.low_read_latency[(int)(info.read_num * EXCEPTION_RATIO)];
    info.high_ex_write_latency = info.high_write_latency[(int)(info.write_num * EXCEPTION_RATIO)];
    info.low_ex_write_latency = info.low_write_latency[(int)(info.write_num * EXCEPTION_RATIO)];
    // calculate the filtered average (without the very high and very low part)
    info.avg_read_latency = (info.avg_read_latency * info.read_num - exception_read_tot) / (info.read_num * (1 - EXCEPTION_RATIO * 2));
    info.avg_write_latency = (info.avg_write_latency * info.write_num - exception_write_tot) / (info.write_num * (1 - EXCEPTION_RATIO * 2));
    sprintf(content, "%u %u %u %u %llu %llu %llu %llu %llu %llu end", info.read_num, info.write_num,
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
