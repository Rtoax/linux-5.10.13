块设备
========================

# 数据结构

struct gendisk:	表示一个磁盘设备或分区
struct struct request_queue: 请求队列

# 接口

## gendisk操作

alloc_disk: 分配gendisk
del_gendisk: 删除gendisk
add_disk: 增加gendisk

## 块设备注册

register_blkdev: 注册块设备驱动
unregister_blkdev: 注销块设备驱动

## 请求队列

blk_init_queue: 初始化请求队列
blk_cleanup_queue: 清除请求队列
blk_alloc_queue: 分配请求队列
blk_queue_make_request: 绑定制造请求函数
blk_peek_request: 提取请求
blk_start_request: 启动请求
blk_end_request_all: 报告完成
__blk_end_request_all: 在持有队列锁的场景下调用
