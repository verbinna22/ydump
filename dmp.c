#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/dax.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>
#include <linux/sysfs.h>

#define DM_MSG_PREFIX "dmp"

struct device_stat {
    uint r_qnum;
    uint w_qnum;
    ullong r_sum_size;
    ullong w_sum_size;
	bool is_init;
	struct mutex m;
};

#define GET_AVG(sum64, num32) (((num32) == 0) ? 0 : (((sum64) / (num32)) << SECTOR_SHIFT) + ((((sum64) % (num32)) << SECTOR_SHIFT) / (num32)))

static ssize_t device_attr_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
    struct device_stat *st;
    st = dev_get_drvdata(dev);
	mutex_lock(&st->m);
    int ret = sprintf(buf,
		"Output:\n"
		"\tread:\n"
		"\t\treqs:%lld\n"
		"\t\tavg size:%lld\n"
		"\twrite:\n"
		"\t\treqs:%lld\n"
		"\t\tavg size:%lld\n"
		"\ttotal:\n"
		"\t\treqs:%lld\n"
		"\t\tavg size:%lld\n",
		st->r_qnum,
		GET_AVG(st->r_sum_size, st->r_qnum),
		st->w_qnum,
		GET_AVG(st->w_sum_size, st->w_qnum),
		st->r_qnum + st->w_qnum,
		GET_AVG(st->r_sum_size + st->w_sum_size, st->w_qnum + st->r_qnum)
	); // TODO: buf len
	mutex_unlock(&st->m);
	return ret;
}

static DEVICE_ATTR_RO(device_attr);

struct dmp_c {
	struct dm_dev *dev;
};

#define assert(...) WARN_ON(!(__VA_ARGS__))

static void dmp_read(struct device_stat *d, ullong size)
{
    assert(size % SECTOR_SIZE == 0);
	mutex_lock(&d->m);
	d->r_qnum++;
	d->r_sum_size += (size >> SECTOR_SHIFT);
	mutex_unlock(&d->m);
}

static void dmp_write(struct device_stat *d, ullong size)
{
    assert(size % SECTOR_SIZE == 0);
	mutex_lock(&d->m);
	d->w_qnum++;
	d->w_sum_size += (size >> SECTOR_SHIFT);
	mutex_unlock(&d->m);
}

static void remove_file(void *f) {
    device_remove_file((struct device *)f, &dev_attr_device_attr);
}

static int dmp_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct dmp_c *lc;
	int ret;
	if (argc != 1) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}
    struct mapped_device *md = dm_table_get_md(ti->table); // TODO
    struct device *dev = disk_to_dev(dm_disk(md));
	struct device_stat *st;
	st = dev_get_drvdata(dev); // TODO
    if (!st) {
        st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
        if (!st) {
            ti->error = "Cannot allocate device context";
			return -ENOMEM;
        }
		mutex_init(&st->m);
    }
    lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	if (lc == NULL) {
		ti->error = "Cannot allocate dmp context";
		return -ENOMEM;
	}
    ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &lc->dev);
	if (ret) {
		ti->error = "Device lookup failed";
		goto bad;
	}
	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->num_secure_erase_bios = 1;
	ti->num_write_zeroes_bios = 1;
	ti->flush_bypasses_map = true;
	ti->private = lc;
	return 0;
bad:
	kfree(lc);
	return ret;
}

static void dmp_dtr(struct dm_target *ti)
{
	struct dmp_c *lc = ti->private;
	dm_put_device(ti, lc->dev);
    kfree(lc);
}

static int dmp_map(struct dm_target *ti, struct bio *bio)
{
	struct dmp_c *lc = ti->private;
    struct mapped_device *md = dm_table_get_md(ti->table); // TODO
    struct device *dev = disk_to_dev(dm_disk(md));
	pr_info("map: %llx %llx %llx\n", &dev->kobj, dev->kobj.sd, &dev_attr_device_attr);
    switch (bio_op(bio)) {
	case REQ_OP_READ:
		if (!(bio->bi_opf & REQ_RAHEAD) && !(bio->bi_opf & REQ_META)) {
			if (strstr(current->comm, "udev") == NULL) {
				pr_info("r %s", current->comm);
				dmp_read(dev_get_drvdata(dev), bio->bi_iter.bi_size);
			}
		}
		break;
    case REQ_OP_ZONE_APPEND:
		pr_info("1");
    case REQ_OP_WRITE_ZEROES:
		pr_info("2");
	case REQ_OP_WRITE:
		pr_info("3");
		dmp_write(dev_get_drvdata(dev), bio->bi_iter.bi_size);
		break;
    default:
	}
	pr_info("\n");
	bio_set_dev(bio, lc->dev->bdev);
	return DM_MAPIO_REMAPPED;
}

// static void dmp_status(struct dm_target *ti, status_type_t type,
// 			  unsigned int status_flags, char *result, unsigned int maxlen)
// {
// 	struct dmp_c *lc = ti->private;
// 	size_t sz = 0;

// 	switch (type) {
// 	case STATUSTYPE_INFO:
// 		result[0] = '\0';
// 		break;

// 	case STATUSTYPE_TABLE:
// 		DMEMIT("%s %llu", lc->dev->name, (unsigned long long)lc->start);
// 		break;

// 	case STATUSTYPE_IMA:
// 		DMEMIT_TARGET_NAME_VERSION(ti->type);
// 		DMEMIT(",device_name=%s,start=%llu;", lc->dev->name,
// 		       (unsigned long long)lc->start);
// 		break;
// 	}
// }

