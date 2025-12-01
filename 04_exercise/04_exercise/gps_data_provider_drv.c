#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/mutex.h>

#define DEVICE_NAME             ("GPS_Device")
#define DEVICE_CLASS            ("GPS_Class")

/*****************************************
*   Function Declarations.
*****************************************/
static int __init gps_drv_init(void);
static void __exit gps_drv_exit(void);
static int gps_dev_open(struct inode* inode, struct file* file);
static int gps_dev_close(struct inode* inode, struct file* file);
static ssize_t gps_dev_write(struct file* filep, const char __user* buffer, size_t count, loff_t* lofft);
static ssize_t gps_dev_read(struct file* filep, char __user* buffer, size_t count, loff_t* lofft);

/*****************************************
*   Global variable Declarations.
*****************************************/
dev_t gps_dev_no;
struct cdev gps_dev_cdev;
struct class* gps_dev_class;
struct file_operations gps_dev_f_ops = 
{
    .owner      = THIS_MODULE,
    .read       = gps_dev_read,
    .write      = gps_dev_write,
    .open       = gps_dev_open,
    .release    = gps_dev_close
};

/*****************************************
*   Function Definitions.
*****************************************/
static int __init gps_drv_init(void)
{
    pr_info("Entered init function\n");

    if(alloc_chrdev_region(&gps_dev_no, 0, 1, DEVICE_NAME) < 0)
    {
        pr_info("Error in Device number creation\n");

        return -1;
    }

    cdev_init(&gps_dev_cdev, &gps_dev_f_ops);

    if(cdev_add(&gps_dev_cdev, gps_dev_no, 1) < 0)
    {
        pr_info("Error in adding cdev\n");

        goto r_cdev;
    }

    if(IS_ERR(gps_dev_class = class_create(DEVICE_CLASS)))
    {
        pr_info("Error in class creation\n");

        goto r_class;
    }

    if(IS_ERR(device_create(gps_dev_class, NULL, gps_dev_no, NULL, DEVICE_NAME)))
    {
        pr_info("Error in device creation\n");

        goto r_device;
    }

    pr_info("GPS module inserted successfully\n");

    return 0;

r_device:
    class_destroy(gps_dev_class);

r_class:
    cdev_del(&gps_dev_cdev);

r_cdev:
    unregister_chrdev_region(gps_dev_no, 1);

    return -1;
}

static void __exit gps_drv_exit(void)
{
    pr_info("Entered Exit Function\n");

    device_destroy(gps_dev_class, gps_dev_no);
    class_destroy(gps_dev_class);
    cdev_del(&gps_dev_cdev);
    unregister_chrdev_region(gps_dev_no, 1);

    pr_info("Module Removed successfully\n");
}

static int gps_dev_open(struct inode* inode, struct file* file)
{
    pr_info("GPS device opened\n");

    return 0;
}

static int gps_dev_close(struct inode* inode, struct file* file)
{
    pr_info("GPS device closed\n");

    return 0;
}

static ssize_t gps_dev_write(struct file* filep,
     const char __user* buffer, size_t count, loff_t* lofft)
{
    pr_info("GPS device write called\n");

    return 0;
}

static ssize_t gps_dev_read(struct file* filep,
     char __user* buffer, size_t count, loff_t* lofft)
{
    pr_info("GPS device read called\n");

    return 0;
}

/*****************************************
*   Module init and exit registration.
*****************************************/
module_init(gps_drv_init);
module_exit(gps_drv_exit);

/*****************************************
*   Module meta information.
*****************************************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Salman Al Fariz k");
MODULE_DESCRIPTION("Device simulating GPS");



