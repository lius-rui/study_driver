/*************字符设备驱动模板**********************/
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
////
#define MEM_SIZE  0x1000 //４Ｋ
#define MEM_CLEAR  0x01
#define GLOBALMEM_MAJOR   230 //主设备号
#define GLOBALMEM_MAGIC  'g'  //幻数
#define MEM_CLEAR  _IO(GLOBALMEM_MAGIC,0)  //清空内存的ioctl命令
#define DEVICE_NUM  10  //最多同时支持10个同类设备
#define barrier()  __asm__ __volatile__("": : :"memory")

static int globalmem_major = GLOBALMEM_MAJOR;
module_param(globalmem_major,int,S_IRUGO);

//自定义的字符设备结构体
//testetetettetet
struct globalmem_dev
{
	struct cdev cdev;//内核自带的字符设备
	unsigned char mem[MEM_SIZE];  //开辟的内存空间大小
	unsigned int current_len;  //当前fifo中有效数据长度
	struct mutex my_mutex;  //互斥体，应为本驱动会使用可能阻塞的函数，所以使用互斥体而不是自旋锁
	wait_queue_head_t r_wait; //读等待队列头
	wait_queue_head_t w_wait; //写等待队列头
	struct fasync_struct *async_queue;   //异步IO的结构体
};

struct globalmem_dev *globalmem_devp;  //设备结构体指针，后面会赋值给私有数据使用



static ssize_t globalmem_read(struct  file *filp, char __user *buf, size_t size, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned int count  = size;
	int ret = 0;
	struct globalmem_dev  *dev = filp->private_data;
	DECLARE_WAITQUEUE(wait,current);  //初始化定义一个等待队列的元素wait
	add_wait_queue(&dev->r_wait,&wait);  //添加元素到读等待队列

	mutex_lock(&dev->my_mutex);

	while(dev->current_len == 0)  //当前fifo长度为0，无数可读，即需要阻塞等待
	{
		if(filp->f_flags & O_NONBLOCK)  //如果设置了非阻塞标志位，没读到数据就直接返回
		{
			ret = -EAGAIN;
			goto out;
		}
		__set_current_state(TASK_INTERRUPTIBLE); //改变当前进程状态为可中断的睡眠状态，达到阻塞等待的目的
		mutex_unlock(&dev->my_mutex);

		schedule();  //开启调度，CPU将调度到其他进程
		if(signal_pending(current))
		{
			ret = -ERESTARTSYS;
			goto out2;
		}

		mutex_lock(&dev->my_mutex);
	}

	if(count > dev->current_len)
		count = dev->current_len;


	if(copy_to_user(buf,dev->mem, count))  //拷贝数据到应用层
	{
		return -EFAULT;
		goto out;
	}
	else
	{
		memcpy(dev->mem, dev->mem + count, dev->current_len - count);
		dev->current_len -= count; //当前可读的fifo长度
	}

	wake_up_interruptible(&dev->w_wait);  //读出数据后，FIFO有空间来写入，所以唤醒写操作的等待队列
	ret = count;

	out:
	mutex_unlock(&dev->my_mutex);
	out2:
	remove_wait_queue(&dev->r_wait,&wait);
	set_current_state(TASK_RUNNING);

	return ret;
}

