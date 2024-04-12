字符设备
============================

# 数据结构

struct file_operations
struct file
struct inode
struct cdev

# 接口

## cdev

 *  cdev_alloc()
 *  cdev_init()
 *  cdev_add()
 *  cdev_del()

## 设备编号

MAJOR	主设备编号
MINOR	此设备编号

## 分配和释放设备编号

register_chrdev_region
unregister_chrdev_region

alloc_chrdev_region	动态分配设备编号


## 注册

register_chrdev	注册一个字符设备驱动程序
unregister_chrdev 注销

## proc

/proc/devices
Character devices:
  1 mem
  4 /dev/vc/0
  4 tty
  4 ttyS
  5 /dev/tty
  5 /dev/console