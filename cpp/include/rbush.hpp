#pragma once

#include "quickselect.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace rbush {
struct Box {
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
};
struct Item : Box { std::uint64_t id = 0; };

namespace detail {
template<class T, class = void> struct HasBox : std::false_type {};
template<class T> struct HasBox<T, std::void_t<decltype(std::declval<T>().minX),
    decltype(std::declval<T>().minY), decltype(std::declval<T>().maxX),
    decltype(std::declval<T>().maxY)>> : std::true_type {};
// Math.min/Math.max propagate NaN and preserve JavaScript's signed-zero choice.
inline double min(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<double>::quiet_NaN();
    if (a == 0 && b == 0) return std::signbit(a) || std::signbit(b) ? -0.0 : 0.0;
    return std::min(a, b);
}
inline double max(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<double>::quiet_NaN();
    if (a == 0 && b == 0) return std::signbit(a) && std::signbit(b) ? -0.0 : 0.0;
    return std::max(a, b);
}
inline Box& extend(Box& a, const Box& b) {
    a.minX = min(a.minX, b.minX); a.minY = min(a.minY, b.minY);
    a.maxX = max(a.maxX, b.maxX); a.maxY = max(a.maxY, b.maxY);
    return a;
}
inline double compareNodeMinX(const Box& a, const Box& b) { return a.minX - b.minX; }
inline double compareNodeMinY(const Box& a, const Box& b) { return a.minY - b.minY; }
inline double bboxArea(const Box& a) { return (a.maxX-a.minX)*(a.maxY-a.minY); }
inline double bboxMargin(const Box& a) { return (a.maxX-a.minX)+(a.maxY-a.minY); }
inline double enlargedArea(const Box& a, const Box& b) {
    return (max(b.maxX,a.maxX)-min(b.minX,a.minX))*(max(b.maxY,a.maxY)-min(b.minY,a.minY));
}
inline double intersectionArea(const Box& a, const Box& b) {
    const double minX=max(a.minX,b.minX), minY=max(a.minY,b.minY);
    const double maxX=min(a.maxX,b.maxX), maxY=min(a.maxY,b.maxY);
    return max(0,maxX-minX)*max(0,maxY-minY);
}
inline bool contains(const Box& a, const Box& b) {
    return a.minX<=b.minX && a.minY<=b.minY && b.maxX<=a.maxX && b.maxY<=a.maxY;
}
inline bool intersects(const Box& a, const Box& b) {
    return b.minX<=a.maxX && b.minY<=a.maxY && b.maxX>=a.minX && b.maxY>=a.minY;
}

template<class T, class Compare>
void multiSelect(std::vector<T>& arr, std::ptrdiff_t left, std::ptrdiff_t right,
                 std::ptrdiff_t n, Compare compare) {
    std::vector<std::ptrdiff_t> stack{left,right};
    while (!stack.empty()) {
        right=stack.back(); stack.pop_back();
        left=stack.back(); stack.pop_back();
        if (right-left<=n) continue;
        const auto mid=left+static_cast<std::ptrdiff_t>(std::ceil(static_cast<double>(right-left)/n/2))*n;
        quickselect(arr,mid,left,right,compare);
        stack.insert(stack.end(),{left,mid,mid,right});
    }
}
} // namespace detail

// The same object can occur multiple times. Shared pointers model JS item/node
// reference identity; toJSON/fromJSON intentionally share their node graph.
template<class T = Item>
class RBush {
public:
    using ItemPtr = std::shared_ptr<T>;
    struct Node;
    using NodePtr = std::shared_ptr<Node>;
    using Child = std::variant<ItemPtr, NodePtr>;
    struct Node : Box {
        std::vector<Child> children;
        int height = 1;
        bool leaf = true;
    };
    using Equals = std::function<bool(const ItemPtr&,const ItemPtr&)>;

    explicit RBush(int maxEntries = 9)
        : _maxEntries(std::max(4,maxEntries)),
          _minEntries(std::max(2,static_cast<int>(std::ceil(_maxEntries*0.4)))) { clear(); }
    virtual ~RBush() = default;
    int maxEntries() const { return _maxEntries; }
    int minEntries() const { return _minEntries; }