static ssize_t globalmem_write(struct file *filp, const char __user *buf,size_t size, loff_t *ppos)
{
	unsigned long p = *ppos;
	unsigned int count = size;
	int ret = 0;
	struct globalmem_dev *dev = filp->private_data;

	DECLARE_WAITQUEUE(wait, current); //初始化定义一个wait等待队列元素

	mutex_lock(&dev->my_mutex);
	add_wait_queue(&dev->w_wait,&wait);

	while(dev->current_len == MEM_SIZE)  //FIFO空间已满，无空间可写，需要阻塞
	{
		if(filp->f_flags & O_NONBLOCK)
		{
			ret = -EAGAIN;
			goto out;
		}

		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&dev->my_mutex);

		schedule();
		if(signal_pending(current))
		{
			ret = -ERESTARTSYS;
			goto out2;			
		}

		mutex_lock(&dev->my_mutex);
	}
	if(count > MEM_SIZE - dev->current_len)
			count = MEM_SIZE - dev->current_len;

	if(copy_from_user(dev->mem + dev->current_len, buf, count))
	{
		return -EFAULT;
		goto out;
	}
	else
	{
		dev->current_len = dev->current_len + count;
		printk(KERN_INFO"write data ok!\r\n");
		wake_up_interruptible(&dev->r_wait);  //写数据完成，唤醒读操作
		if(dev->async_queue)   //增加异步通知部分
		{
			kill_fasync(&dev->async_queue,SIGIO,POLL_IN);  //如果设备可读了，通知应用进程
			printk(KERN_DEBUG "%s kill SIGIO \n",__func__);
		}
		ret = count;
	}
	out:
	mutex_unlock(&dev->my_mutex);
	out2:
	remove_wait_queue(&dev->w_wait,&wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static loff_t globalmem_llseek(struct file *filp, loff_t offset , int orig)
{
	loff_t ret = 0;
	switch(orig)
	{
	case 0 :  //从文件开头位置seek,SEEK_SET
		if(offset < 0 || ((unsigned int)offset > MEM_SIZE))
		{
			ret = -EINVAL;
			break;
		}
		filp->f_pos = (unsigned int)offset;
		ret = filp->f_pos;
		break;
	case 1:   //从文件当前位置开始seek,　SEEK_CUR
		if((filp->f_pos + (unsigned int)offset) > MEM_SIZE || (filp->f_pos + (unsigned int)offset) < 0)
		{
			ret = -EINVAL;
			break;
		}
		filp->f_pos += (unsigned int)offset;
		ret = filp->f_pos;
		break;
	default:
		ret = -EINVAL;
		 break;
	}

	return ret;
}

static long globalmem_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
	struct globalmem_dev *dev = filp->private_data;
	switch(cmd)
	{
	case MEM_CLEAR:
		mutex_lock(&dev->my_mutex);
		memset(dev->mem,0,MEM_SIZE);
		mutex_unlock(&dev->my_mutex);
		printk(KERN_INFO"globalmem is set zero\n");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int globalmem_open(struct inode *inode, struct file *filp)
{
	//filp->private_data = globalmem_devp;
	struct globalmem_dev *dev = container_of(inode->i_cdev,struct globalmem_dev,cdev);
	filp->private_data = dev;
	return 0;
}
//内核实现支持异步IO的操作
static int globalmem_fasync(int fd , struct file *filp , int mode)
{
	struct globalmem_dev *dev = filp->private_data;
	return fasync_helper(fd,filp,mode,&dev->async_queue);

}


static int globalmem_release(struct inode *inode, struct file *filp)
{
	globalmem_fasync(-1,filp,0);
	return 0;
}

static unsigned int globalmem_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	struct globalmem_dev *dev = filp->private_data;

	mutex_lock(&dev->my_mutex);
	poll_wait(filp, &dev->r_wait,wait);
	poll_wait(filp, &dev->w_wait,wait);

	if(dev->current_len != 0)
	{
		mask |= POLLIN | POLLRDNORM;
	}

	if(dev->current_len != MEM_SIZE)
	{
		mask |= POLLOUT | POLLWRNORM;
	}

	mutex_unlock(&dev->my_mutex);
	return mask;

}



//设备驱动到系统调用的映射,由ＶＦＳ完成
static const struct file_operations globalmem_fops = {

	.owner = THIS_MODULE,
	.llseek = globalmem_llseek,
	.read = globalmem_read,
	.write = globalmem_write,
	.unlocked_ioctl = globalmem_ioctl,
	.open = globalmem_open,
	.release = globalmem_release,
	.poll = globalmem_poll,
	.fasync = globalmem_fasync,


};


static void globalmem_setup_cdev(struct globalmem_dev*dev,int index)
{
	int err,devno = MKDEV(globalmem_major,index);

	cdev_init(&dev->cdev,&globalmem_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev,devno,1);
	if(err)
		printk(KERN_NOTICE"add globalmem err\n");

}


//驱动模块加载函数
static int __init globalmem_init(void)
{

	int ret,i;
	local_irq_disable();  //屏蔽中断，进入临界区
	dev_t devno = MKDEV(globalmem_major,0); //合成设备号
	local_irq_enable();   //开中断
	if(globalmem_major)
		ret = register_chrdev_region(devno,DEVICE_NUM,"globalmem");
	else{
		ret = alloc_chrdev_region(&devno,0,DEVICE_NUM,"globalmem");
		globalmem_major = MAJOR(devno);
	}

	if(ret < 0){
		return ret;
	}

	globalmem_devp = kmalloc(sizeof(struct globalmem_dev)*DEVICE_NUM,GFP_KERNEL);
	if(!globalmem_devp){

		ret = -ENOMEM;
		goto fail_malloc;

	}
	for(i = 0; i<DEVICE_NUM; i++)
		globalmem_setup_cdev(globalmem_devp + i,i);

	mutex_init(&globalmem_devp->my_mutex);  //初始化设备的互斥体 
	init_waitqueue_head(&globalmem_devp->r_wait); //初始化等待队列头
	init_waitqueue_head(&globalmem_devp->w_wait);
	return  0;

fail_malloc:
	unregister_chrdev_region(devno,1);
	return ret;
}
module_init(globalmem_init);

static void __exit globalmem_exit(void)
{
	int i = 0;
	for(i=0; i<DEVICE_NUM; i++)
		cdev_del(&(globalmem_devp + i)->cdev);
	kfree(globalmem_devp);
	unregister_chrdev_region(MKDEV(globalmem_major,0),DEVICE_NUM);
}
module_exit(globalmem_exit);

MODULE_AUTHOR("liushuang <269620154@qq.com>");
MODULE_LICENSE("GPL v2");
