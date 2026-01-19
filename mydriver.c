/*
sudo cp ad7606-overlay.dtbo /boot/overlays/ad7606.dtbo 
/boot/firmware/config.txt:
 dtoverlay=ad7606
 dtoverlay=pwm

 # Копируем модуль в системную папку модулей
sudo cp mydriver.ko /lib/modules/$(uname -r)/kernel/drivers/iio/adc/
# Обновляем зависимости модулей
sudo depmod -a
# Проверяем, что модуль в системе
modinfo mydriver
*/

#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/pwm.h>
#include <linux/spi/spi.h>
#include <linux/ioctl.h>
#include "spi_reg.h"

#define MY_MAGIC 'k'
#define MY_GET_AVAILABLE _IOR(MY_MAGIC, 1, int)         // get available bytes for read data    
#define MY_SYNC_BUFFER   _IO(MY_MAGIC, 2)               //set read_index = write_index          -> It is necessary to read the latest data.
#define MY_SEEK_LATEST   _IOW(MY_MAGIC, 3, int)         // set read_index = write_index + arg   -> Required to read the entire buffer

#define DEVICE_NAME "ad7606"
#define CLASS_NAME  "myclass"
#define BUFFSIZE 15728640           // 32768*16*30                    // 8 chanels(16bit) 30 sec


static u8 BUFF[BUFFSIZE] __aligned(32);
static volatile u32 write_index = 0;
// static volatile u32 read_index = 0; -> insteded use file->f_pos OR *ppos

// static int busy_gpio = 27;    // GPIO для BUSY (вход)
// static int ctrl_gpio = 17;    // GPIO для CTRL (выход)
static int busy_gpio = 27 + 569;  // 569 - base gpiochip0
static int ctrl_gpio = 17 + 569;  // 569 - base gpiochip0
static int cs_gpio   = 8 + 569;
static int irq_number = 0;    // Номер прерывания
static int irq_count = 0;     // Счетчик прерываний



static struct pwm_device *my_pwm;
struct spi_device *spi;
static dev_t dev_number;
static struct cdev my_cdev;
static struct class *my_class;

// Флаги для отслеживания созданных ресурсов  
static bool chardev_created = false;
static bool class_created = false;
static bool pwm_enabled = false;

static int my_dev_uevent(const struct device *dev, struct kobj_uevent_env *env) { add_uevent_var(env, "DEVMODE=%#o", 0666); return 0; }
static int my_open(struct inode *inode, struct file *file) {    u32 local_write_index = READ_ONCE(write_index);
                                                                WRITE_ONCE(file->f_pos, local_write_index); 
                                                                printk(KERN_INFO "ad7606: opened\n"); 
                                                                return 0; 
                                                           }
static int my_release(struct inode *inode, struct file *file) { printk(KERN_INFO "ad7606: closed\n"); return 0; }

//----------------------------------------------------------------------------------------------
//-----------------------  c h a r   d e v i c e  ----------------------------------------------
//----------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------
static ssize_t get_available(loff_t f_pos)
{
    
    ssize_t available = 0;   
    if (write_index >= f_pos)  available = write_index - f_pos;
                            else    available = BUFFSIZE - f_pos + write_index;

return available;    
}
//-------------------------- I O C T R L -------------------------------------------------------
static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{

  //   pr_info("call my_ioctrl: get_available(file->f_pos): %d    write_pointer: %d \n", get_available(file->f_pos), write_index);
  switch (cmd) 
    {
        case MY_GET_AVAILABLE:  
                                u32 available = get_available(file->f_pos);                                 
                                if(copy_to_user((int __user *)arg, &available, sizeof(available))) {return -EFAULT;}
                                break;
        
        case MY_SYNC_BUFFER:    // for read only fresh data                                
                                u32 local_write_index = READ_ONCE(write_index);
                                WRITE_ONCE(file->f_pos, local_write_index);
                                break;
        
    case MY_SEEK_LATEST:        // for read all data  arg % 16 !!    
                                if (arg > 0 && arg <= BUFFSIZE) 
                                    {
                                        u32 local_write_index = READ_ONCE(write_index);
                                        WRITE_ONCE(file->f_pos, (local_write_index + arg) % BUFFSIZE);
                                    } else {return -EINVAL;}
                                break;      

    default:                    return -ENOTTY;
    }
    
    return 0;
}