    std::vector<ItemPtr> all() const {
        std::vector<ItemPtr> result;
        _all(data,result);
        return result;
    }
    std::vector<ItemPtr> search(const Box& bbox) const {
        auto node=data;
        std::vector<ItemPtr> result;
        if (!detail::intersects(bbox,*node)) return result;
        std::vector<NodePtr> nodesToSearch;
        while (node) {
            for (const auto& child:node->children) {
                const Box childBBox=childBBoxOf(node,child);
                if (detail::intersects(bbox,childBBox)) {
                    if (node->leaf) result.push_back(std::get<ItemPtr>(child));
                    else if (detail::contains(bbox,childBBox)) _all(std::get<NodePtr>(child),result);
                    else nodesToSearch.push_back(std::get<NodePtr>(child));
                }
            }
            node=pop(nodesToSearch);
        }
        return result;
    }
    bool collides(const Box& bbox) const {
        auto node=data;
        if (!detail::intersects(bbox,*node)) return false;
        std::vector<NodePtr> nodesToSearch;
        while (node) {
            for (const auto& child:node->children) {
                const Box childBBox=childBBoxOf(node,child);
                if (detail::intersects(bbox,childBBox)) {
                    if (node->leaf || detail::contains(bbox,childBBox)) return true;
                    nodesToSearch.push_back(std::get<NodePtr>(child));
                }
            }
            node=pop(nodesToSearch);
        }
        return false;
    }
    RBush& load(const std::vector<ItemPtr>& items) {
        if (items.empty()) return *this;
        if (items.size()<static_cast<std::size_t>(_minEntries)) {
            for (const auto& item:items) insert(item);
            return *this;
        }
        auto copy=items;
        auto node=_build(copy,0,static_cast<Index>(copy.size())-1,0);
        if (data->children.empty()) data=node;
        else if (data->height==node->height) _splitRoot(data,node);
        else {
            if (data->height<node->height) std::swap(data,node);
            _insert(node,data->height-node->height-1,true);
        }
        return *this;
    }
    RBush& insert(const ItemPtr& item = {}) {
        if (item) _insert(item,data->height-1,false);
        return *this;
    }
    RBush& clear() { data=createNode({}); return *this; }
    RBush& remove(const ItemPtr& item = {}, const Equals& equalsFn = {}) {
        if (!item) return *this;
        auto node=data;
        const Box bbox=toBBox(item);
        std::vector<NodePtr> path;
        std::vector<Index> indexes;
        Index i=0;
        NodePtr parent;
        bool goingUp=false;
        while (node || !path.empty()) {
            if (!node) {
                node=pop(path);
                parent=path.empty()?nullptr:path.back();
                i=indexes.back(); indexes.pop_back();
                goingUp=true;
            }
            if (node->leaf) {
                const Index index=findItem(item,node->children,equalsFn);
                if (index!=-1) {
                    node->children.erase(node->children.begin()+index);
                    path.push_back(node);
                    _condense(path);
                    return *this;
                }
            }
            if (!goingUp && !node->leaf && detail::contains(*node,bbox)) {
                path.push_back(node); indexes.push_back(i); i=0; parent=node;
                node=node->children.empty()?nullptr:std::get<NodePtr>(node->children[0]);
            } else if (parent) {
                i++;
                node=i<static_cast<Index>(parent->children.size())?std::get<NodePtr>(parent->children[i]):nullptr;
                goingUp=false;
            } else node=nullptr;
        }
        return *this;
    }
    virtual Box toBBox(const ItemPtr& item) const {
        if constexpr (detail::HasBox<T>::value) return {item->minX,item->minY,item->maxX,item->maxY};
        else throw std::logic_error("Override toBBox for this item type");
    }
    virtual double compareMinX(const ItemPtr& a,const ItemPtr& b) const {
        if constexpr (detail::HasBox<T>::value) return a->minX-b->minX;
        else throw std::logic_error("Override compareMinX for this item type");
    }
    virtual double compareMinY(const ItemPtr& a,const ItemPtr& b) const {
        if constexpr (detail::HasBox<T>::value) return a->minY-b->minY;
        else throw std::logic_error("Override compareMinY for this item type");
    }
    NodePtr toJSON() const { return data; }
    RBush& fromJSON(const NodePtr& node) { data=node; return *this; }

