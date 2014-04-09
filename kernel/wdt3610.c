/*
 * Watchdog driver for MicroPC EES-3610.
 * Copyright (C) 2006 Yuuki Harano
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 * This driver is based on advantechwdt.c, Advantech WDT driver in linux-2.6.5.
 * 
 * The data sheet is EES-3610/Watchdog/watchdog.txt in the EVALUE SUPPORTING CD-ROM of the PC.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#define WATCHDOG_NAME "EES-3610 WDT"
#define PFX WATCHDOG_NAME ": "
#define WATCHDOG_TIMEOUT 60		/* 60 sec default timeout */

static unsigned long wdt_is_open;
static char wdt_expect_close;

/*
 *	You must set these - there is no sane way to probe for this board.
 *
 *	To enable or restart, write the timeout value in seconds (1 to 255)
 *	to I/O port wdt_port.  To disable, write 0 as the timeout value.
 */

static int wdt_port2 = 0x444;
module_param(wdt_port2, int, 0);
MODULE_PARM_DESC(wdt_port2, "EES-3610 WDT mode io port (default 0x444)");

static int wdt_port = 0x443;
module_param(wdt_port, int, 0);
MODULE_PARM_DESC(wdt_port, "EES-3610 WDT heartbeat io port (default 0x443)");

static int timeout = WATCHDOG_TIMEOUT;	/* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds. 1<= timeout <=255, default=" __MODULE_STRING(WATCHDOG_TIMEOUT) ".");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

/*
 *	Kernel methods.
 */

static void
wdt_ping(void)
{
	/* Write a watchdog value */
	outb_p(timeout, wdt_port);
}

static void
wdt_disable(void)
{
	outb_p(0, wdt_port);
}

static ssize_t
wdt_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if (count) {
		if (!nowayout) {
			size_t i;

			wdt_expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf+i))
					return -EFAULT;
				if (c == 'V')
					wdt_expect_close = 42;
			}
		}
		wdt_ping();
	}
	return count;
}

static int
wdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	  unsigned long arg)
{
	int new_timeout;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "EES-3610 WDT",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
	  if (copy_to_user(argp, &ident, sizeof(ident)))
	    return -EFAULT;
	  break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
	  return put_user(0, p);

	case WDIOC_KEEPALIVE:
	  wdt_ping();
	  break;

	case WDIOC_SETTIMEOUT:
	  if (get_user(new_timeout, p))
		  return -EFAULT;
	  if ((new_timeout < 1) || (new_timeout > 255))
		  return -EINVAL;
	  timeout = new_timeout;
	  wdt_ping();
	  /* Fall */

	case WDIOC_GETTIMEOUT:
	  return put_user(timeout, p);

	case WDIOC_SETOPTIONS:
	{
	  int options, retval = -EINVAL;

	  if (get_user(options, p))
	    return -EFAULT;

	  if (options & WDIOS_DISABLECARD) {
	    wdt_disable();
	    retval = 0;
	  }

	  if (options & WDIOS_ENABLECARD) {
	    wdt_ping();
	    retval = 0;
	  }

	  return retval;
	}

	default:
	  return -ENOIOCTLCMD;
	}
	return 0;
}

static int
wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;
	/*
	 *	Activate
	 */

	wdt_ping();
	return nonseekable_open(inode, file);
}

static int
wdt_close(struct inode *inode, struct file *file)
{
	if (wdt_expect_close == 42) {
		wdt_disable();
	} else {
		printk(KERN_CRIT PFX "Unexpected close, not stopping watchdog!\n");
		wdt_ping();
	}
	clear_bit(0, &wdt_is_open);
	wdt_expect_close = 0;
	return 0;
}

/*
 *	Notifier for system down
 */

static int
wdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/* Turn the WDT off */
		wdt_disable();
	}
	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static struct file_operations wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= wdt_write,
	.ioctl		= wdt_ioctl,
	.open		= wdt_open,
	.release	= wdt_close,
};

static struct miscdevice wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &wdt_fops,
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block wdt_notifier = {
	.notifier_call = wdt_notify_sys,
};

static int __init
wdt3610_init(void)
{
	int ret;

	printk(KERN_INFO "WDT driver for EES-3610 initialising.\n");

	if (timeout < 1 || timeout > 255) {
		timeout = WATCHDOG_TIMEOUT;
		printk (KERN_INFO PFX "timeout value must be 1<=x<=255, using %d\n",
			timeout);
	}

	if (wdt_port2 != wdt_port) {
		if (!request_region(wdt_port2, 1, WATCHDOG_NAME)) {
			printk (KERN_ERR PFX "I/O address 0x%04x already in use\n",
				wdt_port2);
			ret = -EIO;
			goto out;
		}
	}

	if (!request_region(wdt_port, 1, WATCHDOG_NAME)) {
		printk (KERN_ERR PFX "I/O address 0x%04x already in use\n",
			wdt_port);
		ret = -EIO;
		goto unreg_stop;
	}

	ret = register_reboot_notifier(&wdt_notifier);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			ret);
		goto unreg_regions;
	}

	ret = misc_register(&wdt_miscdev);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		goto unreg_reboot;
	}

	printk (KERN_INFO PFX "initialized. timeout=%d sec (nowayout=%d)\n",
		timeout, nowayout);

out:
	return ret;
unreg_reboot:
	unregister_reboot_notifier(&wdt_notifier);
unreg_regions:
	release_region(wdt_port, 1);
unreg_stop:
	if (wdt_port2 != wdt_port)
		release_region(wdt_port2, 1);
	goto out;
}

static void __exit
wdt3610_exit(void)
{
	misc_deregister(&wdt_miscdev);
	unregister_reboot_notifier(&wdt_notifier);
	if(wdt_port2 != wdt_port)
		release_region(wdt_port2,1);
	release_region(wdt_port,1);
}

module_init(wdt3610_init);
module_exit(wdt3610_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yuuki Harano <masm@flowernet.gr.jp>");
MODULE_DESCRIPTION("EES-3610 WDT driver");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
