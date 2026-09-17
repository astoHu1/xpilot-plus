#pragma once
namespace QInjection {
template<class T> class Pointer {
public:
    T *data() const { return T::instance; }
};
}
