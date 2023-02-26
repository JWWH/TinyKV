#pragma once

#include "offset_info.h"
#include "../include/tinykv/status.h"
#include "../file/file_reader.h"
#include "../db/options.h"

#include <string>

namespace tinykv {
DBStatus ReadBlock(const FileReader* file, const ReadOptions& options, const OffSetInfo& offset_info, std::string& buf);
}