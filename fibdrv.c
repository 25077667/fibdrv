#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <stdbool.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100

#define SIGINIFICANT_BIT(x) (x) >> sizeof((x)) >> 8

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

typedef struct bigN {
    unsigned long long *num;
    unsigned int len;
} BigN;

unsigned int estimateLen(unsigned long long index)
{
    return ((index * 695 / 1000) >> 6) + 1;
}

static BigN *bigN_init(unsigned int _len, bool is_index)
{
    /*If is index needing to estimate*/
    BigN *a = (BigN *) kmalloc(sizeof(BigN), GFP_KERNEL);
    a->len = (is_index) ? estimateLen(_len) : _len;
    a->num = (long long *) kmalloc(sizeof(long long *) * a->len, GFP_KERNEL);
    memset(a->num, 0, sizeof(long long) * a->len);
    return a; /*retrun 1 if alloc success*/
}


/*
 * Test weather a is greather than b.
 */
static int bigN_greather(BigN *a, BigN *b)
{
    int a_l = a->len;
    int b_l = b->len;
    int i = 0;
    if (a_l ^ b_l) /* a->len is diff. with b->len*/
        return a_l > b_l;
    unsigned long long *a_tmp = a->num;
    unsigned long long *b_tmp = b->num;
    while (i < a_l && a_tmp[i] == b_tmp[i]) {
        i++;
    }
    if (i == a_l)
        return 0;
    return a_tmp[i] > b_tmp[i];
}

void binN_resize(BigN *a)
{
    int i;
    for (i = 0; i < a->len && ffs(a->num[i]); i++)
        ;
    if (i < a->len) {
        long long *new_Num =
            (long long *) kmalloc(sizeof(long long) * i, GFP_KERNEL);
        /*Copy origin number to new number buffer*/
        memcpy(new_Num, a->num, sizeof(long long) * i);
        kfree(a->num);
        a->num = new_Num;
        /*Fix the len to new size*/
        a->len = i;
    }
}

static BigN *bigN_add(BigN *a, BigN *b)
{
    BigN *bigger = bigN_greather(a, b) ? a : b;
    BigN *smaller = bigN_greather(a, b) ? b : a;
    BigN *result = bigN_init(bigger->len + 1, false);
    if (result) {
        /*
         * Do add a and b to result.
         * For the block_i which is [i], which is meaning (1<<64)^i
         */
        int i, carry = 0;
        for (i = 0; i < smaller->len; i++) {
            bool largest_bit = SIGINIFICANT_BIT(a->num[i] | b->num[i]);
            result->num[i] = a->num[i] + b->num[i] + carry;
            carry = (~(SIGINIFICANT_BIT(result->num[i]))) && largest_bit;
        }
        if (bigger->len > i) {
            bigger->num[i] += carry;
            memcpy(&(result->num[i]), &(bigger->num[i]),
                   sizeof(long long) * (bigger->len - i));
        }
    }
    binN_resize(result);
    return result;
}

static BigN fibonacci(int k)
{
    // test_bigN();
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    // long long f[k + 2];
    BigN *f[k + 2];

    f[0] = bigN_init(0, true);
    f[1] = bigN_init(1, true);

    f[0]->num[0] = 0;
    f[1]->num[0] = 1;

    for (int i = 2; i <= k; i++) {
        f[i] = bigN_add(f[i - 1], f[i - 2]);
        kfree(f[i - 2]);
    }
    return *f[k];
}
/*
static long long fib_sequence(long long k)
{*/
/* FIXME: use clz/ctz and fast algorithms to speed up */
/*long long f[k + 2];

f[0] = 0;
f[1] = 1;

for (int i = 2; i <= k; i++) {
    f[i] = f[i - 1] + f[i - 2];
}

return f[k];
}*/

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    // return (ssize_t) fib_sequence(*offset);
    BigN result = fibonacci(*offset);
    char *k_buf = (char *) kmalloc(
        2 * result.len * result.len + 7 * result.len,
        GFP_KERNEL); /*Usage increase: (2 * n ^ 2 + 7 * n) is quadratic*/
    int prev_byte = 0;
    for (int i = result.len - 1; i >= 0; i--, prev_byte++) {
        snprintf(k_buf + prev_byte, 8, "%llu", result.num[i]);
        prev_byte += 8;
        for (int j = 0; j < i; j++, prev_byte += 4)
            snprintf(k_buf + prev_byte, 4, "<<64");
        snprintf(k_buf + prev_byte, 1, "+");
    }
    snprintf(k_buf + prev_byte, 1, "\n");
    copy_to_user(buf, k_buf, prev_byte + 1);
    kfree(k_buf);
    return result.num[0];
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    cdev_init(fib_cdev, &fib_fops);
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
