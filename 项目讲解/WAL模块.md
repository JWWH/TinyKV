# 日志文件介绍

log文件在LevelDb中的主要作用是系统故障恢复时，能够保证不会丢失数据。因为在将记录写入内存的Memtable之前，会先写入Log文件，这样即使系统发生故障，Memtable中的数据没有来得及Dump到磁盘的SSTable文件，LevelDB也可以根据log文件恢复内存的Memtable数据结构内容，不会造成系统丢失数据，在这点上LevelDb和Bigtable是一致的。下面我们带大家看看log文件的具体物理和逻辑布局是怎样的，LevelDb对于一个log文件，会把它切割成以32K为单位的物理Block，每次读取的单位以一个Block作为基本读取单位，下图展示的log文件由3个Block构成，所以从物理布局来讲，一个log文件就是由连续的32K大小Block构成的。

![](http://i.imgur.com/aNXpY.png)

# 写日志文件相关代码

我相信你可以很快的在levelDB中定位到写记录的函数，也就是levelDB在db_impl.cc文件中的Write函数中写入记录

```cpp
StatusDBImpl::Write(constWriteOptions& options,WriteBatch* my_batch);
```

写入记录的方式是先写入log文件，然后再写入memtable。我们只分析写log文件的情况。写入日志文件的语句如下：

```cpp
status = log_->AddRecord(WriteBatchInternal::Contents(updates));
```

`log_`的定义如下：

```cpp
log::Writer*log_;
```

Writer是一个类，封装了文件的操作，Writer的定义如下：

```cpp
classWriter{
 public:
  // Create a writer that will append data to "*dest".
  // "*dest" must be initially empty.
  // "*dest" must remain live while this Writer is in use.
  explicitWriter(FileWriter* dest);
  ~Writer();

  StatusAddRecord(constSlice& slice);

 private:
  FileWriter* dest_;
  int block_offset_;// Current offset in block

  // crc32c values for all supported record types.  These are
  // pre-computed to reduce the overhead of computing the crc of the
  // record type stored in the header.
  uint32_t type_crc_[kMaxRecordType +1];

  StatusEmitPhysicalRecord(RecordType type,constchar* ptr,size_t length);

  // No copying allowed
  Writer(constWriter&);
  voidoperator=(constWriter&);
};
```

可以看到Writer除了构造函数和析构函数，只有一个公有的成员函数：`AddRecord` ，还有一个私有的辅助成员函数 `EmitPhysicalRecord`, 还有一个关键的成员变量：`FileWriter` ，我们来看看这两个成员函数都做了什么。

## AddRecord

`AddRecord` 函数的定义如下：

```cpp
StatusWriter::AddRecord(constSlice& slice){
  constchar* ptr = slice.data();
  size_t left = slice.size();

  // Fragment the record if necessary and emit it.  Note that if slice
  // is empty, we still want to iterate once to emit a single
  // zero-length record
  Status s;
  boolbegin=true;
  do{
    constint leftover = kBlockSize - block_offset_;
    assert(leftover >=0);
    if(leftover < kHeaderSize){
      // Switch to a new block
      if(leftover >0){
        // Fill the trailer (literal below relies on kHeaderSize being 7)
        assert(kHeaderSize ==7);
        dest_->Append(Slice("\x00\x00\x00\x00\x00\x00", leftover));
      }
      block_offset_ =0;
    }

    // Invariant: we never leave < kHeaderSize bytes in a block.
    assert(kBlockSize - block_offset_ - kHeaderSize >=0);

    constsize_t avail = kBlockSize - block_offset_ - kHeaderSize;
    constsize_t fragment_length =(left < avail)? left : avail;

    RecordType type;
    constboolend=(left == fragment_length);
    if(begin&&end){
      type = kFullType;
    }elseif(begin){
      type = kFirstType;
    }elseif(end){
      type = kLastType;
    }else{
      type = kMiddleType;
    }

    s =EmitPhysicalRecord(type, ptr, fragment_length);
    ptr += fragment_length;
    left -= fragment_length;
    begin=false;
  }while(s.ok()&& left >0);
  return s;
}
```

`AddRecord`成员函数将数据按一定格式存储，然后调用 `EmitPhysicalRecord` 成员函数来完成数据的写入。

## EmitPhysicalRecord

在 `EmitPhysicalRecord`中，首先封装记录头，然后计算校验码，再将数据写入到 `dest_`中。`dest_`是一个 `FileWriter`对象，FileWriter类封装文件的读写操作。

```cpp
StatusWriter::EmitPhysicalRecord(RecordType t,constchar* ptr,size_t n){
  assert(n <=0xffff);// Must fit in two bytes
  assert(block_offset_ + kHeaderSize + n <= kBlockSize);

  // Format the header
  char buf[kHeaderSize];
  buf[4]=static_cast<char>(n &0xff);
  buf[5]=static_cast<char>(n >>8);
  buf[6]=static_cast<char>(t);

  // Compute the crc of the record type and the payload.
  uint32_t crc = crc32c::Extend(type_crc_[t], ptr, n);
  crc = crc32c::Mask(crc);// Adjust for storage
  EncodeFixed32(buf, crc);

  // Write the header and the payload
  Status s = dest_->Append(Slice(buf, kHeaderSize));
  if(s.ok()){
    s = dest_->Append(Slice(ptr, n));
    if(s.ok()){
      s = dest_->Flush();
    }
  }
  block_offset_ += kHeaderSize + n;
  return s;
}
```

`AddRecord`和 `EmitPhysicalRecord`两个函数只负责将log 数据编码成相应的格式，然后调用下面的语句写入数据。

```cpp
Status s = dest_->Append(Slice(buf, kHeaderSize));
```

你肯定和我一样好奇 `dest_`是什么，成员变量 `dest_`的定义如下：

```cpp
FileWriter* dest_;
```

它是一个 `FileWriter`类型，而 `FileWriter` 自己也是一个抽象类。FileWriter的定义如下：

```cpp
// A file abstraction for sequential writing.  The implementation
// must provide buffering since callers may append small fragments
// at a time to the file.
classFileWriter{
 public:
  FileWriter(){}
  virtual~FileWriter();

  virtualStatusAppend(constSlice& data)=0;
  virtualStatusClose()=0;
  virtualStatusFlush()=0;
  virtualStatusSync()=0;

 private:
  // No copying allowed
  FileWriter(constFileWriter&);
  voidoperator=(constFileWriter&);
};
```

所以，我们得看看 `log::Writer` 的构造函数被调用的地方，才能知道 `FileWriter *dest_`中的 `dest_`到底是什么类型。

```
log_ =new log::Writer(lfile);
```

`lfile` 的声明如下：

```
FileWriter* lfile;
```

`lfile` 的定义如下：

```
s = options.env->NewFileWriter(LogFileName(dbname, new_log_number),
                                 &lfile);
```

env_posix.cc文件中 `NewFileWriter` 的定义如下：

```
virtualStatusNewFileWriter(const std::string& fname,
                                 FileWriter** result){
    Status s;
    constint fd = open(fname.c_str(), O_CREAT | O_RDWR | O_TRUNC,0644);
    if(fd <0){
      *result = NULL;
      s =IOError(fname, errno);
    }else{
      *result =newPosixMmapFile(fname, fd, page_size_);
    }
    return s;
  }
```

我们先别管 `PosixMmapFile` 了，好歹我们已经看到了下面这条语句，知道了 `log::Writer` 打开了一个文件，进行写操作。

```
constint fd = open(fname.c_str(), O_CREAT | O_RDWR | O_TRUNC,0644);
```

log::Writer是对日志文件的封装，说到底也只是要写一个文件，我们经历了千辛万苦，终于找到了打开文件的操作，这又将我们引入了levelDB的另一片天地：对可移植性的处理。你可能还在纳闷为什么下面的语句会调用env_posix.cc文件中 `PosixEnv`类的成员函数，那就一起来分析下 `options.env`吧。

```
s = options.env->NewFileWriter(LogFileName(dbname, new_log_number),
                                     &lfile);
```

## Env

levelDB考虑到移植性问题，将系统相关的处理(文件/进程/时间之类)抽象成Env ，用户可以自己实现相应的接口，作为 `Options`传入，默认使用自带的实现。

请仔细阅读上面这段话，然后我们来分析一下Env，Env的定义如下：

```cpp
classEnv{
 public:
  Env(){}
  virtual~Env();

  // Return a default environment suitable for the current operating
  // system.  Sophisticated users may wish to provide their own Env
  // implementation instead of relying on this default environment.
  //
  // The result of Default() belongs to leveldb and must never be deleted.
  staticEnv*Default();

  // Create a brand new sequentially-readable file with the specified name.
  // On success, stores a pointer to the new file in *result and returns OK.
  // On failure stores NULL in *result and returns non-OK.  If the file does
  // not exist, returns a non-OK status.
  //
  // The returned file will only be accessed by one thread at a time.
  virtualStatusNewFileReader(const std::string& fname,
                                   FileReader** result)=0;

  // Create a brand new random access read-only file with the
  // specified name.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores NULL in *result and
  // returns non-OK.  If the file does not exist, returns a non-OK
  // status.
  //
  // The returned file may be concurrently accessed by multiple threads.
  virtualStatusNewRandomAccessFile(const std::string& fname,
                                     RandomAccessFile** result)=0;

  // Create an object that writes to a new file with the specified
  // name.  Deletes any existing file with the same name and creates a
  // new file.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores NULL in *result and
  // returns non-OK.
  //
  // The returned file will only be accessed by one thread at a time.
  virtualStatusNewFileWriter(const std::string& fname,
                                 FileWriter** result)=0;

  // Returns true iff the named file exists.
  virtualboolFileExists(const std::string& fname)=0;

  // Store in *result the names of the children of the specified directory.
  // The names are relative to "dir".
  // Original contents of *results are dropped.
  virtualStatusGetChildren(const std::string& dir,
                             std::vector<std::string>* result)=0;

  // Delete the named file.
  virtualStatusDeleteFile(const std::string& fname)=0;

  // Create the specified directory.
  virtualStatusCreateDir(const std::string& dirname)=0;

  // Delete the specified directory.
  virtualStatusDeleteDir(const std::string& dirname)=0;

  // Store the size of fname in *file_size.
  virtualStatusGetFileSize(const std::string& fname,uint64_t* file_size)=0;

  // Rename file src to target.
  virtualStatusRenameFile(const std::string& src,
                            const std::string& target)=0;

  // Lock the specified file.  Used to prevent concurrent access to
  // the same db by multiple processes.  On failure, stores NULL in
  // *lock and returns non-OK.
  //
  // On success, stores a pointer to the object that represents the
  // acquired lock in *lock and returns OK.  The caller should call
  // UnlockFile(*lock) to release the lock.  If the process exits,
  // the lock will be automatically released.
  //
  // If somebody else already holds the lock, finishes immediately
  // with a failure.  I.e., this call does not wait for existing locks
  // to go away.
  //
  // May create the named file if it does not already exist.
  virtualStatusLockFile(const std::string& fname,FileLock**lock)=0;

  // Release the lock acquired by a previous successful call to LockFile.
  // REQUIRES: lock was returned by a successful LockFile() call
  // REQUIRES: lock has not already been unlocked.
  virtualStatusUnlockFile(FileLock*lock)=0;

  // Arrange to run "(*function)(arg)" once in a background thread.
  //
  // "function" may run in an unspecified thread.  Multiple functions
  // added to the same Env may run concurrently in different threads.
  // I.e., the caller may not assume that background work items are
  // serialized.
  virtualvoidSchedule(
      void(*function)(void* arg),
      void* arg)=0;

  // Start a new thread, invoking "function(arg)" within the new thread.
  // When "function(arg)" returns, the thread will be destroyed.
  virtualvoidStartThread(void(*function)(void* arg),void* arg)=0;

  // *path is set to a temporary directory that can be used for testing. It may
  // or many not have just been created. The directory may or may not differ
  // between runs of the same process, but subsequent calls will return the
  // same directory.
  virtualStatusGetTestDirectory(std::string* path)=0;

  // Create and return a log file for storing informational messages.
  virtualStatusNewLogger(const std::string& fname,Logger** result)=0;

  // Returns the number of micro-seconds since some fixed point in time. Only
  // useful for computing deltas of time.
  virtualuint64_tNowMicros()=0;

  // Sleep/delay the thread for the perscribed number of micro-seconds.
  virtualvoidSleepForMicroseconds(int micros)=0;

 private:
  // No copying allowed
  Env(constEnv&);
  voidoperator=(constEnv&);
};
```

Env是一个抽象类，它只定义了一些接口，为了知道我们使用的Env 是哪个类的对象，我们来看一下levelDB 在什么地方定义Env 的。

## PosixEnv

在option.cc 中可以看到，options 的默认构造函数如下：

```cpp
Options::Options()
    : comparator(BytewiseComparator()),
      create_if_missing(false),
      error_if_exists(false),
      paranoid_checks(false),
      env(Env::Default()),
      info_log(NULL),
      write_buffer_size(4<<20),
      max_open_files(1000),
      block_cache(NULL),
      block_size(4096),
      block_restart_interval(16),
      compression(kSnappyCompression),
      filter_policy(NULL){
}
```

也就是说，我们没有指定Env时，系统会通过 `env(Env::Default())`给Env 赋值，我们再来看看 `Env::Default()`函数的定义：

```cpp
staticpthread_once_t once = PTHREAD_ONCE_INIT;
staticEnv* default_env;
staticvoidInitDefaultEnv(){ default_env =newPosixEnv;}

Env*Env::Default(){
  pthread_once(&once,InitDefaultEnv);
  return default_env;
}
```

上面这几行代码位于env_posix.cc 的最后几行，其实也没做什么，只是返回一个PosixEnv对象，PosixEnv对象是Env 的子类，所以，下面这句话调用的是PosixEnv的成员函数。

```
s = options.env->NewFileWriter(LogFileName(dbname, new_log_number),
	                                     &lfile);
```

## EnvWrapper

在levelDB中还实现了一个EnvWrapper类，该类继承自Env，且只有一个成员函数 `Env* target_`，该类的所有变量都调用Env类相应的成员变量，我们知道，Env是一个抽象类，是不能定义Env 类型的对象的。我们传给EnvWrapper 的构造函数的类型是PosixEnv，所以，最后调用的都是PosixEnv类的成员变量，你可能已经猜到了，这就是设计模式中的代理模式，EnvWrapper只是进行了简单的封装，它的代理了Env的子类PosixEnv。

EnvWrapper和Env与PosixEnv的关系如下：

![](http://i.imgur.com/1WVgtlE.png)

# 读日志

日志读取显然比写入要复杂，要检查 **checksum** ，检查是否有损坏等等，处理各种错误。

https://github.com/balloonwj/CppGuide/blob/master/articles/leveldb%E6%BA%90%E7%A0%81%E5%88%86%E6%9E%90/leveldb%E6%BA%90%E7%A0%81%E5%88%86%E6%9E%906.md

## 类层次

Reader主要用到了两个接口，一个是 **汇报错误的Reporter** ，另一个是log文件 **读取类FileReader** 。

Reporter的接口只有 **一个** ：

```cpp
void Corruption(size_t bytes,const Status& status);
```

FileReader有**两个**接口：

```cpp
Status Read(size_t n, Slice* result, char* scratch);
Status Skip(uint64_t n);
```

说明下，Read接口有一个**result**参数传递结果就行了，为何还有一个scratch呢，这个就和Slice相关了。它的字符串指针是传入的外部char*指针，自己并不负责内存的管理与分配。因此Read接口需要调用者提供一个字符串指针，实际存放字符串的地方。

![](https://github.com/balloonwj/CppGuide/raw/master/articles/imgs/leveldb7.webp)

Reader类有几个成员变量，需要注意：

```cpp
bool eof_;      
// 上次Read()返回长度< kBlockSize，暗示到了文件结尾EOF
uint64_t last_record_offset_;  // 函数ReadRecord返回的上一个record的偏移
uint64_t end_of_buffer_offset_;// 当前的读取偏移
uint64_t const initial_offset_;// 偏移，从哪里开始读取第一条record
Slice   buffer_;               // 读取的内容
```

## 日志读取流程

Reader只有一个接口，那就是ReadRecord，下面来分析下这个函数。

###### S1 根据initial offset跳转到调用者指定的位置，开始读取日志文件。跳转就是直接调用FileReader的Seek接口。

另外，需要先调整调用者传入的**initialoffset**参数，调整和跳转逻辑在SkipToInitialBlock函数中。

```cpp
if (last_record_offset_ <initial_offset_) 
{ // 当前偏移 < 指定的偏移，需要Seek
	if (!SkipToInitialBlock()) return false;
}
```

下面的代码是SkipToInitialBlock函数调整read offset的逻辑：

```cpp
// 计算在block内的偏移位置，并圆整到开始读取block的起始位置
size_t offset_in_block =initial_offset_ % kBlockSize;
uint64_t block_start_location =initial_offset_ - offset_in_block;
// 如果偏移在最后的6byte里，肯定不是一条完整的记录，跳到下一个block
if (offset_in_block >kBlockSize - 6)
{
     offset_in_block = 0;
     block_start_location +=kBlockSize;
}
end_of_buffer_offset_ =block_start_location;        
// 设置读取偏移
if (block_start_location > 0)  file_->Skip(block_start_location); // 跳转
```

首先计算出在**block**内的偏移位置，然后圆整到要读取block的起始位置。开始读取日志的时候都要保证读取的是完整的block，这就是 **调整的目的** 。

同时成员变量end_of_buffer_offset_**记录**了这个值，在后续读取中会用到。

###### S2在开始while循环前首先初始化几个标记：

```cpp
// 当前是否在fragment内，也就是遇到了FIRST 类型的record
bool in_fragmented_record = false;
uint64_t prospective_record_offset = 0; // 我们正在读取的逻辑record的偏移
```

###### S3 进入到while(true)循环，直到读取到KLastType或者KFullType的record，或者到了文件结尾。从日志文件读取完整的record是ReadPhysicalRecord函数完成的。

读取出现错误时，并不会退出循环，而是 **汇报错误** ，继续执行，直到**成功读取**一条user record，或者遇到文件结尾。 

**S3.1 从文件读取record**
