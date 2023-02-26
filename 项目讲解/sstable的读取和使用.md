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

```cpp
Iterator* Table::NewIterator(const ReadOptions& options) const {
  return NewTwoLevelIterator(
      //传入index_block的iterator
      rep_->index_block->NewIterator(rep_->options.comparator),
      &Table::BlockReader, const_cast<Table*>(this), options);
}
```

第一个参数传入 index block 的 iterator，用于第一层查找。查找到的 value 会传递给第二个参数(函数指针)，该函数支持解析 value 的 data block，第三、四个参数都在函数调用时使用。

`NewTwoLevelIterator`实际上是返回 `TwoLevelIterator`

```cpp
Iterator* NewTwoLevelIterator(
    Iterator* index_iter,
    BlockFunction block_function,
    void* arg,
    const ReadOptions& options) {
  return new TwoLevelIterator(index_iter, block_function, arg, options);
}
```

`TwoLevelIterator`继承自 `Iterator`，实现了 `Seek/Prev/Next/key/value`等一系列接口。

### TwoLevelIterator

二级迭代器的存在便于对SSTable的DataBlock数据进行访问，其结构如下：

![在这里插入图片描述](https://img-blog.csdnimg.cn/20200614175126109.png#pic_center?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L0g1MTQ0MzQ0ODU=,size_16,color_FFFFFF,t_70)

对于SSTable来说：

* Level_1是Index Block迭代器；
* Level_2是指向DataBlock迭代器。

通过这种设计，可以对SSTable的所有key进行向前扫描，向后扫描这种批量查询工作。

正如其名，由两个 iter 组成，分别指向 index block，以及某个 data block.（注：`IteratorWrapper`封装了 `Iterator`，可以先简单认为是等价的。）

```cpp
 IteratorWrapper index_iter_;
 IteratorWrapper data_iter_; // May be nullptr
```

举一个 `Seek`的例子：

```cpp
void TwoLevelIterator::Seek(const Slice& target) {
  // 先在 index block 找到第一个>= target 的k:v, v是某个data_block的size&offset
  index_iter_.Seek(target);
  // 根据v读取data_block，data_iter_指向该data_block内的k:v
  InitDataBlock();
  if (data_iter_.iter() != nullptr) data_iter_.Seek(target);
  SkipEmptyDataBlocksForward();
}
```

先通过 `index_iter_`找到第一个 >= target 的 key, 对应的 value 是某个 data block 的 size&offset，接下来继续在该 data block 内查找。

为什么可以直接在这个 data block 内查找？我们看下原因。

首先，`(index_iter_ - 1).key() < target`，而 `index_iter_ - 1`对应的 data block 内所有的 key 都满足 `<span> </span><= (index_iter_ - 1)->key()`，因此该 data block^1^ 内所有的 key 都满足 `< target`.

其次，`index_iter_.key() >= target`，而 `index_iter_`对应的 data block^2^ 内所有的 key 都满足 `<span> </span><= index_iter_->key()`。

同理，`index_iter + 1`对应的 data block^3^ 内所有的 key 都满足 ` > (index_iter_ + 1)->key()`.

而 data block^1^^2^^3^是连续的。

因此，如果 target 存在于该 sstable，那么一定存在于 `index_iter_`当前指向的 data block.

注：

1. 如果 `index_iter_`指向第一条记录，那么 `index_iter - 1`无效，但不影响该条件成立。
2. 关于 `index_iter_`的 key 的取值，参考[leveldb-sstable r->last_key]的介绍

之后就是调用 `InitDataBlock`初始化，使得 `data_iter_`指向该 data block，其中就用到了传入的 `block_function`，注意调用时的第三个参数即 `index_iter_.value()`.

```cpp
void TwoLevelIterator::InitDataBlock() {
  if (!index_iter_.Valid()) {
    SetDataIterator(nullptr);
  } else {
    Slice handle = index_iter_.value();
    if (data_iter_.iter() != nullptr && handle.compare(data_block_handle_) == 0) {
      // data_iter_ is already constructed with this iterator, so
      // no need to change anything
    } else {
      // 返回该data block对应的iterator
      Iterator* iter = (*block_function_)(arg_, options_, handle);
      // 记录block的size&offset
      data_block_handle_.assign(handle.data(), handle.size());
      SetDataIterator(iter);
    }
  }
}
```

接着使用 `data_iter->Seek`就定位到实际的 {key:value} 了。
