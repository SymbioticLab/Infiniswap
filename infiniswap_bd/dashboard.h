#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/string.h>

#define EXCEPTION_RATIO 5
#define MAX_RW_SIZE 150000

struct connection_latency
{
    unsigned latency;
    u8 cb_index;
};

struct bd_info
{
    struct connection_latency read_latency[MAX_RW_SIZE];
    struct connection_latency write_latency[MAX_RW_SIZE];
    unsigned read_num;
    unsigned write_num;
    unsigned request_num;
    unsigned remote_request_num;
};

void add_request(void);

void add_remote_request(void);

void add_latency(unsigned long long latency, u8 cb_index, int write);

void clear_info(void);

int write_to_file(void);

void write_info(void);

#endif