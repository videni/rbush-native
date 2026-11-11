#include "rbush.hpp"
#include <cassert>
#include <iostream>

using Tree = rbush::RBush<>;
static Tree::ItemPtr item(double x, double y, std::uint64_t id) {
    auto p = std::make_shared<rbush::Item>();
    p->minX=x; p->minY=y; p->maxX=x+1; p->maxY=y+1; p->id=id;
    return p;
}
struct CoordinateItem { double west, south, east, north; };
struct CustomTree : rbush::RBush<CoordinateItem> {
    using RBush::RBush;
    rbush::Box toBBox(const ItemPtr& p) const override { return {p->west,p->south,p->east,p->north}; }
    double compareMinX(const ItemPtr& a,const ItemPtr& b) const override { return a->west-b->west; }
    double compareMinY(const ItemPtr& a,const ItemPtr& b) const override { return a->south-b->south; }
};
int main() {
    Tree tree;
    assert(tree.maxEntries()==9 && tree.minEntries()==4);
    assert(Tree(1).maxEntries()==4 && Tree(1).minEntries()==2);
    auto first=item(1,2,7);
    tree.insert(first);
    assert(tree.search({2,3,2,3}).size()==1); // inclusive degenerate query
    tree.remove(item(1,2,7)); // same value is not the same object
    assert(tree.all().size()==1);
    tree.remove(item(1,2,7), [](const auto& a,const auto& b){return a->id==b->id;});
    assert(tree.all().empty());
    std::vector<Tree::ItemPtr> batch;
    for (int i=0;i<1200;++i) batch.push_back(item(i%31,i/31,i));
    tree.load(batch);
    assert(tree.all().size()==batch.size());
    assert(tree.toJSON()->height>1);
    auto root=tree.toJSON();
    Tree copy; copy.fromJSON(root);
    assert(copy.toJSON()==root);
    assert(copy.collides({0,0,1,1}));
    for (const auto& p: batch) tree.remove(p);
    assert(tree.all().empty() && tree.toJSON()->height==1 && tree.toJSON()->leaf);
    Tree merged(4);
    std::vector<Tree::ItemPtr> fortyEight(batch.begin(),batch.begin()+48);
    merged.load(fortyEight);
    const int beforeMergeHeight=merged.data->height;
    merged.load(fortyEight);
    assert(merged.data->height==beforeMergeHeight+1);
    assert(merged.all().size()==96);
    // OMT/STLT merges in all relative-height directions, followed by split insertions.
    for (int n : {3,70,800,70,3}) {
        tree.load({batch.begin(), batch.begin()+n});
    }
    assert(tree.all().size()==946);
    for (int i=0;i<100;++i) tree.insert(batch[i]);
    assert(tree.all().size()==1046);
    tree.clear();
    tree.insert(first).insert(first);
    tree.remove(first);
    assert(tree.all().size()==1 && tree.all()[0]==first);
    tree.remove(first);
    assert(tree.all().empty());
    first->maxX=first->minX; first->maxY=first->minY;
    tree.insert(first);
    assert(tree.collides(*first));
    CustomTree custom(4);
    std::vector<CustomTree::ItemPtr> coordinates;
    for (int i=0;i<50;i++) coordinates.push_back(std::make_shared<CoordinateItem>(CoordinateItem{double(i),0,double(i),0}));
    custom.load(coordinates);
    assert(custom.search({10,0,20,0}).size()==11);
    for (const auto& p:coordinates) custom.remove(p);
    assert(custom.all().empty());
    Tree infinite(4);
    const double inf=std::numeric_limits<double>::infinity();
    for (int i=0;i<6;i++) {
        auto p=item(0,0,i); p->minX=-inf; p->minY=-inf; p->maxX=inf; p->maxY=inf;
        infinite.insert(p);
    }
    assert(infinite.data->height==2);
    assert(std::get<Tree::NodePtr>(infinite.data->children[0])->children.size()==4);
    assert(std::get<Tree::NodePtr>(infinite.data->children[1])->children.size()==2);
    const auto firstLeaf=std::get<Tree::NodePtr>(infinite.data->children[0]);
    assert(std::get<Tree::ItemPtr>(firstLeaf->children[0])->id==0);
    assert(std::get<Tree::ItemPtr>(firstLeaf->children[1])->id==1);
    assert(std::get<Tree::ItemPtr>(firstLeaf->children[2])->id==2);
    assert(std::get<Tree::ItemPtr>(firstLeaf->children[3])->id==5);
    const auto originalChildren=firstLeaf->children;
    assert(infinite.search({0,0,0,0}).size()==6);
    assert(infinite.collides({0,0,0,0}));
    assert(infinite.all().size()==6);
    assert(firstLeaf->children==originalChildren);
    for (auto p:infinite.all()) infinite.remove(p);
    assert(infinite.all().empty());
    std::vector<int> selection;
    for (int i=0;i<2000;i++) selection.push_back((i*797)%997);
    const auto original=selection;
    auto sorted=selection; std::sort(sorted.begin(),sorted.end());
    for (std::ptrdiff_t k : {0,1,600,1000,1998,1999}) {
        selection=original;
        rbush::quickselect(selection,k);
        assert(selection[k]==sorted[k]);
        for (std::ptrdiff_t i=0;i<k;i++) assert(selection[i]<=selection[k]);
        for (std::ptrdiff_t i=k+1;i<2000;i++) assert(selection[i]>=selection[k]);
    }
    tree.insert().remove();
    std::cout << "core_smoke: PASS\n";
}
