# Table

![在这里插入图片描述](https://img-blog.csdnimg.cn/20210604011230138.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3dlaXhpbl80MjY1MzQwNg==,size_16,color_FFFFFF,t_70#pic_center)

class Table 是一个 sst 文件在内存中的表示，用户通过使用 Table 提供的 Iterator 来读取 sst 文件中存储的 kv 数据。Table 通过不断的读取并解析 sst 文件，来实现 Iterator 所提供的接口。因此打开的 sst 文件的生命周期必须要和 Table 的生命周期一样长。

sstable的读取过程，首先是 seek 到文件末尾读取固定48个字节大小的 footer，这也是为什么[footer是定长的原因](https://izualzhy.cn/leveldb-sstable#33-footer).

然后解析出 meta_index_block 以及 index_block。

通过 meta_index_block 解析出 filter block，通过 index_block 解析出 data_block.

查找时，先通过 filter block 查找是否存在，然后通过 data_block 解析出对应的 value.

# 源码

Table对外提供两个接口：

`Open`是一个 static 函数，用于打开 sstable 文件并且初始化一个 `Table*`对象。
`NewIterator`返回读取该 sstable 的 Iterator，用于读取 sstable.

## Open


## NewIterator

考虑下 sstable 的数据格式，不考虑 filter 的话，查找是一个二层递进的过程：

先查找 index block，查看可能处于哪个 data block，然后查找 data block，找到对应的 value，因此需要两层的 iterator，分别用于 index block && data block。
