# SGCL specification

This document is the contract of this SGCL fork: what it offers, how every primitive is built, and what happens in every
situation a program can reach. The code conforms to it; where they disagree, one of the two is a bug and is fixed. Words
in **MUST**/**MUST NOT** are requirements on the program using SGCL (the *mutator*); everything else is a guarantee SGCL
gives.

Contents:

1. [Scope and environment](#1-scope-and-environment)
2. [Use cases](#2-use-cases)
3. [Public surface](#3-public-surface)
4. [Design](#4-design)
5. [Behaviour per situation](#5-behaviour-per-situation)
6. [Exception-safety contract](#6-exception-safety-contract)
7. [Concurrency and memory ordering](#7-concurrency-and-memory-ordering)
8. [Limits](#8-limits)
9. [Open design questions](#9-open-design-questions)

## 1. Scope and environment

SGCL is a concurrent, non-moving mark-and-sweep garbage collector for C++ objects. A program allocates objects with
`make_tracked`, refers to them through `tracked_ptr`, and never frees them: one background *collector thread* finds the
objects no `tracked_ptr` reaches any more and destroys them.

The single supported environment:

- C++20, exceptions enabled, RTTI enabled.
- One C++ runtime and one heap per process (on Windows, every image built with `/MD` and the same
  `_ITERATOR_DEBUG_LEVEL`).
- Native threads, created by any means (`std::thread`, the platform API, a thread pool), joined or detached. A thread
  may still run when the process shuts the collector down.
- One process-wide state (§4.9). The program may consist of several images (executables, shared libraries) that all
  reach that one state.
- Destructors of managed types do not throw (§6).

SGCL is not a general-purpose library. Anything not listed in §3 is an implementation detail and is unsupported, even
when it is reachable from a header.

## 2. Use cases

| # | Use case | Primitive |
|---|---|---|
| U1 | Allocate an object and get its first reference. | `make_tracked<T>(args...)` → `unique_ptr<T>` → `tracked_ptr<T>` |
| U2 | Hold a reference in a local variable, a parameter or a temporary. | `tracked_ptr<T>` on the stack (§4.3, stack root) |
| U3 | Hold a reference in a field of a managed object, or in an element of a managed container. | `tracked_ptr<T>` inside a managed allocation (§4.3, child) |
| U4 | Hold a reference anywhere else: a global, a thread-local, a `std::` container, an exception object, a `std::function` capture, memory from `new`. | `tracked_ptr<T>` in external storage (§4.3, external root) |
| U5 | Convert a reference to a base class, including a virtual base; narrow it back. | converting constructors; `dynamic_pointer_cast` |
| U6 | Make a reference from a raw pointer to a live managed object, including `this` inside the object's own constructor. | `tracked_ptr<T>(T*)` |
| U7 | Build reference cycles of any shape and have them collected. | tracing (§4.6) |
| U8 | A growable array of values that may contain references. | `vector<T>` |
| U9 | A hash map and a hash set (a map whose value is an empty marker) whose keys and values may contain references, with user hash and equality functions that may throw. | `unordered_map<K, V, H, E>` |
| U10 | Keep a container iterator inside a managed object across calls. | container iterators (§3.5, §3.6) |
| U11 | Run code when an object becomes unreachable. That code reads the object and its children, may allocate, and may make objects reachable again. | the finalize hook (§4.7) |
| U12 | Collect now and wait until every object unreachable at the call has been processed. | `collector::force_collect(true)` |
| U13 | Count or list the live objects for diagnostics. | `collector::get_live_object_count()`, `collector::get_live_objects()` |
| U14 | Use managed objects across several images of one process. | single process state (§4.9) |
| U15 | Shut down at process exit, while detached threads may still run. | `collector::terminate()` |

## 3. Public surface

Everything is in namespace `sgcl`. Headers: `sgcl/sgcl.h` (all of the below), and `sgcl/state_impl.h` (§3.8).

### 3.1 `make_tracked`

```cpp
template<class T, class... A> unique_ptr<T> make_tracked(A&&... args);
```

Allocates managed storage for one `T` and constructs it with `T(std::forward<A>(args)...)` (or `T` default-initialized
when `args` is empty). `T` is a complete, non-array, non-void object type with `sizeof(T)` within the limit of §8, and
`std::is_nothrow_destructible_v<T>` (rejected at compile time otherwise). If the constructor throws, the exception
propagates and the storage is reclaimed by a later cycle without running a destructor (§5). Out of memory is fatal
(§5.7).

### 3.2 `unique_ptr<T>`

The owning handle `make_tracked` returns: the object is a root while a `unique_ptr` owns it.

- Default and `nullptr` construction; move construction and move assignment; converting move from `unique_ptr<U>`
  when `U*` converts to `T*`.
- `get()`, `operator*`, `operator->`, `explicit operator bool`, `release()`.
- Destroying or resetting a non-empty `unique_ptr` destroys the object synchronously on the calling thread (no
  finalize hook, §4.7) and the slot is reclaimed by a later cycle.
- Converting to `tracked_ptr` (constructor or assignment from `unique_ptr<U>&&`) hands the object over to the
  collector; the `unique_ptr` becomes empty.

Nothing else from `std::unique_ptr` is part of the surface.

### 3.3 `tracked_ptr<T>`

A nullable reference to a managed object, or to a subobject of one (a base class or a member).

| Member | Semantics |
|---|---|
| `tracked_ptr()`, `tracked_ptr(nullptr_t)` | null |
| `explicit tracked_ptr(U* p)`, `U*` convertible to `T*` | refers to `*p`. Preconditions in §5.8. |
| `tracked_ptr(const tracked_ptr&)`, `tracked_ptr(const tracked_ptr<U>&)` | same object; upcast when `U* → T*` converts |
| `tracked_ptr(tracked_ptr&&)`, `tracked_ptr(tracked_ptr<U>&&)` | same object; the source is null afterwards |
| `tracked_ptr(unique_ptr<U>&&)` | takes the object over from the `unique_ptr` |
| `operator=` from all of the above and from `nullptr` | same semantics as the constructors |
| `~tracked_ptr()` | drops the reference |
| `get()`, `operator*`, `operator->`, `explicit operator bool` | access; `*`/`->` on null is a precondition violation |
| `reset()` | becomes null |
| `swap(tracked_ptr&)` | exchanges referents |
| `==`, `<=>` with `tracked_ptr<U>` and with `nullptr` | compare addresses after conversion to the common pointer type |
| `std::hash<tracked_ptr<T>>` | hashes the address |

```cpp
template<class T, class U> tracked_ptr<T> dynamic_pointer_cast(const tracked_ptr<U>&);
```

`dynamic_cast` of the referent; null when the cast fails.

Every member of `tracked_ptr` and `dynamic_pointer_cast` is `noexcept` and runs no user code.

A moved-from `tracked_ptr` is null and fully usable (it can be read, compared, assigned and destroyed).

### 3.4 Managed type requirements

A *managed type* is any `T` passed to `make_tracked<T>` or used as an element/key/value of an SGCL container.

- **MUST** be nothrow-destructible.
- **MUST** follow the layout rule of §4.4: every `tracked_ptr` the type's constructors place inside the object sits at
  an offset that every construction of the type uses for a `tracked_ptr`, and a word that holds a `tracked_ptr` in one
  object of the type holds a `tracked_ptr` or zero in every object of the type, at all times. In practice: no
  `tracked_ptr` inside a `union`, a `std::variant`, or the small-buffer storage of a type-erased object (`std::function`,
  `std::any`) constructed inside a managed object.
- **MAY** declare the finalize hook (§4.7).

### 3.5 `vector<T>`

A contiguous, growable array in managed storage. `T` is a managed type (§3.4).

| Member | Notes |
|---|---|
| `vector()` | empty, no allocation |
| `vector(std::initializer_list<T>)` | |
| `vector(const vector&)`, `operator=(const vector&)` | deep copy |
| `vector(vector&&)`, `operator=(vector&&)` | steals the storage; the source is empty |
| `~vector()` | the storage and its elements become garbage (§4.8) |
| `operator[](size_t)` | unchecked; an index `>= size()` is a precondition violation |
| `front()`, `back()` | precondition: not empty |
| `data()` | pointer to the first element (null when no storage) |
| `size()`, `capacity()`, `empty()` | |
| `begin`, `end`, `cbegin`, `cend`, `rbegin`, `rend` (const and non-const) | random-access iterators |
| `reserve(n)` | no reallocation happens until `size()` exceeds `n` |
| `push_back(const T&)`, `push_back(T&&)`, `emplace_back(args...)` | the argument may alias an element of the same vector |
| `pop_back()` | precondition: not empty |
| `insert(pos, const T&)`, `insert(pos, T&&)` | returns an iterator to the inserted element; the argument may alias an element |
| `insert(pos, first, last)` | forward iterators; the range may be in the same vector |
| `erase(pos)`, `erase(first, last)` | returns an iterator after the last erased element |
| `resize(n)`, `resize(n, const T&)` | |
| `clear()` | destroys every element; keeps the storage |
| `swap(vector&)` | |

Iterators, pointers and references are invalidated as for `std::vector`. A stale iterator never refers to freed memory
while it is reachable from a root (the old storage stays alive), but its value is unspecified.

### 3.6 `unordered_map<K, V, Hash, KeyEqual>`

A hash map with separate chaining in managed storage. `K` and `V` are managed types; `Hash` and `KeyEqual` are
default-constructible, nothrow-copyable and nothrow-movable function objects whose call operators **may throw**.
`value_type` is exactly `std::pair<const K, V>`.

| Member | Notes |
|---|---|
| `unordered_map()` | empty, no allocation |
| `~unordered_map()` | nodes become garbage |
| `insert_or_assign(k, v)` (all four `const&`/`&&` combinations) | returns `std::pair<iterator, bool>`; `true` when inserted |
| `find(k)` (const and non-const) | `end()` when absent |
| `erase(k)` | returns the number of erased elements (0 or 1) |
| `size()`, `empty()` | |
| `clear()` | |
| `begin`, `end`, `cbegin`, `cend` (const and non-const) | forward iterators; `*it` is `value_type&` |

Copy and move construction and assignment are deleted.

Call counts, which are observable when `Hash`/`KeyEqual` have side effects:

- `insert_or_assign`, `find` and `erase(k)` call `Hash` exactly once on the argument key.
- `Hash` is never called on a stored key: each node keeps the hash computed when it was inserted, and a rehash reuses it.
- `KeyEqual(stored, argument)` is called only for stored keys whose hash equals the argument's hash, in chain order,
  until the first match.

Iterator stability: an iterator stays valid across `insert_or_assign` on an existing key and across `erase` of a
different element. Any insertion of a new key may rehash and invalidate all iterators. A stale iterator is memory-safe
while reachable from a root — it holds its node, and the map object it was obtained from **MUST** still be alive — but
the elements it visits afterwards are unspecified. Iteration order is unspecified.

### 3.7 `collector`

| Function | Semantics |
|---|---|
| `static bool force_collect(bool wait = false) noexcept` | Requests a collection. With `wait`, blocks until a full cycle that **started after the call** has finished: every object unreachable at the call has been finalized or destroyed, except objects kept by a finalizer during that cycle (§4.7). Returns `false` without blocking when the collector is shutting down, or when called on the collector thread (a finalize hook or a destructor); the request is still made in the latter case. Any number of threads may wait concurrently. |
| `static size_t get_live_object_count() noexcept` | Number of objects found live by the last completed cycle, including SGCL's internal allocations. Never blocks, never forces a cycle; 0 before the first cycle completes. |
| `static std::tuple<pause_guard, std::vector<void*>> get_live_objects()` | Forces a cycle and returns the addresses of every object it found live. The collector stays paused, and the addresses stay valid, until the returned `pause_guard` is destroyed. **MUST NOT** be called on the collector thread or while the calling thread holds another `pause_guard` (fatal, §5.8). |
| `static void terminate() noexcept` | Stops the collector (§4.10) and returns when it has stopped. Idempotent. **MUST NOT** be called on the collector thread (fatal, §5.8). |

### 3.8 Configuration and the process state

| Macro | Meaning |
|---|---|
| `SGCL_API` | Linkage of the three exported state functions (§4.9): `__declspec(dllexport)` in the owning image and `__declspec(dllimport)` elsewhere on Windows; default visibility on ELF/Mach-O. Required on Windows when the program has more than one image. Default: empty. |
| `SGCL_MAX_TYPES_NUMBER` | The number of distinct managed types a process may allocate (default 4096). A decimal literal. Every image **MUST** use the same value; a mismatch fails to link or load. |
| `SGCL_FINALIZE_HOOK` | The name of the finalize hook member function (default `sgcl_finalize`). Every image **MUST** use the same value. |

`sgcl/state_impl.h` defines the process state. Exactly one translation unit of exactly one image of the process **MUST**
include it. That image is the *owner*: it **MUST** stay loaded until the process exits.

## 4. Design

### 4.1 Memory layout

- **Block**: one `malloc` holding 15 *data pages* of `PageSize` (64 KiB), aligned to `PageSize`. A thread carves pages
  out of its current block; free pages go to a process-wide lock-free stack and are reused by any thread; a block whose
  15 pages are all free is returned to `free`.
- **Data page**: word 0 points to the page's **page header**; the remaining `PageSize - 8` bytes are *slots* of one
  managed type, `ObjectCount = max(1, (PageSize - 8) / sizeof(T))` of them. `Page::page_of(p)` masks any address inside
  a data page down to the page and reads word 0, so every interior pointer of a slot (a base subobject, a member) maps
  to its page and, through a fixed-point reciprocal of `sizeof(T)`, to its slot index.
- **Page header** (separate allocation): the type's `Metadata`, one atomic `State` per slot, three collector-private
  bitmaps per 64 slots (`registered`, `reachable`, `marked`) plus `finalized` (§4.7), and collector bookkeeping.
- **Large allocation**: vector storage that does not fit a data page gets its own `PageSize`-aligned allocation with
  the page-header pointer in word 0 and exactly one slot. Only addresses within its first `PageSize` bytes may be
  passed to `page_of`; SGCL only ever passes the start of the storage.
- **Metadata** (one per managed type per image, immutable once built): destroy function, finalize function, size, slot
  count, child-pointer layout (§4.4), and the type's allocator entry point.

### 4.2 Slot states

| Value | State | Meaning |
|---|---|---|
| 0 | `Used` | allocated and handed over; live only if traced |
| 1 | `Reachable` | the write barrier stored a pointer to it during the current cycle; protected for the cycle |
| 2 | `UniqueLock` | owned by a `unique_ptr`, or it is an external-root cell (§4.3); a root |
| 3 | `Constructing` | its constructor is running; protected |
| 4 | `Destroyed` | destroyed synchronously (`unique_ptr` reset, external cell dropped); awaiting reclamation |
| 8 | `BadAlloc` | its constructor threw; awaiting reclamation; never destroyed |
| 16 | `Reserved` | handed to a thread's free-slot pool, not allocated |
| 32 | `Unused` | free |

`ReachableMask = 3` (Reachable, UniqueLock, Constructing: protected). `CreatedMask = 15` (0..8: the collector
registers the slot).

Transitions; *M* is the allocating or storing mutator thread, *C* the collector:

| From → To | By | When |
|---|---|---|
| (new page) → `Reserved` | M | page creation |
| `Unused` → `Reserved` | M | the page is taken from the shared free-page list and its free slots refilled into the thread's pool |
| `Reserved` → `Constructing` | M | `make_tracked`, before the constructor |
| `Constructing` → `UniqueLock` | M | the constructor returned |
| `Constructing` → `BadAlloc` | M | the constructor threw |
| `UniqueLock`, `Used`, `Reachable` → `Reachable` | M | write barrier: a non-null pointer to the slot was stored into any `tracked_ptr` |
| `Constructing` → `Constructing` | M | write barrier during construction (an alias of `this`): ownership is not handed over |
| `BadAlloc`, `Destroyed` → unchanged | M | write barrier on a dead slot (a pointer that leaked out of a failed constructor): ignored, see §5.1 |
| `Reachable` → `Used` | C | start of each cycle (§4.6 step 3) |
| `UniqueLock` → `Destroyed` | M | `unique_ptr` destruction or reset; external-root cell dropped |
| `Used` → `Unused` | C | swept: finalized (if it has a hook), children cleared, destroyed |
| `Destroyed`, `BadAlloc` → `Unused` | C | swept without running a destructor |
| `Reserved` → `Unused` | M | a thread exits and returns its unused pool slots |

No other transition exists. A slot in `ReachableMask` is never destroyed.

### 4.3 Roots and storage classes

Every `tracked_ptr` holds a *cell*, an atomic word containing the referent's address. Where the cell lives depends on
where the `tracked_ptr` object itself lives, decided once when it is constructed:

1. **Stack root**: the `tracked_ptr` is within `MaxOffsetForStackDetection` (4 KiB) of the current stack pointer. Its
   cell is the slot of a per-thread *shadow stack* at the `tracked_ptr`'s distance from the thread's stack base. The
   collector scans every shadow stack once per cycle.
2. **Child**: the `tracked_ptr` is constructed inside the managed allocation currently being constructed on this
   thread (between `Constructing` and the end of the constructor; for vector elements, the element being constructed)
   at an offset recorded in the type's child layout (§4.4). The cell is the `tracked_ptr` itself. It is not a root;
   the collector reaches it by tracing its parent.
3. **External root**: anything else, including a `tracked_ptr` constructed inside a managed allocation at an offset the
   layout does not record (§4.4). The cell is a small managed allocation in state `UniqueLock`, so the referent stays
   alive while the `tracked_ptr` exists. Cycles through external roots are never collected.

Raw pointers, references and `this` are not roots.

### 4.4 Child layout discovery

The collector needs, for each managed type, the offsets of the `tracked_ptr` cells inside it. They are discovered from
the first construction of the type:

- Before the first successful construction, the storage is filled with the sentinel word `1` and the thread records the
  type's layout map. Each child `tracked_ptr` constructed in the storage finds the sentinel at its address, records its
  offset in the map and becomes null. When the constructor returns, the layout is published as final (release).
- Every later construction zero-fills the storage first. A child `tracked_ptr` whose offset is in the final layout is
  a child; one whose offset is not becomes an external root (§4.3), so a conditional member is never left untraced.
- If the discovering constructor throws, the layout is not published and the next construction discovers again; the
  storage of the failed construction is zero-filled before its slot can be reused.

Consequence (the layout rule of §3.4): the collector reads every recorded offset of every live object of the type as a
`tracked_ptr` cell. A word at a recorded offset that holds anything other than a cell or zero (a `variant`/`union`
alternative, small-buffer storage of a different callable) is read as a pointer, which is undefined behaviour.

### 4.5 Write barrier

Storing a non-null pointer into any cell (construction, assignment, `swap`) is a release store of the cell followed by
`set_state<Reachable>` on the referent's slot (§4.2) and a release store of the page's `state_updated` flag. Storing
null needs no barrier. Together with the stack scan this is an *insertion* barrier: an object that becomes reachable
during a cycle is shaded and survives the cycle. Objects that become unreachable during a cycle may survive it
(floating garbage) and are collected by the next one.

### 4.6 The collection cycle

The collector thread repeats:

1. **Register threads and pages**: link newly created thread records and pages (acquire loads of the lock-free lists).
   Drop the records of threads that exited; their pages stay and keep being collected.
2. **Register objects**: on each page whose `object_created` flag is set, set the `registered` bit of every slot whose
   state is in `CreatedMask`. Unregistered slots are never candidates for sweeping.
3. **Start the epoch**: clear `reachable` and `marked`; on pages with `state_updated` set, demote `Reachable` to `Used`.
4. **Scan stack roots**: mark every non-null cell of every live thread's shadow stack.
5. **Mark to a fixpoint**: drain gray slots, tracing their children through the type's layout (for vector storage,
   every element up to `capacity`, relying on unconstructed elements being zero). Then rescan pages for slots that
   became protected (`ReachableMask`) concurrently, registering late threads, pages and objects, until nothing new is
   gray. Pages left with unmarked, unprotected slots are the *unreachable pages*.
6. **Publish the live count** of this cycle (§3.7).
7. **Finalize** (§4.7).
8. **Resurrection pass**: mark everything a finalizer (or a mutator) made reachable during step 7, to a fixpoint.
9. **Sweep**: on each unreachable page, for each registered, unmarked slot: if its state is in `ReachableMask`, keep it
   (defence in depth); if `Used`, null its children, then destroy it; then set it `Unused` (release) and clear its
   `registered` and `finalized` bits.
10. **Release pages**: pages more than half free go back to their type's shared free-page list; pages and blocks that
    are entirely free are returned to the system (deletion runs on a helper thread).
11. **Handshakes**: complete forced collections (§4.10) and pause for `get_live_objects`.
12. **Sleep** until woken by allocation pressure, a forced collection or termination, or for at most `LongSleepTime`
    (30 s; `ShortSleepTime`, 3 s, after a cycle that freed much memory).

Children are nulled before destructors run, so a destructor of a managed object sees null `tracked_ptr` members and
can never touch another dead object. Destructors and finalize hooks run on the collector thread, concurrently with the
mutators.

### 4.7 Finalization

A managed class type that declares, or inherits, a member function `void SGCL_FINALIZE_HOOK() noexcept` (§3.8) has a
*finalize hook*. The hook **MUST** be `noexcept` (rejected at compile time otherwise).

- In step 7 of a cycle, the hook runs for every unreachable object in state `Used` whose `finalized` bit is clear, and
  sets the bit. All hooks of the cycle run before any child is nulled or any destructor runs, so a hook reads an intact
  object graph even when its children are garbage too.
- A hook may read the object and its children, allocate, store references anywhere (making objects reachable again),
  and call `force_collect` (non-blocking on the collector thread, §3.7).
- **At most once**: an object whose hook ran and that was made reachable again is not finalized again when it becomes
  unreachable later; it is then destroyed without a hook. The bit is reset when the slot is reclaimed.
- Objects made reachable during step 7, and everything they reach, survive the cycle (step 8).
- The order of hooks within a cycle is unspecified.
- No hook runs for an object destroyed through `unique_ptr`, for vector elements, or for objects still reachable when
  the collector stops.

### 4.8 Containers

**`vector<T>`** holds a `tracked_ptr` to an array allocation `{metadata, size, capacity, elements}`. Elements are
constructed in place (child discovery applies per element); slots in `[size, capacity)` are zero. Growth allocates a
new array, moves (or copies, §6) the elements and drops the old array, which the collector reclaims later with its
moved-from elements. Element destructors of discarded storage run on the collector thread; `clear`, `pop_back`,
`erase` and `resize` destroy elements immediately on the calling thread.

**`unordered_map`** holds a `vector<tracked_ptr<Node>>` of buckets and a size; a `Node` is `{value_type, hash, next}`
in its own managed allocation. Maximum load factor 0.75, at least 8 buckets once non-empty; a rehash relinks the
existing nodes using their stored hashes and never allocates a node.

### 4.9 Process state

One `ProcessState` per process (thread registry, free-page stack, type counter, memory counters, collector flags), one
`Collector`, and one `Thread` record per thread, all owned by the image that includes `sgcl/state_impl.h` and reached by
every image through three exported functions whose names embed `SGCL_MAX_TYPES_NUMBER`. `ProcessState` is
constant-initialized and trivially destructible, so it is usable before and after static initialization of any image.
The collector object is immortal. Per-type data (metadata, allocator entry points, child layout) is per image; the
collector reaches it through each page's header, so images need not merge their template statics. Type indices come
from the process-wide counter.

A thread's record is created on its first use of SGCL and destroyed at thread exit; its pages outlive it. No thread
exit and no static destructor stops the collector.

### 4.10 Forced collection and termination

- A forced collection increments a request generation. The collector finishes its current cycle, then runs one full
  cycle, and publishes that cycle's generation as completed. A waiter blocks until the completed generation reaches
  the one it requested, so concurrent waiters never miss a wake-up.
- `terminate()` sets the terminating flag, wakes the collector and blocks until it has stopped. The collector keeps
  cycling until a cycle reclaims nothing, at most five more times while live objects remain, then stops. Objects still
  reachable then are never finalized or destroyed. After termination, allocation keeps working and nothing is
  collected; the collector cannot be restarted.

## 5. Behaviour per situation

### 5.1 Construction

| Situation | Behaviour |
|---|---|
| `T`'s constructor returns | slot `Constructing` → `UniqueLock`; `make_tracked` returns the owning `unique_ptr` |
| `T`'s constructor throws | slot → `BadAlloc`, the exception propagates unchanged; no destructor runs for the object (its members were destroyed by the language); the slot is reclaimed by a later cycle |
| `T`'s constructor stores `this` into a `tracked_ptr` (anywhere) and returns | the object stays `Constructing` until the constructor returns, then is owned by the `unique_ptr` as usual |
| `T`'s constructor stores `this` and then throws | the leaked `tracked_ptr` refers to a dead slot. The write barrier never revives a `BadAlloc` slot, so the slot is reclaimed; using the leaked pointer afterwards is the program's use-after-destruction, as in plain C++ |
| The first constructor of a type throws during layout discovery | discovery restarts on the next construction (§4.4) |

### 5.2 `vector`

| Operation | Throw from | Guarantee |
|---|---|---|
| `push_back`, `emplace_back` | element construction, or element move/copy during growth | **strong** (growth moves with `std::move_if_noexcept`; the new element is constructed before the old storage is abandoned) |
| `insert(pos, v)` | copying `v`, shifting elements | **strong** when `T` is nothrow-move-constructible and nothrow-move-assignable, else **basic** |
| `insert(pos, first, last)` | copying the range, shifting | **strong** when `T` is nothrow-move-constructible and nothrow-move-assignable, else **basic** |
| `erase` | element move assignment | **basic**; `noexcept` when `T` is nothrow-move-assignable |
| `reserve`, copy construction, copy assignment, `vector(initializer_list)` | element copy/move | **strong** |
| `resize(n)`, `resize(n, v)` | element construction | **strong** |
| `clear`, `pop_back`, `~vector`, move operations, `swap` | — | `noexcept` |

*Basic* means: no element is leaked or destroyed twice, every element in `[0, size())` is a valid object (possibly in
a moved-from state), and the vector stays usable. A failed operation never leaves a never-constructed object inside
`[0, size())` or a live object outside it.

### 5.3 `unordered_map`

| Operation | Throw from | Guarantee |
|---|---|---|
| `insert_or_assign` | `Hash`, `KeyEqual`, `K`/`V` construction, `V` assignment | **strong** for `Hash`, `KeyEqual` and node construction (the map is unchanged); `V` assignment to an existing key: **basic** (the element is in whatever state `V`'s assignment left it) |
| `find` | `Hash`, `KeyEqual` | no effect |
| `erase(k)` | `Hash`, `KeyEqual` | **strong** |
| rehash (inside an insertion) | — | never throws: it uses stored hashes |
| `clear`, `erase(iterator)`, iteration, `size`, `~unordered_map` | — | `noexcept` |

### 5.4 Finalize hooks and destructors

Both run on the collector thread. Both are `noexcept` by requirement (§3.4, §4.7), so neither can throw into SGCL. A
type whose destructor or hook is not `noexcept` is rejected at compile time.

### 5.5 Threads

| Situation | Behaviour |
|---|---|
| A thread's first SGCL use | its record (shadow stack, allocators) is created; the thread is registered with the collector |
| A thread exits | its record is marked dead; unused pool slots return to `Unused`; its pages stay and are collected normally |
| A detached thread still runs after `terminate()` | it keeps working; nothing is collected any more |
| The collector thread allocates or stores pointers (in a hook or destructor) | it is a regular SGCL thread for that purpose |

### 5.6 Collector-thread calls

| Call on the collector thread | Behaviour |
|---|---|
| `force_collect(false)` | requests a collection, returns `true` |
| `force_collect(true)` | requests a collection, returns `false` without blocking |
| `get_live_object_count()` | normal |
| `get_live_objects()`, `terminate()` | fatal diagnostic (they would wait for the thread itself) |

### 5.7 Out of memory and size overflow

Out of memory anywhere in SGCL — `make_tracked`, container growth, thread registration, external-root cells, page
headers, the collector's own bookkeeping — and a container size whose byte count overflows are **fatal**: SGCL writes
one line `sgcl: fatal: <reason>` to `stderr` and calls `std::abort()`. SGCL never throws `std::bad_alloc` or
`std::length_error`, and never relies on a `noexcept` boundary to terminate for it.

### 5.8 Misuse

Each of these is detected in every build mode (not only with assertions enabled), reported with a fatal diagnostic
(§5.7), and aborts:

| Misuse | Detection |
|---|---|
| `tracked_ptr(U* p)` where `p` points into SGCL storage whose slot is not live (`Unused`, `Reserved`, `Destroyed`, `BadAlloc`) | slot state check |
| `tracked_ptr(U* p)` where `*p` is still owned by a `unique_ptr` (`UniqueLock`) | slot state check |
| More than `SGCL_MAX_TYPES_NUMBER` managed types | type counter |
| A stack root deeper than `MaxStackSize` from its thread's stack base | shadow-stack index |
| `get_live_objects()` or `terminate()` on the collector thread | thread identity |
| `get_live_objects()` while the calling thread holds a `pause_guard` | thread identity |

These are not detected and are undefined behaviour: dereferencing a null `tracked_ptr`, `vector::operator[]` out of
range, `front`/`back`/`pop_back` on an empty vector, a data race on one non-atomic `tracked_ptr` or container, a type
breaking the layout rule (§3.4), passing a pointer that does not point into SGCL storage at all to `tracked_ptr(U*)`
(it may point anywhere; only addresses inside SGCL data pages are checked), and using an object after its constructor
threw.

## 6. Exception-safety contract

- A function of SGCL is `noexcept` exactly when it runs no user code that may throw. User code is: constructors and
  assignment operators of `T`/`K`/`V`, `Hash` and `KeyEqual` calls, and iterator operations of a range passed to
  `insert`. Where it depends on `T`, the function is conditionally `noexcept`.
- Destructors of managed types and finalize hooks are required to be `noexcept`, so destroying an object never throws.
- Out of memory is not an exception in SGCL (§5.7), so a function that only allocates is `noexcept`.
- Every function that may throw states its guarantee in §5; the strong guarantee is given wherever `std::vector` or
  `std::unordered_map` gives it.
- An exception never leaves SGCL's state inconsistent: no slot stuck in `Constructing`, no lock held, no counter off,
  no bucket chain corrupted.

## 7. Concurrency and memory ordering

Data-race freedom: one `tracked_ptr`, one `vector` and one `unordered_map` are not thread-safe objects. Concurrent
access to the same one from several threads, with at least one writer, **MUST** be synchronized by the program (a
mutex). Different objects may be used concurrently without synchronization; the collector itself runs concurrently with
everything.

Each handoff between a mutator and the collector is a release store paired with an acquire load:

| Publication (mutator, release) | Observation (collector, acquire) |
|---|---|
| cell store (`Pointer::store`) | child-field loads while tracing; shadow-stack slot loads |
| shadow-stack page zeroed, then `is_used` | `is_used` before reading the page |
| slot state set, then `object_created` and `state_updated` | `object_created` before registering; `state_updated` before reading states |
| new page pushed on the thread's page list | page-list head load |
| new thread record pushed (CAS) | thread-list head load |
| thread record `is_deleted` | `is_deleted` before dropping the record |
| child layout `final` | `final` before compacting the layout |
| array `metadata` after the elements are zeroed | `metadata` before tracing the elements |
| pool slots returned at thread exit (`Unused` store, `unused_atomic` add) | page release accounting |

| Publication (collector, release) | Observation (mutator, acquire) |
|---|---|
| slot set `Unused` after destruction | `Unused` before reusing the slot |
| page taken off / put on the free-page list | `on_empty_list` |

The lock-free stacks (threads, pages, free pages) are push-only from mutators except the free-page stack, whose pops
are protected against ABA (§9 tracks the remaining review item).

## 8. Limits

| Limit | Value |
|---|---|
| Size of one object passed to `make_tracked` | about 64 KiB (`PageSize - 8` minus the array header); compile-time error beyond |
| Number of managed types per process | `SGCL_MAX_TYPES_NUMBER` (default 4096); fatal beyond |
| Depth of a thread's stack that holds `tracked_ptr`s | `MaxStackSize` (8 MiB) from the stack base; fatal beyond |
| Addresses on x86-64 | user heap and stack addresses have the two highest bits clear (they carry storage-class tags) |
| Stack switching | fibers, coroutines on separate stacks, `longjmp` across `tracked_ptr`s: unsupported |
| Cycles through external roots | never collected |
| Collector restarts | none: once terminated, the collector stays stopped |

## 9. Open design questions

- **Free-page stack ABA.** The pops from the shared free-page stack and the per-type page buffer use a single-word
  compare-and-swap; the audit must either show that no page can be popped, reused and pushed back between the load and
  the CAS, or add a tag.
- **Hash caching cost.** Each node stores its hash (one word). This trades memory for never calling `Hash` on stored
  keys; revisit if node size matters.
- **Shadow-stack size.** `MaxStackSize` is fixed at 8 MiB per thread of virtual address space; deriving it from each
  thread's real stack size would lift the depth limit.
