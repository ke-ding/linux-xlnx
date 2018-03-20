#include <linux/version.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#define __MILO_DEBUG__

#define DEV_NAME	"bram_map"
#define MAX_DEV_NO	8

typedef struct {
	u32 phy_addr;
	resource_size_t size;
	int irq;

	u32 mem;

	wait_queue_head_t read_wq;
	int data_avail;
}bram_info_t;

static bram_info_t bram_info[MAX_DEV_NO];
static int bram_major = -1;
static int dev_no = 0;

static irqreturn_t bram_interrupt(int irq, void *dev_id)
{
	bram_info_t *info = (bram_info_t *)dev_id;

	info->data_avail = 1;
	wake_up_interruptible(&info->read_wq);

	return IRQ_HANDLED;
}

static int bram_open(struct inode *inode, struct file *file)
{
	const int minor = iminor(inode);
	int err = 0;
	char intr_name[255];

	printk(KERN_DEBUG"(MILO)bram_open\n");

	file->private_data = &bram_info[minor];

	if (bram_info[minor].irq > 0) {
		sprintf(intr_name, "bram_intr%d", minor);
		err = request_irq(bram_info[minor].irq, bram_interrupt,
				IRQF_SHARED | IRQF_TRIGGER_RISING,
				intr_name,
				&bram_info[minor]);
		if (err) {
			printk(KERN_ERR"(MILO)request interrupt err (%d)\n", err);
		}
	}

	return err;
}

static int bram_close(struct inode *inode, struct file *file)
{
	const int minor = iminor(inode);

	printk(KERN_DEBUG"(MILO)bram_close\n");

	if (bram_info[minor].irq > 0) {
		free_irq(bram_info[minor].irq, 0);
	}

	return 0;
}

static ssize_t bram_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	bram_info_t *info = (bram_info_t *)file->private_data;

	wait_event_interruptible(info->read_wq, info->data_avail == 1);

	info->data_avail = 0;

	return 0;
}

static int bram_mmap(struct file *file, struct vm_area_struct * vma)
{
	bram_info_t *info = (bram_info_t *)file->private_data;
	size_t size = vma->vm_end - vma->vm_start;

	if (size > info->size)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (remap_pfn_range(vma,
				vma->vm_start,
				info->phy_addr >> PAGE_SHIFT,
				size,
				vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static struct file_operations bram_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek, 
	.open		= bram_open,
	.release	= bram_close,
	.read		= bram_read,
	.mmap		= bram_mmap,
};              

static struct class *bram_class = NULL;

static int zynq_bram_probe(struct platform_device *pdev)
{
	struct resource *res;
	int err = -1;

	if (dev_no >= MAX_DEV_NO) {
		printk(KERN_ERR"(MILO)too many %s devices\n", DEV_NAME);
		goto err_out;
	}

	init_waitqueue_head(&bram_info[dev_no].read_wq);

	bram_info[dev_no].irq = platform_get_irq(pdev, 0);
	if (bram_info[dev_no].irq < 0)
		bram_info[dev_no].irq = -1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bram_info[dev_no].size = resource_size(res);
	bram_info[dev_no].phy_addr = res->start;
	bram_info[dev_no].mem = (u32) devm_ioremap(&pdev->dev,
						bram_info[dev_no].phy_addr,
						bram_info[dev_no].size);
	if (IS_ERR((void *)bram_info[dev_no].mem))
		return PTR_ERR((void *)bram_info[dev_no].mem);

	if (bram_major < 0) {
		bram_major = register_chrdev(0, DEV_NAME, &bram_fops);
		if (bram_major < 0) {
			err = bram_major;
			goto reg_err;
		}
		bram_class = class_create(THIS_MODULE, DEV_NAME);
		if (IS_ERR(bram_class)) {
			err = PTR_ERR(bram_class);
			bram_class = NULL;
			goto class_err;
		}
	}

	device_create(bram_class, NULL,
			MKDEV(bram_major, dev_no),
			NULL,
			DEV_NAME"%d",
			dev_no);
	printk(KERN_INFO"(MILO)ZYNQ blk-ram Mapper driver loaded\n"
			"\tminor: %d\n"
			"\tphy addr: 0x%08x\n"
			"\tsize: 0x%04x\n"
			"\tirq: %d\n",
			dev_no,
			bram_info[dev_no].phy_addr,
			bram_info[dev_no].size,
			bram_info[dev_no].irq);

	++dev_no;


	return 0;

class_err:
	unregister_chrdev(bram_major, DEV_NAME);
reg_err:
	printk(KERN_ERR"(MILO)failed to register %s device (%d)\n",
			DEV_NAME, err);
err_out:
	return err;
}

static int zynq_bram_remove(struct platform_device *pdev)
{
	if (bram_class) {
		--dev_no;
		while (dev_no > 0) {
			device_destroy(bram_class, MKDEV(bram_major, dev_no));
			--dev_no;
		}

		class_destroy(bram_class);
	}

	if (bram_major > 0)
		unregister_chrdev(bram_major, DEV_NAME);

	return 0;
}

static const struct of_device_id zynq_bram_table[] = {
	{ .compatible = "zynq-bram" },
	{ },
};
MODULE_DEVICE_TABLE(of, zynq_bram_table);

static struct platform_driver zynq_bram_driver = {
	.probe		= zynq_bram_probe,
	.remove		= zynq_bram_remove,
	.driver		= {
		.name	= "zynq-bram",
		.of_match_table = zynq_bram_table,
	},
};
module_platform_driver(zynq_bram_driver);

MODULE_DESCRIPTION("map zynq bram to userspace");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("milo <milod@163.com>");
