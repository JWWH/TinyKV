#pragma once
#include <memory>
#include <string>

#include "../db/options.h"
#include "../include/tinykv/iterator.h"
#include "../file/file_reader.h"
#include "../include/tinykv/status.h"
#include "offset_info.h"
#include "footer_builder.h"
#include "data_block.h" 

namespace tinykv {
class Table final {
public:
	static DBStatus Open(const Options& options, FileReader* file, uint64_t file_size, Table** table);

	Table(const Table&) = delete;
	Table& operator=(const Table&) = delete;

	~Table();

	Iterator* NewIterator(const ReadOptions&) const;
	
private:
	Table(const Options* options, const FileReader* file_reader);
	//DBStatus ReadBlock(const OffSetInfo&, std::string&);
	void ReadMeta(const FooterBuilder* footer);
	void ReadFilter(const std::string& filter_handle_value);
	static Iterator* BlockReader(void*, const ReadOptions&, const std::string&);
	const Options* options_;
	const FileReader* file_reader_;
	uint64_t cache_id_ = 0;
	std::string bf_;
	// index_block对象，用于两层迭代器使用
	std::unique_ptr<DataBlock> index_block_;
};
}