// static int dmp_prepare_ioctl(struct dm_target *ti, struct block_device **bdev,
// 				unsigned int cmd, unsigned long arg,
// 				bool *forward)
// {
// 	struct dmp_c *lc = ti->private;
// 	struct dm_dev *dev = lc->dev;

// 	*bdev = dev->bdev;

// 	/*
// 	 * Only pass ioctls through if the device sizes match exactly.
// 	 */
// 	if (lc->start || ti->len != bdev_nr_sectors(dev->bdev))
// 		return 1;
// 	return 0;
// }

// #ifdef CONFIG_BLK_DEV_ZONED
// static int dmp_report_zones(struct dm_target *ti,
// 		struct dm_report_zones_args *args, unsigned int nr_zones)
// {
// 	struct dmp_c *lc = ti->private;

// 	return dm_report_zones(lc->dev->bdev, lc->start,
// 			       dmp_map_sector(ti, args->next_sector),
// 			       args, nr_zones);
// }
// #else
// #define dmp_report_zones NULL
// #endif

// static int dmp_iterate_devices(struct dm_target *ti,
// 				  iterate_devices_callout_fn fn, void *data)
// {
// 	struct dmp_c *lc = ti->private;

// 	return fn(ti, lc->dev, lc->start, ti->len, data);
// }

// #if IS_ENABLED(CONFIG_FS_DAX)
// static struct dax_device *dmp_dax_pgoff(struct dm_target *ti, pgoff_t *pgoff)
// {
// 	struct dmp_c *lc = ti->private;
// 	sector_t sector = dmp_map_sector(ti, *pgoff << PAGE_SECTORS_SHIFT);

// 	*pgoff = (get_start_sect(lc->dev->bdev) + sector) >> PAGE_SECTORS_SHIFT;
// 	return lc->dev->dax_dev;
// }

// static long dmp_dax_direct_access(struct dm_target *ti, pgoff_t pgoff,
// 		long nr_pages, enum dax_access_mode mode, void **kaddr,
// 		unsigned long *pfn)
// {
// 	struct dax_device *dax_dev = dmp_dax_pgoff(ti, &pgoff);

// 	return dax_direct_access(dax_dev, pgoff, nr_pages, mode, kaddr, pfn);
// }

// static int dmp_dax_zero_page_range(struct dm_target *ti, pgoff_t pgoff,
// 				      size_t nr_pages)
// {
// 	struct dax_device *dax_dev = dmp_dax_pgoff(ti, &pgoff);

// 	return dax_zero_page_range(dax_dev, pgoff, nr_pages);
// }

// static size_t dmp_dax_recovery_write(struct dm_target *ti, pgoff_t pgoff,
// 		void *addr, size_t bytes, struct iov_iter *i)
// {
// 	struct dax_device *dax_dev = dmp_dax_pgoff(ti, &pgoff);

// 	return dax_recovery_write(dax_dev, pgoff, addr, bytes, i);
// }

// #else
// #define dmp_dax_direct_access NULL
// #define dmp_dax_zero_page_range NULL
// #define dmp_dax_recovery_write NULL
// #endif

void dmp_resume (struct dm_target *ti)
{
	struct mapped_device *md = dm_table_get_md(ti->table); // TODO
    struct device *dev = disk_to_dev(dm_disk(md)); // TODO:
	struct device_stat *st;
	st = dev_get_drvdata(dev); // TODO
    if (!st->is_init) {
		mutex_lock(&st->m);
		if (!st->is_init) {
			st->is_init = true;
			dev_set_drvdata(dev, st);
			pr_info("resume %llx %llx %llx\n", &dev->kobj, dev->kobj.sd, &dev_attr_device_attr);
			device_create_file(dev, &dev_attr_device_attr); // TODO: check
			devm_add_action_or_reset(dev, remove_file, dev); // TODO: check
		}
		mutex_unlock(&st->m);
	}
}

static struct target_type dmp_target = {
	.name   = "dmp",
	.version = {1, 5, 0},
	.features = DM_TARGET_PASSES_INTEGRITY | DM_TARGET_NOWAIT |
		    DM_TARGET_ZONED_HM | DM_TARGET_PASSES_CRYPTO |
		    DM_TARGET_ATOMIC_WRITES,
	// .report_zones = dmp_report_zones,
	.module = THIS_MODULE,
	.ctr    = dmp_ctr,
	.dtr    = dmp_dtr,
	.map    = dmp_map,
	.resume = dmp_resume,
	// .status = dmp_status,
	// .prepare_ioctl = dmp_prepare_ioctl,
	// .iterate_devices = dmp_iterate_devices,
	// .direct_access = dmp_dax_direct_access,
	// .dax_zero_page_range = dmp_dax_zero_page_range,
	// .dax_recovery_write = dmp_dax_recovery_write,
};

MODULE_AUTHOR("Nikita Verbin");
MODULE_DESCRIPTION(DM_NAME " store and show statistics");
MODULE_LICENSE("GPL");

static int __init dmp_init(void)
{
    int r = dm_register_target(&dmp_target);
    if (r < 0) {
        DMERR("registration failed");
    } else {
        DMINFO("module loaded");
    }
    return r;
}

static void __exit dmp_exit(void)
{
    dm_unregister_target(&dmp_target);
    DMINFO("module unloaded");
}

module_init(dmp_init);
module_exit(dmp_exit);
