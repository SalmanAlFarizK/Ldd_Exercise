/**************************************************************************************************
*                                       Global Includes.
**************************************************************************************************/
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <linux/ioctl.h>


/**************************************************************************************************
*                                       Macro Definitons.
**************************************************************************************************/
#define DEVICE_NUMBER                                   ("Smart_light_controller_no")
#define DEVICE_CLASS                                    ("Smart_light_controller_class")
#define DEVICE_NAME                                     ("Smart_light_controller_device")
#define TURN_ON_LED                                     _IOW('a', 1, int*)
#define TURN_OFF_LED                                    _IOW('a', 2, int*)
#define SET_TEMPERATURE                                 _IOW('a', 3, int*)
#define SET_BRIGHTNESS                                  _IOW('a', 4, int*)
#define GET_CURRENT_LED_STATE                           _IOR('a', 5, int*)
#define LIGHT_BIIGHTNESS_MAX_RANGE                      (100)
#define LIGHT_BIIGHTNESS_MIN_RANGE                      (0)
#define TEMP_MIN_RANGE                                  (2000)
#define TEMP_MAX_RANGE                                  (6500)
#define LED_OFF                                         (0)
#define LED_ON                                          (1)


/**************************************************************************************************
*                                     Structure Declaration.
**************************************************************************************************/
struct light_state
{
    atomic_t brightness;
    atomic_t temperature;
    atomic_t is_on;
    atomic_t active_users;
};


/**************************************************************************************************
*                                       Function Declarations.
**************************************************************************************************/
static int __init smrt_lt_cntlr_init(void);
static void __exit smrt_lt_cntlr_exit(void);
static ssize_t smrt_lt_cntlr_read(struct file* filep, char* __user buffer,
     size_t count, loff_t* lofft);
static ssize_t smrt_lt_cntlr_write(struct file* filep, const char* __user buffer,
     size_t count, loff_t* lofft);
static int smrt_lt_cntlr_open(struct inode* inode, struct file* file);
static int smrt_lt_ctrlr_release(struct inode* inode, struct file* file);
static long smrt_lt_cntlr_ioctl(struct file* file, unsigned int cmd, unsigned long args);
static void init_atomic_vars(void);
static void turn_on_led(void);
static void turn_off_led(void);
static void set_temperature(int temperature);
static void set_brightness(int brightness);

/**************************************************************************************************
*                                     Global Data Initialization.
**************************************************************************************************/
struct light_state smart_led;
dev_t devno_smrt_ctrlr;
struct cdev cdev_smrt_ctrlr;
struct class* class_smrt_ctrlr;
struct file_operations f_ops_smrt_ctrlr;
struct file_operations f_ops_smrt_ctrlr = 
{
    .owner          = THIS_MODULE,
    .open           = smrt_lt_cntlr_open,
    .release        = smrt_lt_ctrlr_release,
    .read           = smrt_lt_cntlr_read,
    .write          = smrt_lt_cntlr_write,
    .unlocked_ioctl = smrt_lt_cntlr_ioctl
};


/**************************************************************************************************
*                                     Function Definitions.
**************************************************************************************************/

/**************************************************************************************************
* Init Function.
**************************************************************************************************/
static int __init smrt_lt_cntlr_init(void)
{
    pr_info("Entered the init function\n");

    if(alloc_chrdev_region(&devno_smrt_ctrlr, 0, 1, DEVICE_NUMBER) < 0)
    {
        pr_info("Error in creation of device number\n");

        return -1;
    }

    pr_info("Major number: %d Minor Number: %d\n", MAJOR(devno_smrt_ctrlr),
             MINOR(devno_smrt_ctrlr));

    cdev_init(&cdev_smrt_ctrlr, &f_ops_smrt_ctrlr);
    
    if(cdev_add(&cdev_smrt_ctrlr, devno_smrt_ctrlr, 1) < 0)
    {
        pr_info("Error in adding cdev\n");

        goto r_cdev;
    }

    if(IS_ERR(class_smrt_ctrlr = class_create(DEVICE_CLASS)))
    {
        pr_info("Error in creating class\n");

        goto r_class;
    }

    if(IS_ERR(device_create(class_smrt_ctrlr, NULL, devno_smrt_ctrlr,
         NULL, DEVICE_NAME)))
    {
        pr_info("Error in creating device\n");

        goto r_device;
    }

    init_atomic_vars();

    pr_info("Successfully inserted the module\n");

    return 0;

r_device:
    class_destroy(class_smrt_ctrlr);

r_class:
    cdev_del(&cdev_smrt_ctrlr);

r_cdev:
    unregister_chrdev_region(devno_smrt_ctrlr, 1);

    return -1;
}

/**************************************************************************************************
* Exit Function.
**************************************************************************************************/
static void __exit smrt_lt_cntlr_exit(void)
{
    pr_info("Entered the exit function\n");

    device_destroy(class_smrt_ctrlr, devno_smrt_ctrlr);

    class_destroy(class_smrt_ctrlr);

    cdev_del(&cdev_smrt_ctrlr);

    unregister_chrdev_region(devno_smrt_ctrlr, 1);

    return;
}

