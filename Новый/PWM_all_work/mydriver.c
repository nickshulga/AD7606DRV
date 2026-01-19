// mydriver.c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/pwm.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h> 
#include <linux/gpio.h>
#include <linux/interrupt.h>

#define DEVICE_NAME "ad7606"
#define CLASS_NAME  "myclass"
#define BUF_SIZE    1024

// #define BUSY_PIN 27
// #define CTRL_PIN 17

static int busy_gpio = 27;    // GPIO для BUSY (вход)
static int ctrl_gpio = 17;    // GPIO для CTRL (выход)
static int irq_number = 0;    // Номер прерывания
static int irq_count = 0;     // Счетчик прерываний


MODULE_LICENSE("GPL");
// ... 
static int irq_number;
static struct pwm_device *my_pwm;
static dev_t dev_number;
static struct cdev my_cdev;
static struct class *my_class;
static DEFINE_MUTEX(my_mutex); 
static char kernel_buffer[BUF_SIZE];


static int my_dev_uevent(const struct device *dev, struct kobj_uevent_env *env) {
    add_uevent_var(env, "DEVMODE=%#o", 0666); return 0; }
static int my_open(struct inode *inode, struct file *file) {
    if (!mutex_trylock(&my_mutex)) return -EBUSY; printk(KERN_INFO "mydev: opened\n"); return 0; }
static int my_release(struct inode *inode, struct file *file) {
    mutex_unlock(&my_mutex); printk(KERN_INFO "mydev: closed\n"); return 0; }
static ssize_t my_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    size_t len = min(count, (size_t)(BUF_SIZE - *ppos));
    if (len == 0) return 0; if (copy_to_user(buf, kernel_buffer + *ppos, len)) return -EFAULT; *ppos += len; return len; }
static ssize_t my_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    size_t len = min(count, (size_t)(BUF_SIZE - *ppos));
    if (len == 0) return -ENOMEM; if (copy_from_user(kernel_buffer + *ppos, buf, len)) return -EFAULT; *ppos += len; return len; }
static struct file_operations fops = {
    .owner = THIS_MODULE, .open = my_open, .release = my_release,
    .read = my_read, .write = my_write,
};
//------------------------------------------------------------------------------------------
// Обработчик прерывания (в контексте прерывания - быстрый!)
// Обработчик прерывания - только дергаем GPIO17
static irqreturn_t ad7606_irq_handler(int irq, void *dev_id)
{
    irq_count++;
    
    // Дергаем CTRL_PIN
    gpio_set_value(ctrl_gpio, 1);
    gpio_set_value(ctrl_gpio, 0);
    
    return IRQ_HANDLED;
}
//------------------------------------------------------------------------------------------
static int my_driver_probe(struct platform_device *pdev)
{
    int ret = 0;
    u32 period_ns = 30518; 
    u32 duty_ns = 1000;
    pr_info("mydev: PROBE function called! Trying to get PWM.\n");

    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) { pr_alert("mydev: alloc_chrdev_region failed\n"); return ret; }
    cdev_init(&my_cdev, &fops);
    cdev_add(&my_cdev, dev_number, 1);
    my_class = class_create(CLASS_NAME);
    if (IS_ERR(my_class)) return PTR_ERR(my_class);
    my_class->dev_uevent = my_dev_uevent; 
    device_create(my_class, NULL, dev_number, NULL, DEVICE_NAME);
    pr_info("mydev: chardev registered with major %d.\n", MAJOR(dev_number));

    // --- ПОЛУЧЕНИЕ PWM ИЗ DTS ---
    my_pwm = pwm_get(&pdev->dev, "channel0"); // Ищем по имени "channel0" из DTS
    if (IS_ERR(my_pwm)) 
        {
            dev_err(&pdev->dev, "Failed to get PWM for channel0: %ld\n", PTR_ERR(my_pwm));
            // TODO: cleanup chardev on failure
            return PTR_ERR(my_pwm);
        }
    
// 1. Сначала получаем текущее состояние (с параметрами из DTS)
struct pwm_state state;
pwm_get_state(my_pwm, &state);

