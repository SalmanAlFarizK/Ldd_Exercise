/**********************************************************************************
* File Includes.
**********************************************************************************/
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/string.h>

/**********************************************************************************
* Macro Defintions.
**********************************************************************************/
#define DEVICE_NUMBER                       ("Block_device_no")
#define DEVICE_NAME                         ("Block_device")
#define DEVICE_CLASS                        ("Block_class")
#define REGION_SIZE                         (512)
#define TOTAL_REGIONS                       (8)
#define TOTAL_SIZE                          (REGION_SIZE * TOTAL_REGIONS)

#define VBLOCK_LOCK_REGION                  _IOW('a', 1, int)
#define VBLOCK_UNLOCK_REGION                _IOW('a', 2, int)
#define VBLOCK_READ_REGION                  _IOWR('a', 3, struct region_data)
#define VBLOCK_GET_INFO                     _IOR('a', 4, struct device_info)
#define VBLOCK_ERASE_REGION                 _IOW('a', 5, int)

#define MAX_KEYS                            10

/**********************************************************************************
* Data Structures.
**********************************************************************************/
struct region_data {
    int region_num;
    char data[REGION_SIZE];
};

struct device_info {
    unsigned char lock_bitmap;
    int mirror_enabled;
    int total_regions;
    int region_size;
    int valid_keys[MAX_KEYS];
    int key_count;
};

struct vblock_region {
    char data[REGION_SIZE];
    int locked;
    int lock_key;
    struct mutex region_mutex;
    struct semaphore read_sem;
};

struct vblock_device {
    struct vblock_region regions[TOTAL_REGIONS];
    char *mirror_buffer;
    int mirror_enable;
    int user_keys[MAX_KEYS];
    int key_count;
    dev_t dev_no;
    struct cdev cdev;
    struct class *class;
    struct device *device;
};

/**********************************************************************************
* Function Declarations.
**********************************************************************************/
static int __init block_dev_init(void);
static void __exit block_dev_exit(void);
static int block_dev_open(struct inode* inode, struct file* file);
static int block_dev_release(struct inode* inode, struct file* file);
static ssize_t block_dev_read(struct file* filep, char __user* buffer,
     size_t count, loff_t* lofft);
static ssize_t block_dev_write(struct file* filep, const char __user* buffer,
     size_t count, loff_t* lofft);
static long block_dev_ioctl(struct file* file,
     unsigned int cmd, unsigned long args);
static int parse_write_data(const char *buffer, size_t count, 
                           loff_t *offset, int *key);
static bool is_valid_key(struct vblock_device *dev, int key);

/**********************************************************************************
* Module Parameters.
**********************************************************************************/
static int user_keys[MAX_KEYS];
static int key_count = 0;
module_param_array(user_keys, int, &key_count, 0644);
MODULE_PARM_DESC(user_keys, "Array of valid integer keys for unlocking writes");

static int mirror_enable = 0;
module_param(mirror_enable, int, 0644);
MODULE_PARM_DESC(mirror_enable, "Enable mirroring of writes (0=disabled, 1=enabled)");

/**********************************************************************************
* Global data declaration and Initializations.
**********************************************************************************/
static struct vblock_device *vblock_dev;

struct file_operations block_dev_f_ops = 
{
    .owner          = THIS_MODULE,
    .open           = block_dev_open,
    .release        = block_dev_release,
    .read           = block_dev_read,
    .write          = block_dev_write,
    .unlocked_ioctl = block_dev_ioctl,
};

/**********************************************************************************
* Exported Backup Function.
**********************************************************************************/
int vblock_backup_to_file(const char *path)
{
    struct file *file;
    loff_t pos = 0;
    char *buffer;
    int i, ret = 0;
    
    if (!vblock_dev) {
        pr_err("Device not initialized\n");
        return -ENODEV;
    }
    
    buffer = kmalloc(TOTAL_SIZE, GFP_KERNEL);
    if (!buffer) {
        pr_err("Failed to allocate backup buffer\n");
        return -ENOMEM;
    }
    
    // Copy data from all regions
    for (i = 0; i < TOTAL_REGIONS; i++) {
        if (down_interruptible(&vblock_dev->regions[i].read_sem)) {
            kfree(buffer);
            return -ERESTARTSYS;
        }
        
        memcpy(buffer + (i * REGION_SIZE), 
               vblock_dev->regions[i].data, REGION_SIZE);
        
        up(&vblock_dev->regions[i].read_sem);
    }
    
    // Open file for writing
    file = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(file)) {
        pr_err("Failed to open backup file: %s\n", path);
        kfree(buffer);
        return PTR_ERR(file);
    }
    
    // Write data to file
    ret = kernel_write(file, buffer, TOTAL_SIZE, &pos);
    if (ret < 0) {
        pr_err("Failed to write backup file\n");
    } else {
        pr_info("Backup completed: %s (%d bytes)\n", path, ret);
    }
    
    filp_close(file, NULL);
    kfree(buffer);
    
    return ret;
}
EXPORT_SYMBOL(vblock_backup_to_file);