//----------------------------------------------------------------------------------------------
static ssize_t my_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) 
{       
 //   pr_info("call myread count: %d    read_pointer: %d    write_pointer: %d \n", count, ppos, write_index);    
    if((count < 1)||(count > get_available(*ppos))) {return 0;}
     
// . . . . . . c h e c k  o v e r f l o w . . . . . . . . . . . . . 
    uint32_t distance;
    if(write_index >= *ppos) {distance = write_index - *ppos;} 
                             else {distance = (BUFFSIZE - *ppos) + write_index;}
    if(distance >= BUFFSIZE) { *ppos = (write_index+16*4) % BUFFSIZE;}   // Читатель отстал на целый буфер или более + recerve
// . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 

    if (*ppos + count <= BUFFSIZE)
        {           
            if(copy_to_user(buf, &BUFF[*ppos], count)) {return -EFAULT; }
        }
    else
        {
            size_t first_part = BUFFSIZE - *ppos;
            if(copy_to_user(buf, &BUFF[*ppos], first_part)) {return -EFAULT; }
            if(copy_to_user(&buf[first_part], &BUFF[0], count - first_part)) {return -EFAULT; }            
        }    
    *ppos = (*ppos + count) % BUFFSIZE;    
    return count; 
}
//--------------------------------------------------------------------------------------------------------------------
static ssize_t my_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) 
{
    // ssize_t len = min(count, (size_t)(BUFFSIZE - *ppos));
    // if (len == 0) {return -ENOMEM;}
    // //if (copy_from_user(BUFF + *ppos, buf, len)) {return -EFAULT; }
    // *ppos += len; 
    return count; 
}
//--------------------------------------------------------------------------------------------------------------------
static struct file_operations fops = {
    .owner = THIS_MODULE, 
    .open = my_open, 
    .release = my_release,
    .read = my_read, 
    .write = my_write,
    .llseek = noop_llseek,  // Запрещаем lseek
    .unlocked_ioctl = my_ioctl,
    .compat_ioctl = my_ioctl,
};

//------------------------------------------------------------------------------------------
////////////////////////////  О б р а б о т ч и к   п р е р ы в а н и я  ///////////////////
//------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------
//   R E A D   S P I  
//------------------------------------------------------------------------------------------
static void ad7606_read_spi(struct work_struct *work)
{
  
 //spi_read(spi, &BUFF[write_index], 16);
// struct spi_transfer t = 
//     {
//          .rx_buf = &BUFF[write_index],         
//          .len = 16,
//          .cs_change = 0, 
//     };
// struct spi_message m;

// spi_message_init(&m);
// spi_message_add_tail(&t, &m);
// gpio_set_value(cs_gpio, 0);
// spi_sync(spi, &m);
// //spi_async(spi, &m);
// gpio_set_value(cs_gpio, 1);    
//static u32 n = 0;
// my_spi_read(spireg, &BUFF[write_index]) ;
// write_index = (write_index + 16) % BUFFSIZE;
//   if(n++ > 2)
//         {    int16_t sample = (int16_t)((BUFF[write_index -16] << 8) | BUFF[write_index-15]);    
//              float d = sample*5.0/32768.0;
//              pr_info("read %4X\n", sample);
//              n= 0;
//         }


}


//------------------- t h r e a d ----------------------------------------------------------
static DECLARE_WORK(ad7606_work, ad7606_read_spi);
//------------------------------------------------------------------------------------------

