#include "filter/bloomfilter.h"
#include <memory>

using namespace std;
using namespace tinykv;
int main()
{
	//std::unique_ptr<tinykv::FilterPolicy> filter_policy = std::make_unique<tinykv::BloomFilter>(30);
	// unique_ptr<BloomFilter> filter_policy(new BloomFilter(30, 0.1));
	//unique_ptr<int>p1(new int(5));
	BloomFilter filter(10);
	return 0;
}