/**********************************************************************************
* Helper Functions.
**********************************************************************************/
static int parse_write_data(const char *buffer, size_t count, 
                           loff_t *offset, int *key)
{
    char *kernel_buf, *ptr, *token;
    int ret = 0;
    bool key_provided = false;
    
    kernel_buf = kmalloc(count + 1, GFP_KERNEL);
    if (!kernel_buf)
        return -ENOMEM;
    
    // Copy and null-terminate
    memcpy(kernel_buf, buffer, count);
    kernel_buf[count] = '\0';
    
    // Check for key:offset:data format
    ptr = kernel_buf;
    token = strsep(&ptr, ":");
    
    // Try to parse key
    if (kstrtoint(token, 10, key) == 0) {
        key_provided = true;
        
        // Get offset
        token = strsep(&ptr, ":");
        if (!token) {
            kfree(kernel_buf);
            return -EINVAL;
        }
        
        if (kstrtoll(token, 10, offset) != 0) {
            kfree(kernel_buf);
            return -EINVAL;
        }
    } else {
        // No key provided, entire string is offset
        if (kstrtoll(kernel_buf, 10, offset) != 0) {
            kfree(kernel_buf);
            return -EINVAL;
        }
        *key = 0;
    }
    
    kfree(kernel_buf);
    return key_provided ? 1 : 0;
}

static bool is_valid_key(struct vblock_device *dev, int key)
{
    int i;
    
    for (i = 0; i < dev->key_count; i++) {
        if (dev->user_keys[i] == key) {
            return true;
        }
    }
    return false;
}

/**********************************************************************************
* Function Definitions.
**********************************************************************************/
/***********************************************
* Init Function.
***********************************************/
static int __init block_dev_init(void)
{
    int i;
    
    pr_info("Initializing 4KB Block Storage Device\n");
    
    vblock_dev = kzalloc(sizeof(struct vblock_device), GFP_KERNEL);
    if (!vblock_dev) {
        pr_err("Failed to allocate device structure\n");
        return -ENOMEM;
    }
    
    // Initialize regions
    for (i = 0; i < TOTAL_REGIONS; i++) {
        mutex_init(&vblock_dev->regions[i].region_mutex);
        sema_init(&vblock_dev->regions[i].read_sem, 1);
        vblock_dev->regions[i].locked = 0;
        vblock_dev->regions[i].lock_key = 0;
        memset(vblock_dev->regions[i].data, 0, REGION_SIZE);
    }
    
    // Setup mirror buffer if enabled
    vblock_dev->mirror_enable = mirror_enable;
    if (mirror_enable) {
        vblock_dev->mirror_buffer = kzalloc(TOTAL_SIZE, GFP_KERNEL);
        if (!vblock_dev->mirror_buffer) {
            pr_err("Failed to allocate mirror buffer\n");
            kfree(vblock_dev);
            return -ENOMEM;
        }
        pr_info("Mirror mode enabled\n");
    }
    
    // Copy user keys
    vblock_dev->key_count = min(key_count, MAX_KEYS);
    if (vblock_dev->key_count > 0) {
        memcpy(vblock_dev->user_keys, user_keys, 
               vblock_dev->key_count * sizeof(int));
        pr_info("Loaded %d user keys\n", vblock_dev->key_count);
    }
    
    // Allocate device number
    if (alloc_chrdev_region(&vblock_dev->dev_no, 0, 1, DEVICE_NUMBER) < 0) {
        pr_err("Error in device number creation\n");
        goto cleanup_mirror;
    }
    
    pr_info("Major: %d Minor: %d\n", 
            MAJOR(vblock_dev->dev_no), MINOR(vblock_dev->dev_no));
    
    // Initialize character device
    cdev_init(&vblock_dev->cdev, &block_dev_f_ops);
    vblock_dev->cdev.owner = THIS_MODULE;
    
    if (cdev_add(&vblock_dev->cdev, vblock_dev->dev_no, 1) < 0) {
        pr_err("Error in adding cdev\n");
        goto cleanup_chrdev;
    }
    
    // Create device class
    vblock_dev->class = class_create(DEVICE_CLASS);
    if (IS_ERR(vblock_dev->class)) {
        pr_err("Error in class creation\n");
        goto cleanup_cdev;
    }
    
    // Create device node
    vblock_dev->device = device_create(vblock_dev->class, NULL, 
                                      vblock_dev->dev_no, NULL, DEVICE_NAME);
    if (IS_ERR(vblock_dev->device)) {
        pr_err("Error in device creation\n");
        goto cleanup_class;
    }
    
    pr_info("Module Inserted successfully\n");
    pr_info("Total size: %d bytes, %d regions of %d bytes each\n",
            TOTAL_SIZE, TOTAL_REGIONS, REGION_SIZE);
    
    return 0;

cleanup_class:
    class_destroy(vblock_dev->class);
cleanup_cdev:
    cdev_del(&vblock_dev->cdev);
cleanup_chrdev:
    unregister_chrdev_region(vblock_dev->dev_no, 1);
cleanup_mirror:
    if (vblock_dev->mirror_buffer)
        kfree(vblock_dev->mirror_buffer);
    kfree(vblock_dev);
    
    return -1;
}

