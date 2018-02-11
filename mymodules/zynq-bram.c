#include <linux/version.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#define __MILO_DEBUG__

static int l_bram_major = 0;
static u32 l_mem = 0;
static u32 l_phy_addr = 0;
static resource_size_t l_size = 0;

static int bram_open(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG"(MILO)bram_open\n");
	return 0;
}

static int bram_close(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG"(MILO)bram_close\n");
	return 0;
}

static int bram_mmap(struct file *file, struct vm_area_struct * vma)
{
	size_t size = vma->vm_end - vma->vm_start;

	if (size > l_size)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (remap_pfn_range(vma,
				vma->vm_start,
				l_phy_addr >> PAGE_SHIFT,
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
	.mmap		= bram_mmap,
};              

static struct class *bram_class;

static int zynq_bram_probe(struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	l_size = resource_size(res);
	l_phy_addr = res->start;
	l_mem = (u32) devm_ioremap(&pdev->dev, res->start, l_size);
	if (IS_ERR((void *)l_mem))
		return PTR_ERR((void *)l_mem);

	l_bram_major = register_chrdev(0, "bram_map", &bram_fops);
	bram_class = class_create(THIS_MODULE, "bram_map");
	device_create(bram_class, NULL,
			MKDEV(l_bram_major, 0),
			NULL,
			"bram_map");

	printk(KERN_INFO "ZYNQ blk-ram Mapper driver loaded\n");

	return 0;
}

static int zynq_bram_remove(struct platform_device *pdev)
{
	device_destroy(bram_class, MKDEV(l_bram_major, 0));
	class_destroy(bram_class);

	unregister_chrdev(l_bram_major, "bram_map");

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
