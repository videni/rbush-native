#include "rbush.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

// Line-oriented test transport. Replays go through the public C ABI only.
namespace {
void check(rbush_status s) { if (s != RBUSH_OK) throw std::runtime_error(rbush_last_error()); }
double number() { std::string s; std::cin >> s; return std::stod(s); }
rbush_item item() { uint64_t id; std::cin >> id; return {number(),number(),number(),number(),id}; }
rbush_box box() { return {number(),number(),number(),number()}; }
void value(double x) { if (std::isfinite(x)) std::cout << std::setprecision(17) << x; else std::cout << '"' << (std::isnan(x) ? "NaN" : x < 0 ? "-Infinity" : "Infinity") << '"'; }
void bounds(double x, double y, double X, double Y) {
    std::cout << "\"minX\":"; value(x); std::cout << ",\"minY\":"; value(y);
    std::cout << ",\"maxX\":"; value(X); std::cout << ",\"maxY\":"; value(Y);
}
void emitNode(size_t index, const std::vector<rbush_node>& nodes, const std::vector<rbush_item>& items, const std::vector<uint64_t>& children) {
    const auto& n = nodes.at(index);
    std::cout << '{'; bounds(n.min_x,n.min_y,n.max_x,n.max_y);
    std::cout << ",\"height\":" << n.height << ",\"leaf\":" << (n.leaf ? "true" : "false") << ",\"children\":[";
    for (size_t i=0; i<n.child_count; ++i) {
        if (i) std::cout << ',';
        if (n.leaf) std::cout << "{\"identity\":" << items.at(n.child_offset+i).identity << '}';
        else emitNode(children.at(n.child_offset+i),nodes,items,children);
    }
    std::cout << "]}";
}
int equalIdentity(uint64_t, uint64_t candidate, void* context) {
    return static_cast<std::unordered_set<uint64_t>*>(context)->count(candidate) != 0;
}
struct Comparators {std::map<std::pair<uint64_t,uint64_t>,std::pair<double,double>> values;};
double compareX(uint64_t a,uint64_t b,void* context){return static_cast<Comparators*>(context)->values.at({a,b}).first;}
double compareY(uint64_t a,uint64_t b,void* context){return static_cast<Comparators*>(context)->values.at({a,b}).second;}
}
int main() {
    rbush_tree* tree = nullptr;
    Comparators comparators;
    try {
        char op;
        while (std::cin >> op) {
            if (op == 'C') { int max; std::cin >> max; rbush_destroy(tree); tree=nullptr; check(rbush_create(max,&tree)); }
            else if (op == 'I') check(rbush_insert(tree,item()));
            else if (op == 'L') { size_t n; std::cin >> n; std::vector<rbush_item> items(n); for (auto& i:items) i=item(); check(rbush_load(tree,items.data(),items.size())); }
            else if (op == 'R') {
                const auto requested=item(); int64_t count; std::cin >> count;
                std::unordered_set<uint64_t> matches;
                for (int64_t i=0;i<count;++i) { uint64_t id; std::cin >> id; matches.insert(id); }
                int32_t removed;uint64_t identity;
                check(rbush_remove_ex(tree,requested,count < 0 ? nullptr : equalIdentity,&matches,&removed,&identity));
                std::cout<<(removed?identity:0)<<'\n';
            } else if(op=='P') {
                size_t count;int x,y;std::cin>>count>>x>>y;comparators.values.clear();
                for(size_t i=0;i<count;++i){uint64_t a,b;std::cin>>a>>b;double vx=number(),vy=number();comparators.values[{a,b}]={vx,vy};}
                check(rbush_set_comparators(tree,x?compareX:nullptr,y?compareY:nullptr,&comparators));
            } else if (op == 'Z') check(rbush_clear(tree));
            else if (op == 'A' || op == 'S') {
                const auto query=op == 'S' ? box() : rbush_box{};
                rbush_result* result=nullptr;
                check(op == 'A' ? rbush_all_result(tree,&result) : rbush_search_result(tree,query,&result));
                const size_t count=rbush_result_count(result); const rbush_item* items=rbush_result_data(result);
                std::cout << '['; for (size_t i=0;i<count;++i) { if(i) std::cout << ','; std::cout << items[i].identity; } std::cout << "]\n";
                rbush_result_destroy(result);
            } else if (op == 'X') { int32_t result; check(rbush_collides(tree,box(),&result)); std::cout << (result ? "true" : "false") << '\n'; }
            else if (op == 'E') {
                size_t nn,ni,nc; check(rbush_export_counts(tree,&nn,&ni,&nc));
                std::vector<rbush_node> nodes(nn); std::vector<rbush_item> items(ni); std::vector<uint64_t> children(nc);
                check(rbush_to_json(tree,nodes.data(),nn,items.data(),ni,children.data(),nc));
                emitNode(0,nodes,items,children); std::cout << '\n';
            } else if (op == 'F') {
                size_t nn,ni,nc; std::cin >> nn >> ni >> nc;
                std::vector<rbush_node> nodes(nn); std::vector<rbush_item> items(ni); std::vector<uint64_t> children(nc);
                for (auto& n:nodes) { n.min_x=number(); n.min_y=number(); n.max_x=number(); n.max_y=number(); std::cin >> n.child_offset >> n.child_count >> n.height >> n.leaf; }
                for (auto& i:items) i=item(); for (auto& i:children) std::cin >> i;
                check(rbush_from_json(tree,nodes.data(),nn,items.data(),ni,children.data(),nc));
            } else throw std::runtime_error("unknown operation");
        }
        rbush_destroy(tree); return 0;
    } catch (const std::exception& e) { rbush_destroy(tree); std::cerr << e.what() << '\n'; return 1; }
}
