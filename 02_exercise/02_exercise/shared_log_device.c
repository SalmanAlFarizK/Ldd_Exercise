#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>

#define DEVICE_NAME                 ("Logger_Device")
#define DEVICE_CLASS                ("Logger_Class")

#define FETCH_KERNEL_SIZE           _IOR('a', 1, int*)
#define CLEAR_KERNEL_BUFFER         _IOW('a', 2, int*)


/**
* Function Declarations.
*/
static int __init logger_device_init(void);
static void __exit logger_device_exit(void);
static int logger_device_open(struct inode* inode, struct file* file);
static int logger_device_release(struct inode* inode, struct file* file);
static ssize_t logger_device_write(struct file* filp, const char __user* buffer, size_t count, loff_t* lofft);
static ssize_t logger_device_read(struct file* filp, char __user* buffer, size_t count, loff_t* lofft);
static long logger_device_ioctl(struct file* file, unsigned int cmd, unsigned long args);


/**
* Global Variable Initialization.
*/
dev_t logger_device_no;
struct cdev logger_cdev;
struct class * logger_class;
struct mutex logger_device_mutex;
unsigned int kernel_buff_size = 0;
char* kernel_buff = NULL;
int kernel_index = 0;

struct file_operations f_ops = 
{
    .owner          = THIS_MODULE,
    .write          = logger_device_write,
    .read           = logger_device_read,
    .release        = logger_device_release,
    .open           = logger_device_open,
    .unlocked_ioctl = logger_device_ioctl 
};

module_param(kernel_buff_size, int, S_IRUSR|S_IWUSR);

/**
* Function Definitions.
*/

static int __init logger_device_init(void)
{
    if(alloc_chrdev_region(&logger_device_no, 0, 1, DEVICE_NAME) < 0)
    {
        pr_info("Error in creating device number\n");

        return -1;
    }

    pr_info("Major Number: %d Minor Number: %d\n", MAJOR(logger_device_no),MINOR(logger_device_no));

    cdev_init(&logger_cdev, &f_ops);

    if(cdev_add(&logger_cdev, logger_device_no, 1) < 0)
    {
        pr_info("Error in Adding Cdev\n");

        goto r_cdev;
    }

    if(IS_ERR(logger_class = class_create(DEVICE_CLASS)))
    {
        pr_info("Error in creating class\n");

        goto r_class;
    }

    if(IS_ERR(device_create(logger_class, NULL, logger_device_no, NULL, DEVICE_NAME)))
    {
        pr_info("Error in creating device\n");

        goto r_device;
    }

    if(kernel_buff_size > 0)
    {
        kernel_buff = (char*)kmalloc(sizeof(char)* kernel_buff_size, GFP_KERNEL);

        if(NULL == kernel_buff)
        {
            pr_info("Error in allocating memory\n");

            goto r_device;
        }
    }
    else
    {
        pr_info("Kernel buffer size is 0\n");

        goto r_device;
    }

    pr_info("Logger Module inserted successfully\n");

    return 0;

r_device:
    class_destroy(logger_class);

r_class:
    cdev_del(&logger_cdev);

r_cdev:
    unregister_chrdev_region(logger_device_no, 1);

    return -1;
}

static void __exit logger_device_exit(void)
{
    pr_info("Entered Exit Function\n");

    kfree(kernel_buff);

    device_destroy(logger_class, logger_device_no);

    class_destroy(logger_class);

    cdev_del(&logger_cdev);

    unregister_chrdev_region(logger_device_no, 1);

    return;
}

static int logger_device_open(struct inode* inode, struct file* file)
{
    pr_info("Logger Device Opened\n");

    return 0;
}

static int logger_device_release(struct inode* inode, struct file* file)
{
    pr_info("Logger Device Closed\n");

    return 0;
}

static ssize_t logger_device_write(struct file* filp, const char __user* buffer,
     size_t count, loff_t* lofft)
{
    pr_info("Write Function Called\n");

    /* Lock Mutex. */
    mutex_lock(&logger_device_mutex);

    if(NULL == kernel_buff || kernel_buff_size <= 0)
    {
        pr_info("No memory space in kernel buffer\n");

        return -ENOMEM;
    }

    if(kernel_index >= kernel_buff_size)
    {
        pr_info("Kernel buffer is Full\n");

        return -ENOMEM;
    }

    if(count + kernel_index > kernel_buff_size)
    {
        count = kernel_buff_size - kernel_index;
    }

    if(copy_from_user(kernel_buff + kernel_index, buffer, count))
    {
        pr_info("Error in writing data to kernel buffer\n");

        return -EFAULT;
    }

    kernel_index += count;

    pr_info("Successfully Written to kernel buffer\n");

    pr_info("Kernel Buffer is %s\n",kernel_buff);

    /* Unlock Mutex. */
    mutex_unlock(&logger_device_mutex);

    return count;
}

static ssize_t logger_device_read(struct file* filp, char __user* buffer,
     size_t count, loff_t* lofft)
{
    pr_info("Read function called\n");

    /* Lock Mutex. */
    mutex_lock(&logger_device_mutex);

    if(NULL == kernel_buff || kernel_buff_size <= 0)
    {
        pr_info("Empty kernel Buffer\n");

        return -ENOMEM;
    }

    if(count > kernel_index)
    {
        count = kernel_index;
    }

    if(copy_to_user(buffer, kernel_buff, count))
    {
        pr_info("Error in copying to user space\n");

        return -EFAULT;
    }

    /* Unlock mutex. */
    mutex_unlock(&logger_device_mutex);

    pr_info("Successfully read kernel buffer\n");

    return count;
}

static long logger_device_ioctl(struct file* file,
    unsigned int cmd, unsigned long args)
{
    switch(cmd)
    {
        case FETCH_KERNEL_SIZE:
            if(copy_to_user((unsigned int*)args, &kernel_buff_size, sizeof(kernel_buff_size)))
            {
                pr_info("Error in copying size from ioctl %d\n", kernel_buff_size);

                return -EFAULT;
            }

            break;
        
        case CLEAR_KERNEL_BUFFER:
            /* Lock Mutex. */
            mutex_lock(&logger_device_mutex);
            
            kfree(kernel_buff);
            kernel_buff = NULL;
            kernel_buff_size = 0;
            kernel_index = 0;

            /* Unlock Mutex. */
            mutex_unlock(&logger_device_mutex);

            break;

        default:
            pr_info("Default\n");

            break;
    }

    return 0;
}

module_init(logger_device_init);
module_exit(logger_device_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple data logger device");
MODULE_AUTHOR("Salman Al Fariz K");