    // Public as in upstream RBush; callers must preserve the node invariants.
    NodePtr data;

private:
    using Index=std::ptrdiff_t;
    int _maxEntries;
    int _minEntries;
    static NodePtr createNode(std::vector<Child> children) {
        auto node=std::make_shared<Node>(); node->children=std::move(children); return node;
    }
    static NodePtr pop(std::vector<NodePtr>& nodes) {
        if (nodes.empty()) return nullptr;
        auto node=nodes.back(); nodes.pop_back(); return node;
    }
    Box childBBoxOf(const NodePtr& node,const Child& child) const {
        return node->leaf?toBBox(std::get<ItemPtr>(child)):static_cast<Box>(*std::get<NodePtr>(child));
    }
    static Index findItem(const ItemPtr& item,const std::vector<Child>& items,const Equals& equalsFn) {
        for (Index i=0;i<static_cast<Index>(items.size());i++) {
            const auto& candidate=std::get<ItemPtr>(items[i]);
            if (equalsFn?equalsFn(item,candidate):item==candidate) return i;
        }
        return -1;
    }
    void _all(NodePtr node,std::vector<ItemPtr>& result) const {
        std::vector<NodePtr> nodesToSearch;
        while (node) {
            if (node->leaf) for (const auto& child:node->children) result.push_back(std::get<ItemPtr>(child));
            else for (const auto& child:node->children) nodesToSearch.push_back(std::get<NodePtr>(child));
            node=pop(nodesToSearch);
        }
    }
    NodePtr _build(std::vector<ItemPtr>& items,Index left,Index right,int height) {
        const Index N=right-left+1;
        Index M=_maxEntries;
        NodePtr node;
        if (N<=M) {
            std::vector<Child> children;
            for (Index i=left;i<=right;i++) children.emplace_back(items[i]);
            node=createNode(std::move(children)); calcBBox(node); return node;
        }
        if (!height) {
            height=static_cast<int>(std::ceil(std::log(static_cast<double>(N))/std::log(static_cast<double>(M))));
            M=static_cast<Index>(std::ceil(N/std::pow(static_cast<double>(M),height-1)));
        }
        node=createNode({}); node->leaf=false; node->height=height;
        const Index N2=static_cast<Index>(std::ceil(static_cast<double>(N)/M));
        const Index N1=N2*static_cast<Index>(std::ceil(std::sqrt(static_cast<double>(M))));
        detail::multiSelect(items,left,right,N1,[this](const auto& a,const auto& b){return compareMinX(a,b);});
        for (Index i=left;i<=right;i+=N1) {
            const Index right2=std::min(i+N1-1,right);
            detail::multiSelect(items,i,right2,N2,[this](const auto& a,const auto& b){return compareMinY(a,b);});
            for (Index j=i;j<=right2;j+=N2) {
                const Index right3=std::min(j+N2-1,right2);
                node->children.emplace_back(_build(items,j,right3,height-1));
            }
        }
        calcBBox(node); return node;
    }
    NodePtr _chooseSubtree(const Box& bbox,NodePtr node,int level,std::vector<NodePtr>& path) {
        while (true) {
            path.push_back(node);
            if (node->leaf || static_cast<int>(path.size())-1==level) break;
            double minArea=std::numeric_limits<double>::infinity();
            double minEnlargement=std::numeric_limits<double>::infinity();
            NodePtr targetNode;
            for (const auto& entry:node->children) {
                const auto child=std::get<NodePtr>(entry);
                const double area=detail::bboxArea(*child);
                const double enlargement=detail::enlargedArea(bbox,*child)-area;
                if (enlargement<minEnlargement) {
                    minEnlargement=enlargement; minArea=area<minArea?area:minArea; targetNode=child;
                } else if (enlargement==minEnlargement) {
                    if (area<minArea) { minArea=area; targetNode=child; }
                }
            }
            node=targetNode?targetNode:std::get<NodePtr>(node->children[0]);
        }
        return node;
    }
    void _insert(const Child& item,int level,bool isNode) {
        const Box bbox=isNode?static_cast<Box>(*std::get<NodePtr>(item)):toBBox(std::get<ItemPtr>(item));
        std::vector<NodePtr> insertPath;
        auto node=_chooseSubtree(bbox,data,level,insertPath);
        node->children.push_back(item); detail::extend(*node,bbox);
        while (level>=0) {
            if (insertPath[level]->children.size()>static_cast<std::size_t>(_maxEntries)) {
                _split(insertPath,level); level--;
            } else break;
        }
        _adjustParentBBoxes(bbox,insertPath,level);
    }
    void _split(std::vector<NodePtr>& insertPath,int level) {
        const auto node=insertPath[level];
        const Index M=static_cast<Index>(node->children.size()), m=_minEntries;
        _chooseSplitAxis(node,m,M);
        const Index splitIndex=_chooseSplitIndex(node,m,M);
        auto newNode=createNode({node->children.begin()+splitIndex,node->children.end()});
        node->children.erase(node->children.begin()+splitIndex,node->children.end());
        newNode->height=node->height; newNode->leaf=node->leaf;
        calcBBox(node); calcBBox(newNode);
        if (level) insertPath[level-1]->children.emplace_back(newNode);
        else _splitRoot(node,newNode);
    }
    void _splitRoot(NodePtr node,NodePtr newNode) {
        data=createNode({node,newNode}); data->height=node->height+1; data->leaf=false; calcBBox(data);
    }
    Index _chooseSplitIndex(const NodePtr& node,Index m,Index M) const {
        Index index=0;
        double minOverlap=std::numeric_limits<double>::infinity(),minArea=minOverlap;
        for (Index i=m;i<=M-m;i++) {
            const Box bbox1=distBBox(node,0,i),bbox2=distBBox(node,i,M);
            const double overlap=detail::intersectionArea(bbox1,bbox2);
            const double area=detail::bboxArea(bbox1)+detail::bboxArea(bbox2);
            if (overlap<minOverlap) {
                minOverlap=overlap; index=i; minArea=area<minArea?area:minArea;
            } else if (overlap==minOverlap) {
                if (area<minArea) { minArea=area; index=i; }
            }
        }
        return index?index:M-m;
    }
    void _chooseSplitAxis(const NodePtr& node,Index m,Index M) {
        const auto compareX=[this,node](const Child& a,const Child& b) {
            return node->leaf?compareMinX(std::get<ItemPtr>(a),std::get<ItemPtr>(b)):
                detail::compareNodeMinX(*std::get<NodePtr>(a),*std::get<NodePtr>(b));
        };
        const auto compareY=[this,node](const Child& a,const Child& b) {
            return node->leaf?compareMinY(std::get<ItemPtr>(a),std::get<ItemPtr>(b)):
                detail::compareNodeMinY(*std::get<NodePtr>(a),*std::get<NodePtr>(b));
        };
        const double xMargin=_allDistMargin(node,m,M,compareX);
        const double yMargin=_allDistMargin(node,m,M,compareY);
        if (xMargin<yMargin) stableSort(node->children,compareX);
    }
    template<class Compare> static void stableSort(std::vector<Child>& children,Compare compare) {
        std::stable_sort(children.begin(),children.end(),[&compare](const Child& a,const Child& b){return compare(a,b)<0;});
    }
    template<class Compare>
    double _allDistMargin(const NodePtr& node,Index m,Index M,Compare compare) {
        stableSort(node->children,compare);
        Box leftBBox=distBBox(node,0,m),rightBBox=distBBox(node,M-m,M);
        double margin=detail::bboxMargin(leftBBox)+detail::bboxMargin(rightBBox);
        for (Index i=m;i<M-m;i++) {
            detail::extend(leftBBox,childBBoxOf(node,node->children[i])); margin+=detail::bboxMargin(leftBBox);
        }
        for (Index i=M-m-1;i>=m;i--) {
            detail::extend(rightBBox,childBBoxOf(node,node->children[i])); margin+=detail::bboxMargin(rightBBox);
        }
        return margin;
    }
    static void _adjustParentBBoxes(const Box& bbox,const std::vector<NodePtr>& path,int level) {
        for (int i=level;i>=0;i--) detail::extend(*path[i],bbox);
    }
    void _condense(const std::vector<NodePtr>& path) {
        for (Index i=static_cast<Index>(path.size())-1;i>=0;i--) {
            if (path[i]->children.empty()) {
                if (i>0) {
                    auto& siblings=path[i-1]->children;
                    auto it=std::find_if(siblings.begin(),siblings.end(),[&](const Child& child){return std::get<NodePtr>(child)==path[i];});
                    siblings.erase(it);
                } else clear();
            } else calcBBox(path[i]);
        }
    }
    void calcBBox(const NodePtr& node) const {
        distBBox(node,0,static_cast<Index>(node->children.size()),static_cast<Box&>(*node));
    }
    Box distBBox(const NodePtr& node,Index k,Index p) const {
        Box dest;
        distBBox(node,k,p,dest);
        return dest;
    }
    void distBBox(const NodePtr& node,Index k,Index p,Box& dest) const {
        dest=Box{};
        for (Index i=k;i<p;i++) detail::extend(dest,childBBoxOf(node,node->children[i]));
    }
};
} // namespace rbush