/***********************************************
* Exit Function.
***********************************************/
static void __exit block_dev_exit(void)
{
    pr_info("Removing 4KB Block Storage Device\n");
    
    if (vblock_dev) {
        device_destroy(vblock_dev->class, vblock_dev->dev_no);
        class_destroy(vblock_dev->class);
        cdev_del(&vblock_dev->cdev);
        unregister_chrdev_region(vblock_dev->dev_no, 1);
        
        if (vblock_dev->mirror_buffer)
            kfree(vblock_dev->mirror_buffer);
        
        kfree(vblock_dev);
    }
    
    pr_info("Module removed successfully\n");
}

/***********************************************
* Open Function.
***********************************************/
static int block_dev_open(struct inode* inode, struct file* file)
{
    file->private_data = vblock_dev;
    pr_debug("Block device opened\n");
    return 0;
}

/***********************************************
* Close Function.
***********************************************/
static int block_dev_release(struct inode* inode, struct file* file)
{
    pr_debug("Block device closed\n");
    return 0;
}

/***********************************************
* Read Function.
***********************************************/
static ssize_t block_dev_read(struct file* filep, char __user* buffer,
     size_t count, loff_t* lofft)
{
    struct vblock_device *dev = filep->private_data;
    int region_num, region_offset;
    size_t to_read, chunk;
    ssize_t ret = 0;
    
    // Validate offset
    if (*lofft >= TOTAL_SIZE) {
        return 0;
    }
    
    // Calculate region and offset
    region_num = *lofft / REGION_SIZE;
    region_offset = *lofft % REGION_SIZE;
    
    // Don't read past device boundary
    to_read = min_t(size_t, count, TOTAL_SIZE - *lofft);
    
    while (to_read > 0) {
        if (region_num >= TOTAL_REGIONS) {
            break;
        }
        
        chunk = min_t(size_t, REGION_SIZE - region_offset, to_read);
        
        // Acquire read semaphore
        if (down_interruptible(&dev->regions[region_num].read_sem)) {
            return -ERESTARTSYS;
        }
        
        // Copy to userspace
        if (copy_to_user(buffer + ret, 
                         dev->regions[region_num].data + region_offset, chunk)) {
            up(&dev->regions[region_num].read_sem);
            return -EFAULT;
        }
        
        up(&dev->regions[region_num].read_sem);
        
        ret += chunk;
        to_read -= chunk;
        region_offset = 0;
        region_num++;
    }
    
    if (ret > 0) {
        *lofft += ret;
    }
    
    return ret;
}