//--------------------  H a n d l e r  -----------------------------------------------------
static irqreturn_t ad7606_irq_handler(int irq, void *dev_id)
{
    irq_count++;
    
    // Дергаем CTRL_PIN
     gpio_set_value(ctrl_gpio, 1);
     
 my_spi_read(spireg, &BUFF[write_index]) ;
 write_index = (write_index + 16) % BUFFSIZE;
 // schedule_work(&ad7606_work);
   gpio_set_value(ctrl_gpio, 0);
    return IRQ_HANDLED;
}
//--------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////////////////////
//--------------------------------------------------------------------------
// Функция очистки ресурсов
static void cleanup_resources(struct spi_device *pdev)
{
   // struct device *dev = &pdev->dev;
    
    // Отключаем PWM
    if (pwm_enabled && my_pwm) 
        {
            pwm_disable(my_pwm);
            pwm_enabled = false;
            pr_info("ad7606: PWM disabled\n");
        }
    
    // Очищаем chardev
    if (chardev_created) 
    {
        device_destroy(my_class, dev_number);
        chardev_created = false;
        pr_info("ad7606: Device destroyed\n");
    }
    
    if (class_created) 
        {
            class_destroy(my_class);
            class_created = false;
            pr_info("ad7606: Class destroyed\n");
        }
    
    if (dev_number) 
        {
            cdev_del(&my_cdev);
            unregister_chrdev_region(dev_number, 1);
            dev_number = 0;
            pr_info("ad7606: Chardev unregistered\n");
        }


    spi_close();    
}

///////////////////////////  P R O B E   //////////////////////////////////////
static int my_driver_probe(struct spi_device *pdev)
{
    pr_info("ad7606: PROBE function called!\n");
    int ret = 0;
    struct device *dev = &pdev->dev;
    spi = pdev;

    ret = devm_gpio_request_one(dev, cs_gpio, GPIOF_OUT_INIT_LOW, "ad7606_cs");       
    if (ret == -EPROBE_DEFER) {dev_info(dev, "ad7606: GPIO controller not ready (CTRL), retrying...\n"); }


// Настройка SPI для AD7606
    spi_init();
    // spi->mode = SPI_MODE_2;       // CPOL=0, CPHA=1    
    // spi->bits_per_word = 8;      // AD7606 отдаёт 16-битные слова
    // spi->max_speed_hz = 24000000;
    // //spi->max_speed_hz = 30000000;
    // ret = spi_setup(spi);
    // if (ret) {
    //     dev_err(dev, "ad7606: spi_setup failed: %d\n", ret);
    //     return ret;
    // }

       
    // 1. --------------  Регистрируем chardev  -----------------------------
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) { 
        pr_alert("ad7606: alloc_chrdev_region failed: %d\n", ret); 
        return ret; 
    }
    
    cdev_init(&my_cdev, &fops);
    cdev_add(&my_cdev, dev_number, 1);
    
    my_class = class_create(CLASS_NAME);
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);
        pr_alert("ad7606: class_create failed: %d\n", ret);
        goto err_chrdev;
    }
    class_created = true;
    
    my_class->dev_uevent = my_dev_uevent; 
    
    if (device_create(my_class, NULL, dev_number, NULL, DEVICE_NAME) == NULL) {
        ret = -ENOMEM;
        pr_alert("ad7606: device_create failed\n");
        goto err_class;
    }
    chardev_created = true;
    
    pr_info("ad7606: chardev registered with major %d.\n", MAJOR(dev_number));

    //                              PIN + IQR    
    // 3. Пробуем получить GPIO
    // CTRL_GPIO (выход, по умолчанию LOW)
    ret = devm_gpio_request_one(dev, ctrl_gpio, GPIOF_OUT_INIT_LOW, "ad7606_ctrl");
    if (ret) { dev_err(dev, "ad7606: Failed to request CTRL GPIO %d: %d\n", ctrl_gpio, ret); goto err_device;}
    
    // BUSY_GPIO (вход)    
    ret = devm_gpio_request_one(dev, busy_gpio, GPIOF_IN, "ad7606_busy");
    if (ret) { dev_err(dev, "ad7606: Failed to request BUSY GPIO %d: %d\n", busy_gpio, ret); goto err_device;}
    
    // 4. Получаем номер прерывания    
    irq_number = gpio_to_irq(busy_gpio);
    if (irq_number < 0) 
        {
            dev_err(dev, "ad7606: Failed to get IRQ for GPIO %d: %d\n", busy_gpio, irq_number);
            ret = irq_number;
            goto err_device;
        }
    
    // 5. Регистрируем прерывание
    ret = devm_request_irq(dev, irq_number, ad7606_irq_handler, IRQF_TRIGGER_FALLING, DEVICE_NAME, NULL);
    if (ret) 
        {
            dev_err(dev, "ad7606: Failed to request IRQ %d: %d\n", irq_number, ret);
            goto err_device;
        }    
    dev_info(dev, "AD7606 GPIOs ready: BUSY=GPIO%d (IRQ=%d), CTRL=GPIO%d\n", busy_gpio, irq_number, ctrl_gpio);
    

    // 6.                                P W M    
    pr_info("ad7606: Trying to get PWM.\n");
    my_pwm = devm_pwm_get(dev, "convst");
    if (IS_ERR(my_pwm)) 
        {
            ret = PTR_ERR(my_pwm);
            dev_err(dev, "ad7606: Failed to get PWM for convst: %d\n", ret);
            goto err_device;
        }
    
    // 7. Настраиваем PWM
    struct pwm_state state;    
    pwm_init_state(my_pwm, &state);
    state.period = 30518;      // 30.518 µs период (32 kHz)
   // state.period = 500000000;      // 
    state.duty_cycle = 300;    // 300ns импульс
    state.polarity = PWM_POLARITY_NORMAL;
    state.enabled = true;

    ret = pwm_apply_might_sleep(my_pwm, &state);
    if (ret) {
        dev_err(dev, "ad7606: Failed to apply PWM state: %d\n", ret);
        goto err_device;
    }
    pwm_enabled = true;
    
    pr_info("ad7606: PWM enabled on channel %d\n", my_pwm->hwpwm);
    dev_info(dev, "AD7606 fully initialized and ready\n");
    
    return 0;

