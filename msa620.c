#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>
#include <linux/input.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fernando Oechsler");
MODULE_DESCRIPTION("Driver teclado MSA620 com thread, input, debounce e Led de status");
MODULE_VERSION("1.0");

#define NUM_ROWS 6
#define NUM_COLS 5

/* Debounce config */
#define DEBOUNCE_SAMPLES 4
#define DEBOUNCE_TIME_MS 40

static struct gpio_descs *row_gpios;
static struct gpio_descs *col_gpios;
static struct gpio_desc *ledtec_gpio;

static struct input_dev *input_dev;
static struct task_struct *keyboard_task;

/* Key map */
static const int key_map[NUM_ROWS][NUM_COLS] = {
    {KEY_F5, KEY_F4, KEY_F3, KEY_F2, KEY_F1},
    {KEY_UP, -1, KEY_3, KEY_2, KEY_1},
    {KEY_RIGHT, KEY_LEFT, -1, -1, -1},
    {KEY_DOWN, -1, KEY_6, KEY_5, KEY_4},
    {KEY_ENTER, KEY_ESC, KEY_9, KEY_8, KEY_7},
    {-1, -1 , -1, KEY_0, -1}
};

/* Debounce function */
static bool debounce_read(struct gpio_desc *gpio) {
    int stable_count = 0;
    unsigned long start = jiffies;
    unsigned long interval = msecs_to_jiffies(DEBOUNCE_TIME_MS) / DEBOUNCE_SAMPLES;
    bool last = gpiod_get_value(gpio);

    while (time_before(jiffies, start + msecs_to_jiffies(DEBOUNCE_TIME_MS))) {
        bool now = gpiod_get_value(gpio);
        if (now == last) {
            stable_count++;
        } else {
            stable_count = 0;
        }
        last = now;

        if (stable_count >= DEBOUNCE_SAMPLES)
            break;

        while (time_before(jiffies, start + interval)) {
            cpu_relax();
        }
        start = start + interval;
    }

    return last;
}

/* Thread */
static int keyboard_thread_fn(void *data) {
    bool led_state = false;
    unsigned long led_jiffies = jiffies;
    int col, row;
    bool key_pressed = false;

    while (!kthread_should_stop()) {
        key_pressed = false;

        for (col = 0; col < NUM_COLS; col++) {
            /* Ativa coluna (LOW) */
            gpiod_set_value(col_gpios->desc[col], 0);

            for (row = 0; row < NUM_ROWS; row++) {
                int keycode = key_map[row][col];
                if (keycode == -1)
                    continue;

                bool val = debounce_read(row_gpios->desc[row]);

                if (!val) { /* Pressionado */
                    key_pressed = true;
                    input_report_key(input_dev, keycode, 1);
                    input_sync(input_dev);
                } else {
                    input_report_key(input_dev, keycode, 0);
                    input_sync(input_dev);
                }
            }

            /* Desativa coluna (HIGH) */
            gpiod_set_value(col_gpios->desc[col], 1);
        }

        /* LED control */
        if (key_pressed) {
            gpiod_set_value(ledtec_gpio, 0);  /* LED ACESO */
        } else {
            if (time_after(jiffies, led_jiffies + msecs_to_jiffies(500))) {
                led_state = !led_state;
                gpiod_set_value(ledtec_gpio, led_state);
                led_jiffies = jiffies;
            }
        }

        cpu_relax();
    }

    return 0;
}

/* Probe */
static int msa620_probe(struct platform_device *pdev) {
    int ret, i;

    row_gpios = gpiod_get_array(&pdev->dev, "row", GPIOD_IN);
    if (IS_ERR(row_gpios))
        return PTR_ERR(row_gpios);

    col_gpios = gpiod_get_array(&pdev->dev, "col", GPIOD_OUT_HIGH);
    if (IS_ERR(col_gpios)) {
        ret = PTR_ERR(col_gpios);
        goto err_rows;
    }

    ledtec_gpio = gpiod_get(&pdev->dev, "ledtec", GPIOD_OUT_LOW);
    if (IS_ERR(ledtec_gpio)) {
        ret = PTR_ERR(ledtec_gpio);
        goto err_cols;
    }

    input_dev = input_allocate_device();
    if (!input_dev) {
        ret = -ENOMEM;
        goto err_led;
    }

    input_dev->name = "msa620-keyboard";
    input_dev->phys = "gpio/msa620";
    input_dev->id.bustype = BUS_HOST;
    input_dev->evbit[0] = BIT_MASK(EV_KEY);

    for (i = 0; i < NUM_ROWS; i++) {
        int j;
        for (j = 0; j < NUM_COLS; j++) {
            if (key_map[i][j] != -1)
                set_bit(key_map[i][j], input_dev->keybit);
        }
    }

    ret = input_register_device(input_dev);
    if (ret)
        goto err_input;

    keyboard_task = kthread_run(keyboard_thread_fn, NULL, "msa620_kb_thread");
    if (IS_ERR(keyboard_task)) {
        ret = PTR_ERR(keyboard_task);
        goto err_input_reg;
    }

    dev_info(&pdev->dev, "msa620 keyboard driver loaded\n");
    return 0;

err_input_reg:
    input_unregister_device(input_dev);
    input_dev = NULL;
err_input:
    input_free_device(input_dev);
err_led:
    gpiod_put(ledtec_gpio);
err_cols:
    gpiod_put_array(col_gpios);
err_rows:
    gpiod_put_array(row_gpios);
    return ret;
}

/* Remove */
static void msa620_remove(struct platform_device *pdev) {
    kthread_stop(keyboard_task);
    input_unregister_device(input_dev);
    gpiod_put(ledtec_gpio);
    gpiod_put_array(col_gpios);
    gpiod_put_array(row_gpios);
    dev_info(&pdev->dev, "msa620 keyboard driver unloaded\n");
}

/* Match */
static const struct of_device_id msa620_of_match[] = {
    { .compatible = "keyboard,msa620" },
    { }
};
MODULE_DEVICE_TABLE(of, msa620_of_match);

/* Platform driver */
static struct platform_driver msa620_driver = {
    .probe = msa620_probe,
    .remove = msa620_remove,
    .driver = {
        .name = "msa620_keyboard",
        .of_match_table = msa620_of_match,
    },
};

module_platform_driver(msa620_driver);
