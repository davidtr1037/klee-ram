#include "klee/util/ArrayCache.h"

namespace klee {

ArrayCache::~ArrayCache() {
  // Free Allocated Array objects
  for (ArrayHashMap::iterator ai = cachedSymbolicArrays.begin(),
                              e = cachedSymbolicArrays.end();
       ai != e; ++ai) {
    delete *ai;
  }
  for (ArrayPtrVec::iterator ai = concreteArrays.begin(),
                             e = concreteArrays.end();
       ai != e; ++ai) {
    delete *ai;
  }
}

const Array *ArrayCache::CreateResizedArray(const Array *a, uint64_t _size) {
    std::string name = a->name + std::to_string(_size);
    Expr::Width _domain = a->domain;
    Expr::Width _range = a->range;
    if(a->isSymbolicArray()) return CreateArray(name, _size,0,0, _domain, _range);

    Array *array = new Array(name, _size, 0,0, _domain, _range);
    array->constantValues = a->constantValues;
    array->constantValues.resize(_size);
    ref<ConstantExpr> zero = ConstantExpr::create(0, _range);
    for(int i = a->size; i < _size; i++) {
        array->constantValues[i] = zero;
    }
    assert(array->isConstantArray());
    concreteArrays.push_back(array); // For deletion later
    return array;

}
const Array *
ArrayCache::CreateArray(const std::string &_name, uint64_t _size,
                        const ref<ConstantExpr> *constantValuesBegin,
                        const ref<ConstantExpr> *constantValuesEnd,
                        Expr::Width _domain, Expr::Width _range) {

  const Array *array = new Array(_name, _size, constantValuesBegin,
                                 constantValuesEnd, _domain, _range);
  if (array->isSymbolicArray()) {
    std::pair<ArrayHashMap::const_iterator, bool> success =
        cachedSymbolicArrays.insert(array);
    if (success.second) {
      // Cache miss
      return array;
    }
    // Cache hit
    delete array;
    array = *(success.first);
    assert(array->isSymbolicArray() &&
           "Cached symbolic array is no longer symbolic");
    return array;
  } else {
    // Treat every constant array as distinct so we never cache them
    assert(array->isConstantArray());
    concreteArrays.push_back(array); // For deletion later
    return array;
  }
}
}
