#include "table.h"

namespace tinykv {
Table::Table(const Options* options, const FileReader* file_reader)
	: options_(options)
	, file_reader_(file_reader)
{}
DBStatus Open(uint64_t file_size);


}