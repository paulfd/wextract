#include <memory>

template<typename F>
class DeferFinalizer {
    F f;
    bool moved;
  public:
    template<typename T>
    DeferFinalizer(T && f_)
    : f(std::forward<T>(f_)), moved(false) 
    { }

    DeferFinalizer(const DeferFinalizer &) = delete;

    DeferFinalizer(DeferFinalizer && other) 
    : f(std::move(other.f)), moved(other.moved)
    {
        other.moved = true;
    }

    ~DeferFinalizer()
    {
        if (!moved)
            f();
    }
};

struct {
    template<typename F>
    DeferFinalizer<F> operator<<(F && f)
    {
        return DeferFinalizer<F>(std::forward<F>(f));
    }
} deferrer;

#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)

#define defer auto TOKENPASTE2(__deferred_lambda_call, __COUNTER__) = deferrer << [&]