/***********************************************
* Write Function.
***********************************************/
static ssize_t block_dev_write(struct file* filep, const char __user* buffer,
     size_t count, loff_t* lofft)
{
    struct vblock_device *dev = filep->private_data;
    char *kernel_buf, *data_ptr = NULL;
    int region_num, region_offset;
    int key = 0, key_status;
    size_t data_len, chunk;
    ssize_t ret = 0;
    bool key_required = false;
    
    // Allocate kernel buffer
    kernel_buf = kmalloc(count + 1, GFP_KERNEL);
    if (!kernel_buf) {
        return -ENOMEM;
    }
    
    // Copy from userspace
    if (copy_from_user(kernel_buf, buffer, count)) {
        kfree(kernel_buf);
        return -EFAULT;
    }
    kernel_buf[count] = '\0';
    
    // Parse the write data
    key_status = parse_write_data(kernel_buf, count, lofft, &key);
    if (key_status < 0) {
        kfree(kernel_buf);
        return key_status;
    }
    
    // Find data portion (after second colon if key provided)
    if (key_status == 1) {
        // Key provided, find data after second colon
        char *ptr = kernel_buf;
        strsep(&ptr, ":"); // Skip key
        strsep(&ptr, ":"); // Skip offset
        data_ptr = ptr;
    } else {
        // No key, entire buffer is offset, data is empty
        data_ptr = "";
    }
    
    data_len = strlen(data_ptr);
    
    // Validate offset
    if (*lofft < 0 || *lofft >= TOTAL_SIZE) {
        kfree(kernel_buf);
        return -EINVAL;
    }
    
    // Calculate region and offset
    region_num = *lofft / REGION_SIZE;
    region_offset = *lofft % REGION_SIZE;
    
    // Get target region
    if (region_num >= TOTAL_REGIONS) {
        kfree(kernel_buf);
        return -EINVAL;
    }
    
    // Check if region is locked
    if (dev->regions[region_num].locked) {
        key_required = true;
        if (key_status != 1) {
            kfree(kernel_buf);
            return -EACCES;
        }
        
        // Check if key is valid
        if (!is_valid_key(dev, key)) {
            kfree(kernel_buf);
            return -EACCES;
        }
        
        // Check if key matches region's lock key
        if (key != dev->regions[region_num].lock_key) {
            kfree(kernel_buf);
            return -EPERM;
        }
    }
    
    // Acquire region mutex for write
    mutex_lock(&dev->regions[region_num].region_mutex);
    
    // Acquire read semaphore
    if (down_interruptible(&dev->regions[region_num].read_sem)) {
        mutex_unlock(&dev->regions[region_num].region_mutex);
        kfree(kernel_buf);
        return -ERESTARTSYS;
    }
    
    // Write data in chunks
    while (data_len > 0 && region_num < TOTAL_REGIONS) {
        chunk = min_t(size_t, REGION_SIZE - region_offset, data_len);
        
        // Copy data to region
        memcpy(dev->regions[region_num].data + region_offset, 
               data_ptr + ret, chunk);
        
        // Mirror if enabled
        if (dev->mirror_enable && dev->mirror_buffer) {
            loff_t mirror_offset = (region_num * REGION_SIZE) + region_offset;
            memcpy(dev->mirror_buffer + mirror_offset,
                   data_ptr + ret, chunk);
        }
        
        ret += chunk;
        data_len -= chunk;
        region_offset = 0;
        region_num++;
    }
    
    up(&dev->regions[region_num-1].read_sem);
    mutex_unlock(&dev->regions[region_num-1].region_mutex);
    
    kfree(kernel_buf);
    
    if (ret > 0) {
        *lofft += ret;
    }
    
    return ret;
}

