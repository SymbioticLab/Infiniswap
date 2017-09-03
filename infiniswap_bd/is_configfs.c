/*
 * Infiniswap, remote memory paging over RDMA
 * Copyright 2017 University of Michigan, Ann Arbor
 *  
 * Copyright (c) 2013 Mellanox Technologies��. All rights reserved.
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the Mellanox Technologies�� BSD license
 * below:
 *
 *      - Redistribution and use in source and binary forms, with or without
 *        modification, are permitted provided that the following conditions
 *        are met:
 *
 *      - Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 *      - Neither the name of the Mellanox Technologies�� nor the names of its
 *        contributors may be used to endorse or promote products derived from
 *        this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "infiniswap.h"


#define cgroup_to_IS_session(x) container_of(x, struct IS_session, session_cg)
#define cgroup_to_IS_device(x) container_of(x, struct IS_file, dev_cg)

// bind in IS_device_item_ops
static ssize_t device_attr_store(struct config_item *item,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#else
			         struct configfs_attribute *attr,
#endif
			         const char *page, size_t count)
{
	struct IS_session *IS_session;
	struct IS_file *IS_device;
	char xdev_name[MAX_IS_DEV_NAME];
	ssize_t ret;

	pr_info("%s\n", __func__);
	IS_session = cgroup_to_IS_session(to_config_group(item->ci_parent));
	IS_device = cgroup_to_IS_device(to_config_group(item));

	sscanf(page, "%s", xdev_name);
	if(IS_file_find(IS_session, xdev_name)) {
		pr_err("Device already exists: %s", xdev_name);
		return -EEXIST;
	}
	// device is IS_file
	ret = IS_create_device(IS_session, xdev_name, IS_device); 
	if (ret) {
		pr_err("failed to create device %s\n", xdev_name);
		return ret;
	}

	return count;
}

// bind in IS_device_item_ops
static ssize_t state_attr_show(struct config_item *item,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#else
			       struct configfs_attribute *attr,
#endif
			       char *page)
{
	struct IS_file *IS_device;
	ssize_t ret;

	pr_info("%s\n", __func__);
	IS_device = cgroup_to_IS_device(to_config_group(item));

	ret = snprintf(page, PAGE_SIZE, "%s\n", IS_device_state_str(IS_device));

	return ret;
}

// bind in IS_device_type
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#else
static struct configfs_item_operations IS_device_item_ops = {
		.store_attribute = device_attr_store,
		.show_attribute = state_attr_show,
};
#endif

// bind in IS_device_item_attrs
static struct configfs_attribute device_item_attr = {
		.ca_owner       = THIS_MODULE,
		.ca_name        = "device",
		.ca_mode        = S_IWUGO,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.show		= state_attr_show,
		.store		= device_attr_store,
#endif
};
// bind in IS_device_item_attrs
static struct configfs_attribute state_item_attr = {
		.ca_owner       = THIS_MODULE,
		.ca_name        = "state",
		.ca_mode        = S_IRUGO,

};

// bind in IS_device_type
static struct configfs_attribute *IS_device_item_attrs[] = {
		&device_item_attr,
		&state_item_attr,
		NULL,
};

// defined in IS_device_make_group
static struct config_item_type IS_device_type = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#else
		.ct_item_ops    = &IS_device_item_ops,
#endif
		.ct_attrs       = IS_device_item_attrs,
		.ct_owner       = THIS_MODULE,
};

// bind in IS_session_devices_group_ops
static struct config_group *IS_device_make_group(struct config_group *group,
		const char *name)
{
	struct IS_session *IS_session;
	struct IS_file *IS_file;

	pr_info("%s, name=%s\n", __func__, name);
	IS_file = kzalloc(sizeof(*IS_file), GFP_KERNEL); // allocate space for device
	if (!IS_file) {
		pr_err("IS_file alloc failed\n");
		return NULL;
	}

	spin_lock_init(&IS_file->state_lock);
	if (IS_set_device_state(IS_file, DEVICE_OPENNING)) {  // change state
		pr_err("device %s: Illegal state transition %s -> openning\n",
		       IS_file->dev_name,
		       IS_device_state_str(IS_file));
		goto err;
	}

	sscanf(name, "%s", IS_file->dev_name);
	IS_session = cgroup_to_IS_session(group);
	spin_lock(&IS_session->devs_lock);
	list_add(&IS_file->list, &IS_session->devs_list);
	spin_unlock(&IS_session->devs_lock);

	config_group_init_type_name(&IS_file->dev_cg, name, &IS_device_type);// define & bind IS_device_type

	return &IS_file->dev_cg;
err:
	kfree(IS_file);
	return NULL;
}

// bind in IS_session_devices_group_ops
static void IS_device_drop(struct config_group *group, struct config_item *item)
{
	struct IS_file *IS_device;
	struct IS_session *IS_session;

	pr_info("%s\n", __func__);
	IS_session = cgroup_to_IS_session(group);
	IS_device = cgroup_to_IS_device(to_config_group(item));
	IS_destroy_device(IS_session, IS_device);
	kfree(IS_device);
}

// bind in IS_session_item_ops
static ssize_t portal_attr_store(struct config_item *citem,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#else
		struct configfs_attribute *attr,
#endif
		const char *buf,size_t count)
{
	char rdma[MAX_PORTAL_NAME] = "rdma://" ;
	struct IS_session *IS_session;

	pr_info("%s, buf=%s\n", __func__, buf);
	sscanf(strcat(rdma, buf), "%s", rdma);
	if(IS_session_find_by_portal(&g_IS_sessions, rdma)) {
		pr_err("Portal already exists: %s", buf);
		return -EEXIST;
	}

	IS_session = cgroup_to_IS_session(to_config_group(citem));
	// session is created here
	if (IS_session_create(rdma, IS_session)) {
		printk("Couldn't create new session with %s\n", rdma);
		return -EINVAL;
	}

	return count;
}

// bind in IS_session_type
static struct configfs_group_operations IS_session_devices_group_ops = {
		.make_group     = IS_device_make_group,
		.drop_item      = IS_device_drop,
};

// bind in IS_session_type
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#else
static struct configfs_item_operations IS_session_item_ops = {
		.store_attribute = portal_attr_store,
};
#endif

// bind in IS_session_item_attrs
static struct configfs_attribute portal_item_attr = {
		.ca_owner       = THIS_MODULE,
		.ca_name        = "portal",
		.ca_mode        = S_IWUGO,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.store		= portal_attr_store,
#endif		
};
// bind in IS_session_type
static struct configfs_attribute *IS_session_item_attrs[] = {
		&portal_item_attr,
		NULL,
};

// bind in IS_session_make_group()
static struct config_item_type IS_session_type = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#else
		.ct_item_ops    = &IS_session_item_ops,
#endif
		.ct_attrs       = IS_session_item_attrs,
		.ct_group_ops   = &IS_session_devices_group_ops,
		.ct_owner       = THIS_MODULE,
};

// defined in IS_group_ops
static struct config_group *IS_session_make_group(struct config_group *group,
		const char *name)
{
	struct IS_session *IS_session;

	pr_info("%s, name=%s\n", __func__, name);
	IS_session = kzalloc(sizeof(*IS_session), GFP_KERNEL); // allocate the space for IS_session
	if (!IS_session) {
		pr_err("failed to allocate IS session\n");
		return NULL;
	}

	INIT_LIST_HEAD(&IS_session->devs_list);
	spin_lock_init(&IS_session->devs_lock);
	mutex_lock(&g_lock);
	list_add(&IS_session->list, &g_IS_sessions);
	created_portals++;
	mutex_unlock(&g_lock);

	config_group_init_type_name(&IS_session->session_cg, name, &IS_session_type); // bind session with IS_session_type (item_type)

	return &IS_session->session_cg;

}

// defined in IS_group_ops
static void IS_session_drop(struct config_group *group, struct config_item *item)
{
	struct IS_session *IS_session;

	pr_info("%s\n", __func__);
	IS_session = cgroup_to_IS_session(to_config_group(item));
	IS_session_destroy(IS_session);  // call IS_session_destroy
	kfree(IS_session);
}

static struct configfs_group_operations IS_group_ops = {
		.make_group     = IS_session_make_group,
		.drop_item      = IS_session_drop,
};

static struct config_item_type IS_item = {
		.ct_group_ops   = &IS_group_ops,
		.ct_owner       = THIS_MODULE,
};

static struct configfs_subsystem IS_subsys = {
		.su_group = {
				.cg_item = {
						.ci_namebuf = "infiniswap",
						.ci_type = &IS_item,
				},
		},

};

int IS_create_configfs_files(void)
{
	int err = 0;

	pr_info("%s\n", __func__);
	config_group_init(&IS_subsys.su_group);
	mutex_init(&IS_subsys.su_mutex);

	err = configfs_register_subsystem(&IS_subsys);
	if (err) {
		pr_err("Error %d while registering subsystem %s\n",
				err, IS_subsys.su_group.cg_item.ci_namebuf);
	}

	return err;
}

void IS_destroy_configfs_files(void)
{
	configfs_unregister_subsystem(&IS_subsys);
}
