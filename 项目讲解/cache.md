大多数磁盘数据库都提供了缓存，因为磁盘和内存的访问速度差了好几个数量级。如果整个数据库的工作集小于内存，那么热数据基本都可以缓存到内存里，这时候数据库表现得就像一个内存数据库，读写效率很高。

最完美的缓存就是将最近将要使用的数据缓存在内存里。然而，未来的访问数据是比较难估算的，一般会采取一些预读的方案将数据预先读取到内存中。而缓存的策略一般都是LRU，也就是根据过去的访问来决定缓存。遵循这样的原则：最近被访问过的数据未来有很大概率再次被访问。

LevelDB提供了一个 `Cache`接口，用户可以实现自己的缓存方式。默认提供了一个LRU Cache，缓存最近使用的数据。

LevelDB的缓存使用在两个地方：

* 缓存SSTable里的Data Block，也就是缓存数据，数据的缓存不是以Kv为单位的，而是以Data Block为最小单位进行缓存，默认情况下会开启一个8MB的LRU Cache来缓存Data Block。考虑到一次扫描可能将所有的内存缓存都刷出去了，LevelDB支持在扫描时，不缓存数据；
* 缓存SSTable在内存中的数据结构 `Table`，一个表在使用前需要先被 `Open`，被 `Open`时会将SSTable的元数据，比如Index Block和布隆过滤器，读取到内存中。缓存 `Table`时是以个数计算的，缓存的个数是 `max_open_files - kNumNonTableCacheFiles`，`kNumNonTableCacheFiles`表示给非SSTable预留的文件描述符数量，为10。

# 缓存的实现

## 缓存接口

缓存有一个接口 `Cache`，每个缓存需要实现这个接口，主要操作包括 `Insert`、`Lookup`和 `Erase`。

```cpp
// include/leveldb/cache.h

class LEVELDB_EXPORT Cache {
    ...

    struct Handle {};

    // 插入一个缓存项
    virtual Handle* Insert(const Slice& key, void* value, size_t charge,
                         void (*deleter)(const Slice& key, void* value)) = 0;

    // 查询一个缓存项
    virtual Handle* Lookup(const Slice& key) = 0;

    // 擦除一个缓存项
    virtual void Erase(const Slice& key) = 0;
    ...
}
```

## 分段锁缓存

