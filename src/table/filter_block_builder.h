#pragma once

#include <string>
#include <vector>
#include "../include/tinykv/filter_policy.h"
#include "../db/options.h"

namespace tinykv {
class FilterBlockBuilder final {
public: 
	FilterBlockBuilder(const Options& options);
	bool Available() { return policy_filter_ != nullptr; }
	void Add(const std::string_view& key);
	void CreateFilter();
	bool MayMatch(const std::string& key);
	bool MayMatch(const std::string& key, const std::string& bf_datas);
	const std::string& Data();
	void Finish();
private:
	std::string buffer_;
	std::vector<std::string> datas_;
	FilterPolicy* policy_filter_ = nullptr;
};
}