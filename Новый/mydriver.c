#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define DEVICE_NAME "mydev"
#define CLASS_NAME  "myclass"
#define BUF_SIZE    1024

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nick");
MODULE_DESCRIPTION("Char driver for Trixie 6.12, auto /dev, 0666, no root needed");
MODULE_VERSION("1.0");

#define MYDEV_MAGIC  'k'
#define IOCTL_GET_VALUE  _IOR(MYDEV_MAGIC, 1, int)
#define IOCTL_SET_VALUE  _IOW(MYDEV_MAGIC, 2, int)

static int value = 123;  // пример внутреннего параметра

static dev_t dev_number;
static struct cdev my_cdev;
static struct class *my_class;
static char kernel_buffer[BUF_SIZE];
static struct mutex my_mutex;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Функция для установки прав 0666
static int my_dev_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0666);
    return 0;
}

// Файловые операции
//------------------------------------------------------------------------------------------------------------
static int my_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&my_mutex)) return -EBUSY;
    printk(KERN_INFO "mydev: opened\n");
    return 0;
}
//------------------------------------------------------------------------------------------------------------
static int my_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&my_mutex);
    printk(KERN_INFO "mydev: closed\n");
    return 0;
}
//------------------------------------------------------------------------------------------------------------
static ssize_t my_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{

    size_t len = min(count, (size_t)(BUF_SIZE - *ppos));
    if (len == 0) return 0;
    if (copy_to_user(buf, kernel_buffer + *ppos, len)) return -EFAULT;
    *ppos += len;
    printk(KERN_INFO "mydev: read %zu bytes\n", len);
    return len;

}
//------------------------------------------------------------------------------------------------------------
static ssize_t my_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t len = min(count, (size_t)(BUF_SIZE - *ppos));
    if (len == 0) return -ENOMEM;
    if (copy_from_user(kernel_buffer + *ppos, buf, len)) return -EFAULT;
    *ppos += len;
    printk(KERN_INFO "mydev: wrote %zu bytes\n", len);
    return len;
}

//------------------------------------------------------------------------------------------------------------
static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int tmp;

    switch (cmd) {
    case IOCTL_GET_VALUE:
        if (copy_to_user((int __user *)arg, &value, sizeof(value)))
            return -EFAULT;
        printk(KERN_INFO "mydev: IOCTL_GET_VALUE -> %d\n", value);
        break;

    case IOCTL_SET_VALUE:
        if (copy_from_user(&tmp, (int __user *)arg, sizeof(tmp)))
            return -EFAULT;
        value = tmp;
        printk(KERN_INFO "mydev: IOCTL_SET_VALUE -> %d\n", value);
        break;

    default:
        return -ENOTTY;
    }

    return 0;
}
//------------------------------------------------------------------------------------------------------------
static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_release,
    .read = my_read,
    .write = my_write,
     .unlocked_ioctl = my_ioctl,
};
//------------------------------------------------------------------------------------------------------------
// Инициализация драйвера
static int __init mydriver_init(void)
{
    int ret;

    mutex_init(&my_mutex);

    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ALERT "mydev: alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    cdev_init(&my_cdev, &fops);
    ret = cdev_add(&my_cdev, dev_number, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev_number, 1);
        printk(KERN_ALERT "mydev: cdev_add failed: %d\n", ret);
        return ret;
    }

    my_class = class_create(CLASS_NAME);
    if (IS_ERR(my_class)) {
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev_number, 1);
        printk(KERN_ALERT "mydev: class_create failed\n");
        return PTR_ERR(my_class);
    }

    my_class->dev_uevent = my_dev_uevent; // права 0666

    if (IS_ERR(device_create(my_class, NULL, dev_number, NULL, DEVICE_NAME))) {
        class_destroy(my_class);
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev_number, 1);
        printk(KERN_ALERT "mydev: device_create failed\n");
        return -1;
    }

     
    if (ret) {
        printk(KERN_ALERT "mydev: gpio_init failed: %d\n", ret);
        device_destroy(my_class, dev_number);
        class_destroy(my_class);
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev_number, 1);
        return ret;
    }

    printk(KERN_INFO "mydev: registered with major %d minor %d\n",
           MAJOR(dev_number), MINOR(dev_number));
    return 0;
}
//------------------------------------------------------------------------------------------------------------
// Выгрузка драйвера
static void __exit mydriver_exit(void)
{
    device_destroy(my_class, dev_number);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_number, 1);
    mutex_destroy(&my_mutex);
    printk(KERN_INFO "mydev: unregistered\n");
}
//------------------------------------------------------------------------------------------------------------
module_init(mydriver_init);
module_exit(mydriver_exit);
