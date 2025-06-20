#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>

static struct gpio_descs *led_desc;
static struct task_struct *blink_thread;

// Função da thread
static int blink_thread_fn(void *data)
{
    bool led_state = false;

    while (!kthread_should_stop()) {
        led_state = !led_state;

        // Atualiza o estado dos LEDs
        gpiod_set_array_value_cansleep(led_desc->ndescs, led_desc->desc, NULL,
                                       (unsigned long[]){led_state, led_state});

        // Espera 500ms sem travar a CPU
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(msecs_to_jiffies(500));
    }

    return 0;
}

// Função INIT
static int __init led_driver_init(void)
{
    struct device_node *np;

    pr_info("led_blink: Init\n");

    // Acha o nó no device tree
    np = of_find_node_by_name(NULL, "multitherm_gpio");
    if (!np) {
        pr_err("led_blink: Node multitherm_gpio não encontrado!\n");
        return -ENODEV;
    }

    // Pega os GPIOs dos LEDs
    led_desc = gpiod_get_array(np, "led", GPIOD_OUT_LOW);
    of_node_put(np);

    if (IS_ERR(led_desc)) {
        pr_err("led_blink: Falha ao pegar led-gpios\n");
        return PTR_ERR(led_desc);
    }

    // Cria a thread
    blink_thread = kthread_run(blink_thread_fn, NULL, "led_blink_thread");
    if (IS_ERR(blink_thread)) {
        pr_err("led_blink: Falha na criação da thread\n");
        gpiod_put_array(led_desc);
        return PTR_ERR(blink_thread);
    }

    return 0;
}

// Função EXIT
static void __exit led_driver_exit(void)
{
    pr_info("led_blink: Exit\n");

    if (blink_thread)
        kthread_stop(blink_thread);

    if (led_desc)
        gpiod_put_array(led_desc);
}

module_init(led_driver_init);
module_exit(led_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("fefo");
MODULE_DESCRIPTION("Driver para piscar LEDs via device tree");