/***********************************************
* IOCTL Function.
***********************************************/
static long block_dev_ioctl(struct file* file, unsigned int cmd,
     unsigned long args)
{
    struct vblock_device *dev = file->private_data;
    int region_num;
    struct region_data reg_data;
    struct device_info info;
    int i;
    
    switch(cmd)
    {
        case VBLOCK_LOCK_REGION:
            if (copy_from_user(&region_num, (int __user *)args, sizeof(int))) {
                return -EFAULT;
            }
            
            if (region_num < 0 || region_num >= TOTAL_REGIONS) {
                return -EINVAL;
            }
            
            mutex_lock(&dev->regions[region_num].region_mutex);
            
            if (!dev->regions[region_num].locked) {
                dev->regions[region_num].locked = 1;
                // Use a simple key generation (region number + 1000)
                dev->regions[region_num].lock_key = region_num + 1000;
                pr_debug("Region %d locked with key %d\n", 
                        region_num, dev->regions[region_num].lock_key);
            }
            
            mutex_unlock(&dev->regions[region_num].region_mutex);
            break;
            
        case VBLOCK_UNLOCK_REGION:
            if (copy_from_user(&region_num, (int __user *)args, sizeof(int))) {
                return -EFAULT;
            }
            
            if (region_num < 0 || region_num >= TOTAL_REGIONS) {
                return -EINVAL;
            }
            
            mutex_lock(&dev->regions[region_num].region_mutex);
            
            if (dev->regions[region_num].locked) {
                dev->regions[region_num].locked = 0;
                dev->regions[region_num].lock_key = 0;
                pr_debug("Region %d unlocked\n", region_num);
            }
            
            mutex_unlock(&dev->regions[region_num].region_mutex);
            break;
            
        case VBLOCK_READ_REGION:
            if (copy_from_user(&reg_data, (struct region_data __user *)args,
                              sizeof(struct region_data))) {
                return -EFAULT;
            }
            
            if (reg_data.region_num < 0 || reg_data.region_num >= TOTAL_REGIONS) {
                return -EINVAL;
            }
            
            // Acquire read semaphore
            if (down_interruptible(&dev->regions[reg_data.region_num].read_sem)) {
                return -ERESTARTSYS;
            }
            
            // Copy region data
            memcpy(reg_data.data, dev->regions[reg_data.region_num].data, REGION_SIZE);
            
            up(&dev->regions[reg_data.region_num].read_sem);
            
            if (copy_to_user((struct region_data __user *)args, &reg_data,
                            sizeof(struct region_data))) {
                return -EFAULT;
            }
            break;
            
        case VBLOCK_GET_INFO:
            memset(&info, 0, sizeof(info));
            
            // Build lock bitmap
            info.lock_bitmap = 0;
            for (i = 0; i < TOTAL_REGIONS; i++) {
                if (dev->regions[i].locked) {
                    info.lock_bitmap |= (1 << i);
                }
            }
            
            info.mirror_enabled = dev->mirror_enable;
            info.total_regions = TOTAL_REGIONS;
            info.region_size = REGION_SIZE;
            info.key_count = dev->key_count;
            memcpy(info.valid_keys, dev->user_keys, 
                   min(dev->key_count, MAX_KEYS) * sizeof(int));
            
            if (copy_to_user((struct device_info __user *)args, &info,
                            sizeof(struct device_info))) {
                return -EFAULT;
            }
            break;
            
        case VBLOCK_ERASE_REGION:
            if (copy_from_user(&region_num, (int __user *)args, sizeof(int))) {
                return -EFAULT;
            }
            
            if (region_num < 0 || region_num >= TOTAL_REGIONS) {
                return -EINVAL;
            }
            
            mutex_lock(&dev->regions[region_num].region_mutex);
            
            // Check if region is locked
            if (dev->regions[region_num].locked) {
                mutex_unlock(&dev->regions[region_num].region_mutex);
                return -EACCES;
            }
            
            if (down_interruptible(&dev->regions[region_num].read_sem)) {
                mutex_unlock(&dev->regions[region_num].region_mutex);
                return -ERESTARTSYS;
            }
            
            // Erase region
            memset(dev->regions[region_num].data, 0, REGION_SIZE);
            
            // Mirror erase if enabled
            if (dev->mirror_enable && dev->mirror_buffer) {
                memset(dev->mirror_buffer + (region_num * REGION_SIZE),
                       0, REGION_SIZE);
            }
            
            up(&dev->regions[region_num].read_sem);
            mutex_unlock(&dev->regions[region_num].region_mutex);
            
            pr_debug("Region %d erased\n", region_num);
            break;
            
        default:
            pr_debug("Unknown ioctl command: %u\n", cmd);
            return -ENOTTY;
    }
    
    return 0;
}

/**********************************************************************************
* Module Init Exit registration.
**********************************************************************************/
module_init(block_dev_init);
module_exit(block_dev_exit);

/**********************************************************************************
* Module Meta Information.
**********************************************************************************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Salman Al Fariz K");
MODULE_DESCRIPTION("Device driver simulating 4KB sector storage");