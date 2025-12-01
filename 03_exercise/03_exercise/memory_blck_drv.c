#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#define DEVICE_NAME             ("Blck_Device_Drv")
#define DEVICE_CLASS            ("Blck_Device_Class")
#define KERNEL_BUFFER_SIZE      (1024)

/*********************************************************
* Function Declarations.
*********************************************************/
static int __init blck_drv_init(void);
static void __exit blck_drv_exit(void);
static ssize_t blck_drv_write(struct file* filep, const char __user* buffer,
                             size_t count, loff_t* lofft);
static ssize_t blck_drv_read(struct file* filep, char __user* buffer,
                             size_t count, loff_t* lofft);
static int blck_drv_open(struct inode* inode, struct file* file);
static int blck_drv_close(struct inode* inode, struct file* file);


/*********************************************************
* Global Variable Initialization.
*********************************************************/
dev_t blck_drv_dev_no;
struct cdev blck_drv_cdev;
struct class* blck_drv_class;

struct file_operations f_ops = 
{
    .owner      = THIS_MODULE,
    .open       = blck_drv_open,
    .release    = blck_drv_close,
    .write      = blck_drv_write,
    .read       = blck_drv_read
};

char kernel_buffer[KERNEL_BUFFER_SIZE];
int kernel_buffer_idx = 0;

/*********************************************************
* Function Definitions.
*********************************************************/
static int __init blck_drv_init(void)
{
    pr_info("Entered the init function\n");

    if(alloc_chrdev_region(&blck_drv_dev_no, 0, 1, DEVICE_NAME) < 0)
    {
        pr_info("Error in Creating the device number\n");

        return -1;
    }

    pr_info("Major Number: %d Minor Number: %d\n",MAJOR(blck_drv_dev_no), MINOR(blck_drv_dev_no));

    cdev_init(&blck_drv_cdev, &f_ops);

    if(cdev_add(&blck_drv_cdev, blck_drv_dev_no, 1) < 0)
    {
        pr_info("Error in adding cdev\n");

        goto r_cdev;
    }

    if(IS_ERR(blck_drv_class = class_create(DEVICE_CLASS)))
    {
        pr_info("Error in creating class\n");

        goto r_class;
    }

    if(IS_ERR(device_create(blck_drv_class, NULL, blck_drv_dev_no,NULL, DEVICE_NAME)))
    {
        pr_info("Error in creating device\n");

        goto r_device;
    }

    pr_info("Successfully inserted the module\n");

    return 0;

r_device:
    class_destroy(blck_drv_class);

r_class:
    cdev_del(&blck_drv_cdev);

r_cdev:
    unregister_chrdev_region(blck_drv_dev_no, 1);

    return -1;
}

static void __exit blck_drv_exit(void)
{
    pr_info("Entered Exit function\n");

    device_destroy(blck_drv_class, blck_drv_dev_no);

    class_destroy(blck_drv_class);

    cdev_del(&blck_drv_cdev);

    unregister_chrdev_region(blck_drv_dev_no, 1);
}

static ssize_t blck_drv_write(struct file* filep, const char __user* buffer,
                            size_t count, loff_t* lofft)
{
    pr_info("Write Function called\n");

    if(kernel_buffer_idx >= KERNEL_BUFFER_SIZE)
    {
        pr_info("Buffer is full\n");

        return -ENOMEM;
    }

    if(count + kernel_buffer_idx > KERNEL_BUFFER_SIZE)
    {
        count = KERNEL_BUFFER_SIZE - kernel_buffer_idx;
    }

    if(copy_from_user(kernel_buffer + kernel_buffer_idx, buffer, count))
    {
        pr_info("Error in copying data from user\n");

        return -EFAULT;
    }

    kernel_buffer_idx += count;

    pr_info("Successfully written to kernel buffe\n");

    pr_info("kernel buffer is %s\n",kernel_buffer);

    return count;
}

static ssize_t blck_drv_read(struct file* filep, char __user* buffer,
                             size_t count, loff_t* lofft)
{
    pr_info("Read Function called\n");

    if(kernel_buffer_idx == 0)
    {
        pr_info("Empty Kernel Buffer\n");

        return -ENOMEM;
    }

    if(count > kernel_buffer_idx)
    {
        count = kernel_buffer_idx;
    }

    if(copy_to_user(buffer, kernel_buffer, count))
    {
        pr_info("Error in copying data to user\n");

        return -EFAULT;
    }
    

    return count;
}

static int blck_drv_open(struct inode* inode, struct file* file)
{
    pr_info("Device Opened\n");

    return 0;
}

static int blck_drv_close(struct inode* inode, struct file* file)
{
    pr_info("Device closed\n");

    return 0;
}

/*********************************************************
* Module Exit and Init Registration.
*********************************************************/
module_init(blck_drv_init);
module_exit(blck_drv_exit);

/*********************************************************
* Module Meta Informations.
*********************************************************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Salman Al Fariz K");
MODULE_DESCRIPTION("A simple Memory block driver");