// 2. Логируем, что получили
dev_info(&pdev->dev, "PWM from DTS: period=%llu, duty_cycle=%llu, polarity=%d",
         state.period, state.duty_cycle, state.polarity);

// 3. Настраиваем нужные параметры (например, для AD7606)
// AD7606 требует короткий импульс CONVST (~50ns)
state.period = 30518;      // 10µs период (100 kHz)
state.duty_cycle = 300;     // 50ns импульс (0.5% скважность)
state.polarity = PWM_POLARITY_NORMAL;
state.enabled = true;

// 4. Применяем настройки
ret = pwm_apply_might_sleep(my_pwm, &state);
if (ret) {
    dev_err(&pdev->dev, "Failed to apply PWM state: %d\n", ret);
    return ret;
}
   
        
    // ret = pwm_config(my_pwm, duty_ns, period_ns);
    // if (ret < 0) { dev_err(&pdev->dev, "Failed to config PWM: %d\n", ret); pwm_put(my_pwm); return ret; }
    // ret = pwm_enable(my_pwm);
    // if (ret < 0) { dev_err(&pdev->dev, "Failed to enable PWM: %d\n", ret); pwm_put(my_pwm); return ret; }
    pr_info("mydev: PWM enabled on channel %d.\n", my_pwm->hwpwm);
    
//...................................   I N T E R R U P T  ...................................................
struct device *dev = &pdev->dev;  
// 1. Настраиваем CTRL_GPIO (выход, по умолчанию LOW) 
  ret = devm_gpio_request_one(dev, ctrl_gpio, GPIOF_OUT_INIT_LOW, "ad7606_ctrl");
    if (ret) {
        dev_err(dev, "Failed to request CTRL GPIO %d: %d\n", ctrl_gpio, ret);
        return ret;
    }
    
    // 2. Настраиваем BUSY_GPIO (вход)
    ret = devm_gpio_request_one(dev, busy_gpio, GPIOF_IN, "ad7606_busy");
    if (ret) {
        dev_err(dev, "Failed to request BUSY GPIO %d: %d\n", busy_gpio, ret);
        return ret;
    }
    
    // 3. Получаем номер прерывания
    irq_number = gpio_to_irq(busy_gpio);
    if (irq_number < 0) {
        dev_err(dev, "Failed to get IRQ for GPIO %d: %d\n", busy_gpio, irq_number);
        return irq_number;
    }
    
    // 4. Регистрируем прерывание по СПАДУ (1→0)
    ret = devm_request_irq(dev, irq_number, ad7606_irq_handler,
                          IRQF_TRIGGER_FALLING, DEVICE_NAME, NULL);
    if (ret) {
        dev_err(dev, "Failed to request IRQ %d: %d\n", irq_number, ret);
        return ret;
    }
    
    dev_info(dev, "AD7606 ready: BUSY=GPIO%d (IRQ=%d), CTRL=GPIO%d\n",
             busy_gpio, irq_number, ctrl_gpio);
//............................................................................................................



    return 0;
}
//------------------------------------------------------------------------------------------
static void my_driver_remove(struct platform_device *pdev)
{
    pr_info("mydev: REMOVE function called. Cleaning up.\n");
    if (my_pwm) {
        pwm_disable(my_pwm);
        pwm_put(my_pwm);
    }
    device_destroy(my_class, dev_number);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_number, 1);
    // mutex_destroy(&my_mutex); // не нужно с DEFINE_MUTEX
    // Нет return 0;
}
//-----------------------------------------------------------------------------------------
static const struct of_device_id my_of_match[] = {
    { .compatible = "ctp,ad7606-pwm", }, // ИСПОЛЬЗУЕМ РАБОЧУЮ СТРОКУ
    { }
};
MODULE_DEVICE_TABLE(of, my_of_match);

static struct platform_driver ad7606_driver = {
    .probe = my_driver_probe,
    .remove = my_driver_remove,
    .driver = { .name = "ad7606_driver", .of_match_table = my_of_match, },
};
module_platform_driver(ad7606_driver);

MODULE_SOFTDEP("pre: pinctrl-rp1"); 