/**************************************************************************************************
* Read Function.
**************************************************************************************************/
static ssize_t smrt_lt_cntlr_read(struct file* filep, char* __user buffer,
    size_t count, loff_t* lofft)
{
    pr_info("Entered read function\n");

    return 0;
}

/**************************************************************************************************
* Write Function.
**************************************************************************************************/
static ssize_t smrt_lt_cntlr_write(struct file* filep, const char* __user buffer,
     size_t count, loff_t* lofft)
{
    pr_info("Entered write function\n");

    return 0;
}

/**************************************************************************************************
* Open Function.
**************************************************************************************************/
static int smrt_lt_cntlr_open(struct inode* inode, struct file* file)
{
    pr_info("Open Function\n");

    atomic_inc(&smart_led.active_users);

    return 0;
}

/**************************************************************************************************
* Release Function.
**************************************************************************************************/
static int smrt_lt_ctrlr_release(struct inode* inode, struct file* file)
{
    pr_info("Release Function\n");

    atomic_dec(&smart_led.active_users);

    return 0;
}

/**************************************************************************************************
* IOCTL Function.
**************************************************************************************************/
static long smrt_lt_cntlr_ioctl(struct file* file, unsigned int cmd, unsigned long args)
{
    int temperature = 0;
    int brightness = 0;

    switch(cmd)
    {
        case TURN_ON_LED:
        {
            turn_on_led();

            break;
        }

        case TURN_OFF_LED:
        {
            turn_off_led();

            break;
        }

        case SET_TEMPERATURE:
        {
            if(copy_from_user(&temperature, (int*)args, sizeof(int)))
            {
                pr_info("Error in temperature copying\n");

                return -EFAULT;
            }

            set_temperature(temperature);

            break;
        }

        case SET_BRIGHTNESS:
        {
            if(copy_from_user(&brightness, (int*)args, sizeof(int)))
            {
                pr_info("Error in copying the brightness of the light\n");

                return -EFAULT;
            }

            set_brightness(brightness);

            break;
        }

        case GET_CURRENT_LED_STATE:
        {
            if(copy_to_user((struct light_state*)args, &smart_led, sizeof(struct light_state)))
            {
                pr_info("Error in copying state of the smart light\n");

                return -EFAULT;
            }

            break;
        }

        default:
        {
            pr_info("Default got executed\n");

            return -EINVAL;
        }
    }

    return 0;
}

/**************************************************************************************************
* Atomic variables init function.
**************************************************************************************************/
static void init_atomic_vars(void)
{
    atomic_set(&smart_led.brightness, 0);

    atomic_set(&smart_led.temperature, 0);

    atomic_set(&smart_led.is_on, 0);

    atomic_set(&smart_led.active_users, 0);

    return;
}

/**************************************************************************************************
* Function to turn on the led.
**************************************************************************************************/
static void turn_on_led(void)
{
    if(LED_OFF == atomic_read(&smart_led.is_on))
    {
        atomic_set(&smart_led.is_on, LED_ON);

        pr_info("Turned on led\n");
    }
    else
    {
        pr_info("Led is already on\n");
    }

    return;
}

/**************************************************************************************************
* Function to turn off the led.
**************************************************************************************************/
static void turn_off_led(void)
{
    if(LED_ON == atomic_read(&smart_led.is_on))
    {
        atomic_set(&smart_led.is_on, LED_OFF);

        pr_info("Turned off led\n");
    }
    else
    {
        pr_info("Led is already off\n");
    }

    return;
}

/**************************************************************************************************
* Function to set the temperature value.
**************************************************************************************************/
static void set_temperature(int temperature)
{
    if((temperature >= TEMP_MIN_RANGE)
    && (temperature <= TEMP_MAX_RANGE))
    {
        atomic_set(&smart_led.temperature, temperature);
    }
    else
    {
        pr_info("Temperature is out of range\n");
    }

    return;
}

/**************************************************************************************************
* Function to set the brightness value.
**************************************************************************************************/
static void set_brightness(int brightness)
{
    if((brightness >= LIGHT_BIIGHTNESS_MIN_RANGE) 
    && (brightness <= LIGHT_BIIGHTNESS_MAX_RANGE))
    {
        atomic_set(&smart_led.brightness, brightness);
    }
    else
    {
        pr_info("Brightness value is out of range\n");
    }

    return;
}


/**************************************************************************************************
*                                     Init and Exit Registration.
**************************************************************************************************/
module_init(smrt_lt_cntlr_init);
module_exit(smrt_lt_cntlr_exit);

/**************************************************************************************************
*                                     Module Meta Information.
**************************************************************************************************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Salman Al Fariz K");
MODULE_DESCRIPTION("A driver for controlling smart light");