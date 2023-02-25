#include "filter_block_builder.h"
#include "../utils/codec.h"

namespace tinykv {
FilterBlockBuilder::FilterBlockBuilder(const Options& options){
	if(options.filter_policy) {
		policy_filter_ = options.filter_policy.get();
	}
}
void FilterBlockBuilder::Add(const std::string_view& key) {
	if(key.empty() || !Available()) {
		return;
	}
	datas_.emplace_back(key);
}
void FilterBlockBuilder::CreateFilter() {
	if (!Available() || datas_.empty()) {
		return;
	}
  	policy_filter_->CreateFilter(&datas_[0], datas_.size());
}
bool FilterBlockBuilder::MayMatch(const std::string& key) {
	if (key.empty() || !Available()) {
    		return false;
  	}
  	return policy_filter_->MayMatch(key, 0, 0);
}
bool FilterBlockBuilder::MayMatch(const std::string& key, const std::string& bf_datas){
	if (key.empty() || !Available()) {
    		return false;
  	}
  	return policy_filter_->MayMatch(key, bf_datas);
}
const std::string& FilterBlockBuilder::Data() {
	return buffer_;
}
void FilterBlockBuilder::Finish() {
	if (Available() && !datas_.empty()) {
		// 先构建布隆过滤器
		CreateFilter();
		// 序列化hash个数和bf本身数据
		buffer_ = policy_filter_->Data();
		PutFixed32(&buffer_, policy_filter_->GetMeta().hash_num);
	}
}
}