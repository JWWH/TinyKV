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
	Table(const Options* options, const FileReader* file_reader);
	DBStatus Open(uint64_t file_size);
	DBStatus ReadBlock(const OffSetInfo&, std::string&);
	void ReadMeta(const FooterBuilder* footer);
	void ReadFilter(const std::string& filter_handle_value);
	Iterator* NewIterator(const ReadOptions&) const;
	Iterator* BlockReader(const ReadOptions&, const std::string_view&);
private:
	const Options* options_;
	const FileReader* file_reader_;
	uint64_t table_id_ = 0;
	std::string bf_;
	// index_block对象，用于两层迭代器使用
	std::unique_ptr<DataBlock> index_block_;
};
}