![img](https://pic4.zhimg.com/80/v2-52ac53ce5dcd58666e765c4e3ae3a123_1440w.webp)

LevelDB默认的LRU缓存采用了类似于分段锁的设计方式：

* 首先实现了一个 `LRUCache`类，这个类实现了一个可以指定容量的LRU缓存，当达到容量后，会将旧的数据从缓存移除；
* 为了实现线程安全，`LRUCache`在做一些操作时，会进行加锁，但是加锁操作会降低并发度，针对这个问题，LevelDB对外提供的实际是一个 `ShardedLRUCache`缓存；
* `ShardedLRUCache`包含一个 `LRUCache`缓存数组，大小是16，根据缓存键的Hash值的高4位进行哈希，将缓存项分布到不同的 `LRUCache`里，这样当并发操作时，很有可能缓存项不在同一个 `LRUCache`里，不会冲突，大大提高了并发度；
* `ShardedLRUCache`的实现只是简单的将对缓存的操作代理到相应的 `LRUCache`里。

以下是 `Insert`操作的实现，根据hash值计算出对应的 `LRUCache`，然后代理到对应的 `LRUCache`。

```cpp
// util/cache.cc

Handle* ShardedLRUCache::Insert(const Slice& key, void* value, size_t charge,
                 void (*deleter)(const Slice& key, void* value)) override {
    const uint32_t hash = HashSlice(key);  // 计算哈希值
    return shard_[Shard(hash)].Insert(key, hash, value, charge, deleter);
}
```

所以接下来重点讨论 `LRUCache`的实现。

## LRUCache实现

```cpp
// util/cache.cc

class LRUCache {
  size_t capacity_;                        // 缓存容量

  mutable port::Mutex mutex_;              // 包含缓存的锁

  size_t usage_ GUARDED_BY(mutex_);        // 当前使用了多少容量

  LRUHandle lru_ GUARDED_BY(mutex_);       // 缓存项链表

  LRUHandle in_use_ GUARDED_BY(mutex_);    // 当前正在被使用的缓存项链表

  HandleTable table_ GUARDED_BY(mutex_);   // 缓存的哈希表，快速查找缓存项
}

```

`LRUCache`的实现有以下特点：

* 每一个缓存项都保存在一个 `LRUHandler`里；
* 每一个 `LRUHandler`首先被保存在一个哈希表 `table_`里面，支持根据键快速的查找;
* `LRUCache`里面有两个双向链表 `lru_`和 `in_use_`，每一个 `LRUHandler`可以在两个链表中的一个里，但是不会同时在两个里，也有可能有些 `LRUHandler`被淘汰出缓存了，不在任何链表上；
* `in_use_`保存当前正在被引用的 `LRUHandler`，这个链表主要是为了检查；
* `lru_`保存没有被使用的 `LRUHandler`，按照访问顺序来保存，`lru_.next`保存最旧的，`lru_.prev`保存最新的，需要淘汰缓存时，会从 `lru_`里的 `next`开始淘汰；
* 当一个 `LRUHandler`被使用时，会从 `lru_`移动到 `in_use_`，使用完成后，会从 `in_use_`重新移动到 `lru_`里；
* 每个 `LRUCache`都有一个容量 `capacity_`，表示这个缓存的大小，每次插入一个项时都会指定这个缓存项的大小，更新 `usage_`字段，当 `usage_`超过 `capacity_`时，就淘汰最旧的缓存项，直到低于 `capacity_`。

![](https://pic4.zhimg.com/80/v2-958e9878a2847dd49a4a7b38b5ed6f5b_1440w.webp)

以下是 `LRUHandler`的定义：

```cpp
// util/cache.cc

struct LRUHandle {
    void* value;                                 // 值
    void (*deleter)(const Slice&, void* value);  // 数据项被移出缓存时的回调函数
    LRUHandle* next_hash;                        // 哈希表的链接
    LRUHandle* next;                             // 两个双向链表的链接
    LRUHandle* prev;
    size_t charge;                               // 缓存项的大小
    size_t key_length;                           // 键的长度
    bool in_cache;                               // 当前项是否在缓存中
    uint32_t refs;                               // 当前项的引用计数
    uint32_t hash;                               // 哈希值
    char key_data[1];                            // 键值

    Slice key() const {
        return Slice(key_data, key_length);
    }
};
```

LRUCache通过引用计数来管理 `LRUHandler`。

```cpp
// util/cache.cc

void LRUCache::Ref(LRUHandle* e) {
    if (e->refs == 1 && e->in_cache) {  // 如果当前在lru_里，移动到in_use_里
        LRU_Remove(e);                  // 先从链表中移除
        LRU_Append(&in_use_, e);        // 插入到in_use_
    }
    e->refs++;
}

void LRUCache::Unref(LRUHandle* e) {
    e->refs--;
    if (e->refs == 0) {  // 销毁缓存项
        (*e->deleter)(e->key(), e->value);
        free(e);
    } else if (e->in_cache && e->refs == 1) {
        // 重新移动到lru_里
        LRU_Remove(e);
        LRU_Append(&lru_, e);
    }
}
```

通过引用计数，LRUCache有以下特点：

* 当一个 `LRUHandler`被加入到缓存里面，并且没有被使用时，计数为1；
* 如果客户端需要访问一个缓存，就会找到这个 `LRUHandler`，调用 `Ref`，将计数加1，并且当此时缓存在 `lru_`里，就移动到 `in_use`里；
* 当客户端使用完一个缓存时，调用 `Unref`里，将计数减1，当计数为0时，调用回调函数销毁缓存，当计数为1时，移动到 `in_use`里面；
* 这样可以自动控制缓存的销毁，当一个 `LRUHandler`被移出缓存时，如果还有其他的引用，也不会被销毁。

所以查找一个缓存就非常简单了:

```cpp
// util/cache.cc

Cache::Handle* LRUCache::Lookup(const Slice& key, uint32_t hash) {
    MutexLock l(&mutex_);                    // 加锁操作，使用分段缓存减少锁等待
    LRUHandle* e = table_.Lookup(key, hash);
    if (e != nullptr) {
        Ref(e);
    }
    return reinterpret_cast<Cache::Handle*>(e);
}

void LRUCache::Release(Cache::Handle* handle) {
    MutexLock l(&mutex_);
    Unref(reinterpret_cast<LRUHandle*>(handle));
}
```

* 通过哈希表查找对应的 `LRUHandler`；
* 如果找到了，调用 `Ref`，返回缓存项；
* 使用完缓存项后，调用 `Release`释放缓存。

插入缓存需要将缓存项插入到哈希表以及链表中，并且更新容量，如果缓存容量过多，需要淘汰旧缓存。插入一个缓存项的步骤如下：

* 生成一个 `LRUHandler`保存缓存的内容，计数为1；
* 再将计数加1，表示当前缓存项被当前客户端引用，插入到 `in_use_`链表中；
* 插入时会指定插入项的大小更新 `usage_`字段；
* 插入到哈希表中；
* 如果有相同值旧的缓存项，释放旧项；
* 判断容量是否超标，如果超标，释放最旧的缓存项，直到容量不超标为止。

# 缓存使用

LevelDB里SSTable在内存中是以 `Table`结构存在的，要使用一个SSTable，必须先进行 `Open`操作，会将Index Block和Filter Data都读取到内存里，保存在 `Table`里，但是Data Block依然保存在磁盘上。需要读取数据时，可以将数据放到缓存中，下次再次访问数据时，就可以从缓存里读取。所以缓存有两方面：

* 每个 `Table`结构都要占据一定的内存，被打开的 `Table`放在一个缓存中，缓存一定数量的 `Table`，当数量太多时，有一些 `Table`需要被驱逐出内存，这样当需要再次访问这些 `Table`时需要再次被打开；
* 每个 `Table`的Data Block可以被缓存，这样再次访问相同的数据时，不需要读磁盘。

## Table缓存

SSTable的文件名类似于000005.ldb，前缀部分就是一个 `file_number`，`Table`就是用这个 `file_number`作为键来缓存的。`Table`的缓存存储在 `TableCache`类里面。

```cpp
// db/table_cache.cc

Status TableCache::FindTable(uint64_t file_number, uint64_t file_size,
                             Cache::Handle** handle) {
    Status s;
    char buf[sizeof(file_number)];
    EncodeFixed64(buf, file_number);
    Slice key(buf, sizeof(buf));     // key为file_number
    *handle = cache_->Lookup(key);   // cache_是LRUCache的实例
    if (*handle == nullptr) {        // 如果缓存没命中，则打开新的Table
        ...
        s = Table::Open(options_, file, file_size, &table);
        TableAndFile* tf = new TableAndFile;
        tf->file = file;
        tf->table = table;
        // 插入一个缓存项，大小为1
        *handle = cache_->Insert(key, tf, 1, &DeleteEntry);
    }
    return s;
}
```

查询一个 `Table`时步骤如下:

* 先从缓存里面找，键是 `file_number`，如果找到了，就可以直接返回 `Table`；
* 如果没有找到，需要 `Open`这个SSTable，然后插入到缓存里面；
* 缓存的 `capacity_`大小为支持打开的 `Table`的个数，而每一个缓存项大小为1，这样当缓存的 `Table`个数大于容量时，就会将最旧的 `Table`淘汰。

## Data Block缓存

每个 `Table`打开的时候，都会指定一个 `cache_id`，这是一个单调递增的整数，每个 `Table`都有一个唯一的 `cache_id`。在每一个SSTable里面，每一个Data Block都有一个固定的文件偏移 `offset`。所以每一个Data Block都可以由 `cache_id`和 `offset`来唯一标识，也就是根据这两个值生成一个键，来插入和查找缓存。

```cpp
// table/table.cc

// 根据一个Index读取一个Data Block

Iterator* Table::BlockReader(void* arg, const ReadOptions& options,
                             const Slice& index_value) {
    Table* table = reinterpret_cast<Table*>(arg);
    Cache* block_cache = table->rep_->options.block_cache;
    Block* block = nullptr;
    Cache::Handle* cache_handle = nullptr;

    BlockHandle handle;                   // 保存索引项
    Slice input = index_value;
    Status s = handle.DecodeFrom(&input);

    if (s.ok()) {
        BlockContents contents;
        // 使用缓存，则先读缓存
        if (block_cache != nullptr) {
            // 构造缓存键，使用cache_id和offset
            char cache_key_buffer[16];
            EncodeFixed64(cache_key_buffer, table->rep_->cache_id);
            EncodeFixed64(cache_key_buffer + 8, handle.offset());
            Slice key(cache_key_buffer, sizeof(cache_key_buffer));
            // 查找缓存是否存在
            cache_handle = block_cache->Lookup(key);
            // 存在则直接获取到block
            if (cache_handle != nullptr) {
                block = reinterpret_cast<Block*>(block_cache->Value(cache_handle));
            } else {
                // 否则从文件里读取Data Block
                s = ReadBlock(table->rep_->file, options, handle, &contents);
                if (s.ok()) {
                    block = new Block(contents);
                    if (contents.cachable && options.fill_cache) {
                        // 插入缓存
                        cache_handle = block_cache->Insert(key, block, block->size(),
                                               &DeleteCachedBlock);
                    }
                }
            }
        } else {
            // 不使用缓存，直接读取数据
            s = ReadBlock(table->rep_->file, options, handle, &contents);
            if (s.ok()) {
                block = new Block(contents);
            }
        }
    }
  ...
}
```

当要获取一个Data Block时：

* 从这个Data Block的索引项出发，根据索引得到 `offset`，然后根据 `Table`得到 `cache_id`，这样就得到了缓存键；
* 在缓存里读取Data Block，如果存在就可以返回；
* 否则从文件里读取Data Block，这里根据选项 `fill_cache`，可以决定是否插入到缓存。

# 小结

以上便是LevelDB里面缓存的实现，对于磁盘型的数据库，缓存是非常重要的，如果内存足够大，大到足以容纳所有数据，那么数据库的读效率就像内存数据库一样。除了数据部分，索引和元数据LevelDB一般是缓存在内存里面的，基于SSTable的结构和存储，这些数据都不会改变，只读不写。只有Compaction时，才会变化，但是是生成新文件，而不是写旧数据，所以也不会有缓存更新过期的问题。
