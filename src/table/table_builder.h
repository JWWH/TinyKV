#pragma once

#include "../file/file_reader.h"
#include "../file/file_writer.h"
#include "offset_info.h"

namespace tinykv {

struct Options;

class TableBuilder final {
public:

private:
	Options options_;	// 构造TableBuilder需要的元数据
	Options index_options_;	// 

};

}