err_device:
    cleanup_resources(pdev);
    return ret;

err_class:
    if (class_created) {
        class_destroy(my_class);
        class_created = false;
    }

err_chrdev:
    if (dev_number) {
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev_number, 1);
        dev_number = 0;
    }
    
    return ret;
}
//------------------------------------------------------------------------------------
static void my_driver_remove(struct spi_device *pdev)
{
    pr_info("ad7606: REMOVE function called. Cleaning up.\n");
    cleanup_resources(pdev);
    pr_info("ad7606: Total IRQs handled: %d\n", irq_count);    
}
//------------------------------------------------------------------------------------
static void my_driver_shutdown(struct spi_device *pdev)
{
    pr_info("ad7606: SHUTDOWN function called.\n");
    my_driver_remove(pdev);
}
//--------------------------------------------------------------------------------
static const struct of_device_id my_of_match[] = {
    { .compatible = "ctp,ad7606", },
    { }
};
MODULE_DEVICE_TABLE(of, my_of_match);
//----------------------------------------------------------------------------------
static struct spi_driver ad7606_driver = 
{
    .probe = my_driver_probe,
    .remove = my_driver_remove,
    .shutdown = my_driver_shutdown,    
    .driver = 
    { 
        .name = "ad7606", 
        .of_match_table = my_of_match,
        .owner = THIS_MODULE,
    },
};
//----------------------------------------------------------------------------------
module_spi_driver(ad7606_driver);
//----------------------------------------------------------------------------------
MODULE_SOFTDEP("pre: pinctrl-rp1 gpio-rp1");
MODULE_DESCRIPTION("AD7606 Driver with PWM and GPIO interrupts on busy");
MODULE_AUTHOR("Nick Shulga");
MODULE_LICENSE("GPL");
//----------------------------------------------------------------------------------