/*************平台设备驱动模板---内核中断和时钟操作**********************/
/************************************
 *
 * 中断底半部主要用法
 * 1.tasklet(不允许睡眠)
 * void my_tasklet_func(unsigned long);
 * DECLARE_TASKLET(my_tasklet,my_tasklet_func,data);
 * tasklet_schedule(&my_tasklet); //调度tasklet
 *
 * 2.工作队列（可以睡眠）
 * struct work_struct my_wq;   //定义一个工作队列
 * void my_wq_func(struct work_struct *work);// 定义一个处理函数
 * INIT_WORK(&my_wq, my_wq_func);   //初始化并绑定处理函数
 * schdeule_work(&my_wq);      //调度工作队列
 *
 *3.软中断(不可以睡眠)
 *
 *4.threaded_irq
 *
 *内核定时器用法
 *
 *
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/iio/iio.h>

#define SECON_MAJOR  256  //秒设备，住设备号
static int second_major = SECON_MAJOR;
module_param(second_major,int, S_IRUGO);

struct second_dev   //秒设备结构体
{
	struct cdev cdev;   //字符设备
	atomic_t counter;
	struct timer_list s_timer; //内核定时器
};

static struct second_dev *second_devp;  //设备结构体指针

//内核定时器中断服务函数
static void second_timer_handler(unsigned long arg)
{
	mod_timer(&second_devp->s_timer,jiffies + HZ);  //修改定时器到期时间，触发下一次定时
	atomic_inc(&second_devp->counter);     //增加秒计数
	printk(KERN_INFO "current jiffies is %ld \n",jiffies);
}

//设备打开函数，初始化定时器和计数值
static int second_open(struct inode *inode , struct file *filp)
{
	init_timer(&second_devp->s_timer);  //初始化内核定时器
	second_devp->s_timer.function = &second_timer_handler;  //设置中断回调
	second_devp->s_timer.expires = jiffies + HZ;  //设置溢出时间
	add_timer(&second_devp->s_timer);   //添加定时器到内核定时器链表
	atomic_set(&second_devp->counter,0);  //设置秒计数值为0
	return 0;
}


//设备释放函数，删除定时器操作
static int second_release(struct inode *inode, struct file *filp)
{
	del_timer(&second_devp->s_timer); //删除内核定时器

	return 0;
}


//设备读函数，将当前定时器计数值读取到用户空间
static ssize_t second_read(struct file *filp, char __user* buf, size_t count,loff_t *ppos)
{

	int counter;

	counter = atomic_read(&second_devp->counter);  //获取定时器计数值

	if(put_user(counter,(int*)buf))  //复制计数值到用户空间
			return -EFAULT;
	else
			return sizeof(unsigned int);
}

//设备操作结构体
static const struct file_operations second_fops =
{
	.owner = THIS_MODULE,
	.open  = second_open,
	.release = second_release,
	.read = second_read,

};



//注册函数主要完成字符设备的初始化和添加
static void second_setup_cdev(struct second_dev *dev, int index)
{
	int err, devno = MKDEV(second_major,index);  //合成设备号，主设备号+从设备号
	cdev_init(&dev->cdev,&second_fops);  //初始化字符设备
	dev->cdev.owner = THIS_MODULE;

	err = cdev_add(&dev->cdev,devno,1);   //添加字符设备到系统
	if(err)
	{
		printk(KERN_ERR"failed to add second device!\n");
	}
}


static int second_probe(struct platform_device *pdev)
{
	int ret = 0;
	dev_t devno = MKDEV(second_major,0);

	if(second_major)
		ret = register_chrdev_region(devno,1,"second");
	else
	{
		ret = alloc_chrdev_region(&devno,0,1,"second");
		second_major = MAJOR(devno);
	}

	if(ret < 0)
		return ret;

	second_devp = kzalloc(sizeof(*second_devp),GFP_KERNEL);//dev_kzalloc(&pdev->dev,sizeof(*second_devp),GFP_KERNEL);
	if(!second_devp)
	{
		ret = -ENOMEM;
		goto fail_malloc;
	}

	second_setup_cdev(second_devp,0);

	return 0;

fail_malloc:
	unregister_chrdev_region(devno,1);
	return ret;

}


static int second_remove(struct platform_device *pdev)
{
	cdev_del(&second_devp->cdev);
	unregister_chrdev_region(MKDEV(second_major,0),1);

	return 0;
}

static struct platform_driver second_drive = {

	.driver = {
			.name = "second",
			.owner = THIS_MODULE,
	},
	.probe = second_probe,
	.remove = second_remove,
};



module_platform_driver(second_drive);

MODULE_AUTHOR("liushuang <269620154@qq.com>");
MODULE_LICENSE("GPL v2");


