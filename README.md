# SGCL

SGCL (Smart Garbage Collection Library) is a concurrent, non-moving mark-and-sweep garbage collector for C++20.
Objects are allocated with `make_tracked`, referenced through `tracked_ptr`, and destroyed by a background collector
thread once nothing reaches them, reference cycles included. A cheap write barrier keeps the collector concurrent
and pause-free.

This fork narrows SGCL to one small, fully specified surface. **[SPEC.md](SPEC.md) is the contract**: the supported
types and functions, the design of every primitive, and the behaviour of every operation when user code throws,
memory runs out or a precondition is broken. Anything SPEC.md does not list is an implementation detail.

```cpp
#include "sgcl/state_impl.h"   // in exactly one translation unit; others include "sgcl/sgcl.h"

struct Node {
    int value;
    sgcl::tracked_ptr<Node> next;
    explicit Node(int v) : value(v) {}
};

int main() {
    sgcl::tracked_ptr<Node> a = sgcl::make_tracked<Node>(1);
    a->next = sgcl::make_tracked<Node>(2);
    a->next->next = a;              // a cycle: collected once `a` goes away
    a = nullptr;
    sgcl::collector::force_collect(true);
    sgcl::collector::terminate();   // before the process exits
}
```

Exactly one translation unit of the program must include `sgcl/state_impl.h`, which defines the process-wide state
(SPEC.md §3.8).

## Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j8
build/tests/tests && build/tests/static_init_test && build/tests/multi_image/multi_image_test
```

The `external/googletest` submodule is needed for the tests only.

## License

Apache-2.0; see [LICENSE](LICENSE).
