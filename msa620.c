#include <linux/module.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/proc_fs.h>

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("fernando-oechsler");
MODULE_DESCRIPTION("teste");

/* Declate the probe and remove functions */
static int dt_probe(struct platform_device *pdev);
static void dt_remove(struct platform_device *pdev);

static struct of_device_id my_driver_ids[] = {
	{
		.compatible = "keyboard,msa620",
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_driver_ids);

static struct platform_driver my_driver = {
	.probe = dt_probe,
	.remove = dt_remove,
	.driver = {
		.name = "my_device_driver",
		.of_match_table = my_driver_ids,
	},
};

/* GPIO variable */
static struct gpio_desc *my_leds = NULL;

static struct proc_dir_entry *proc_file;

/**
 * @brief Write data to buffer
 */
static ssize_t my_write(struct file *File, const char *user_buffer, size_t count, loff_t *offs) {
    int i, val;

    if (user_buffer[0] != '0' && user_buffer[0] != '1')
        return count;

    val = user_buffer[0] - '0';

    for (i = 0; i < my_leds->ndescs; i++) {
        gpiod_set_value(my_leds->desc[i], val);
    }

    return count;
}

static struct proc_ops fops = {
	.proc_write = my_write,
};

/**
 * @brief This function is called on loading the driver 
 */
static int dt_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;

	printk("dt_gpio - Now I am in the probe function!\n");

	/* Check for device properties */
	if(!device_property_present(dev, "led-gpios")) {
		printk("dt_gpio - Error! Device property 'led' not found!\n");
		return -1;
	}

	/* Init GPIO */
	my_leds = gpiod_get_array(&pdev->dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(my_leds)) {
    		printk("dt_gpio - Error! Could not setup the LED GPIOs\n");
    		return PTR_ERR(my_leds);
	}

	/* Creating procfs file */
	proc_file = proc_create("my-led", 0666, NULL, &fops);
	if(proc_file == NULL) {
		printk("procfs_test - Error creating /proc/my-led\n");
		gpiod_put_array(my_leds);
		return -ENOMEM;
	}


	return 0;
}

/**
 * @brief This function is called on unloading the driver 
 */
static void dt_remove(struct platform_device *pdev) {
	printk("dt_gpio - Now I am in the remove function\n");
	gpiod_put_array(my_leds);
	proc_remove(proc_file);
}

/**
 * @brief This function is called, when the module is loaded into the kernel
 */
static int __init my_init(void) {
	printk("dt_gpio - Loading the driver...\n");
	if(platform_driver_register(&my_driver)) {
		printk("dt_gpio - Error! Could not load driver\n");
		return -1;
	}
	return 0;
}

/**
 * @brief This function is called, when the module is removed from the kernel
 */
static void __exit my_exit(void) {
	printk("dt_gpio - Unload driver");
	platform_driver_unregister(&my_driver);
}

module_init(my_init);
module_exit(my_exit);
