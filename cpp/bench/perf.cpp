#include "rbush.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// bench/perf.js: same randBox formula, maxFill, datasets and operation order.
// A seeded LCG replaces Math.random so JS/WASM and native get the same inputs.
using Tree=rbush::RBush<>;
struct Random {
    uint32_t state=42;
    double operator()() { state=state*1664525u+1013904223u; return state/4294967296.0; }
};
std::vector<Tree::ItemPtr> generate(size_t n,double size,Random& random,uint64_t first=0) {
    std::vector<Tree::ItemPtr> data; data.reserve(n);
    for(size_t i=0;i<n;++i) {
        const double x=random()*(100-size),y=random()*(100-size);
        auto item=std::make_shared<rbush::Item>();
        item->minX=x;item->minY=y;item->maxX=x+size*random();item->maxY=y+size*random();item->id=first+i;
        data.push_back(item);
    }
    return data;
}
template<class F> void measure(const char* name,F fn) {
    const auto start=std::chrono::steady_clock::now();
    const uint64_t checksum=fn();
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<name<<','<<std::fixed<<std::setprecision(3)<<ms<<','<<checksum<<'\n';
}
int main(int argc,char** argv) {
    if(argc>1&&std::string(argv[1])=="suite") {
        // Direct counterparts of insert.bench, bulk.bench, search.bench and
        // bulksearch.bench. One timed sample per case, not Benchmark.js statistics.
        Random random;
        const auto insertData=generate(100,1,random),bulkData=generate(10000,1,random);
        std::cout<<"# original microbenchmark workloads; maxFill=16; seed=42\noperation,ms,checksum\n";
        measure("insert 100 items",[&]{Tree t(16);for(const auto& i:insertData)t.insert(i);return insertData.size();});
        measure("bulk loading 10000 items",[&]{Tree t(16);t.load(bulkData);return bulkData.size();});
        for(bool bulk:{false,true}) {
            const auto data=generate(10000,bulk?100:1,random);
            const auto q100=generate(1000,100*std::sqrt(.1),random),q10=generate(1000,10,random),q1=generate(1000,1,random);
            Tree t(16);if(bulk)t.load(data);else for(const auto& i:data)t.insert(i);
            auto search=[&](const auto& boxes){uint64_t hits=0;for(const auto& b:boxes)hits+=t.search(*b).size();return hits;};
            std::cout<<(bulk?"# bulksearch.bench.js, size100 input\n":"# search.bench.js, size1 input\n");
            measure("1000 searches 10%",[&]{return search(q100);});
            measure("1000 searches 1%",[&]{return search(q10);});
            measure("1000 searches 0.01%",[&]{return search(q1);});
        }
        return 0;
    }
    const size_t n=argc>1?std::strtoull(argv[1],nullptr,10):1000000;
    if(n==0) { std::cerr<<"item count must be positive\n";return 1; }
    Random random;
    const auto data=generate(n,1,random),data2=generate(n,1,random,n);
    const auto bboxes100=generate(1000,100*std::sqrt(.1),random),bboxes10=generate(1000,10,random),bboxes1=generate(1000,1,random);
    Tree tree(16);
    std::cout<<"# core-only C++17; items="<<n<<"; maxFill=16; seed=42\noperation,ms,checksum\n";
    measure("insert one by one",[&]{for(const auto& item:data)tree.insert(item);return 0;});
    auto searches=[&](const auto& boxes){uint64_t hits=0;for(const auto& box:boxes)hits+=tree.search(*box).size();return hits;};
    measure("1000 searches 10%",[&]{return searches(bboxes100);});
    measure("1000 searches 1%",[&]{return searches(bboxes10);});
    measure("1000 searches 0.01%",[&]{return searches(bboxes1);});
    measure("remove 1000 one by one",[&]{for(size_t i=0;i<std::min<size_t>(1000,n);++i)tree.remove(data[i]);return 0;});
    measure("bulk-insert more",[&]{tree.load(data2);return 0;});
    measure("1000 searches 1% after bulk",[&]{return searches(bboxes10);});
    measure("1000 searches 0.01% after bulk",[&]{return searches(bboxes1);});
    measure("clear + bulk load",[&]{tree.clear().load(data);return 0;});
}
