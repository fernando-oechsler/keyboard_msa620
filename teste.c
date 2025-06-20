#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>

#define DRIVER_NAME "led_blink"

static struct gpio_descs *led_desc;
static struct task_struct *task;

// Thread que pisca os LEDs
static int led_thread_fn(void *data)
{
    bool led_state = false;
    unsigned long led_values;

    while (!kthread_should_stop()) {
        led_values = led_state ? ~0UL : 0UL;

        gpiod_set_array_value_cansleep(led_desc->ndescs, led_desc->desc, NULL, &led_values);

        led_state = !led_state;

        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(msecs_to_jiffies(500));
    }

    // Garante que os LEDs apaguem ao finalizar
    led_values = 0UL;
    gpiod_set_array_value_cansleep(led_desc->ndescs, led_desc->desc, NULL, &led_values);

    return 0;
}

// Init do módulo
static int __init led_init(void)
{
    struct device_node *np;

    pr_info(DRIVER_NAME ": Init\n");

    np = of_find_node_by_path("/led_pins");
    if (!np) {
        pr_err(DRIVER_NAME ": Node /led_pins não encontrado na device tree\n");
        return -ENODEV;
    }

    led_desc = gpiod_get_array_from_of_node(
    			of_find_node_by_path("/led_pins"),
    			"brcm,pins",
    			GPIOD_OUT_LOW,
    			"leds");
    of_node_put(np);

    if (IS_ERR(led_desc)) {
        pr_err(DRIVER_NAME ": Erro ao obter os GPIOs dos LEDs\n");
        return PTR_ERR(led_desc);
    }

    task = kthread_run(led_thread_fn, NULL, "led_thread");
    if (IS_ERR(task)) {
        pr_err(DRIVER_NAME ": Erro ao criar thread\n");
        gpiod_put_array(led_desc);
        return PTR_ERR(task);
    }

    return 0;
}

// Exit do módulo
static void __exit led_exit(void)
{
    pr_info(DRIVER_NAME ": Exit\n");

    if (task)
        kthread_stop(task);

    gpiod_put_array(led_desc);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fefo");
MODULE_DESCRIPTION("Driver de pisca LED");
