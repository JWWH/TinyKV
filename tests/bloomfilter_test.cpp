#include "../src/filter/bloomfilter.h"

#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include <iostream>

static const std::vector<std::string> kTestKeys = {"tinykv",  "tinykv1", "tinykv2"};

TEST(bloomFilterTest, CreateFilter)
{
	// std::cout<<"in Test"<< std::endl;
	std::unique_ptr<tinykv::FilterPolicy> filter_policy = std::make_unique<tinykv::BloomFilter>(30);
	//std::unique_ptr<tinykv::FilterPolicy> filter_policy(new tinykv::BloomFilter(30));
	std::vector<std::string> tmp;
	for (const auto& item : kTestKeys) {
		tmp.emplace_back(item);
	}
	filter_policy->CreateFilter(&tmp[0], tmp.size());
	tmp.emplace_back("hardcore");
	for(const auto& item : tmp)
	{
		for (const auto& item : tmp) {
		std::cout << "[ key:" << item
			<< ", has_existed:" << filter_policy->MayMatch(item, 0, 0) << " ]"
			<< std::endl;
		}
	}
}

