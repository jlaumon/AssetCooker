#pragma once

// Break to the debugger (or crash if no debugger is attached).
#define breakpoint __debugbreak()

// Assert macro.
#ifdef ASSERTS_ENABLED
#define gAssert(condition) do { if (!(condition)) breakpoint; } while(0)
#else
#define gAssert(condition) do { (void)sizeof(condition); } while(0)
#endif

// Preprocessor utilities.
#define TOKEN_PASTE1(x, y) x ## y
#define TOKEN_PASTE(x, y) TOKEN_PASTE1(x, y)

// Defer execution of a block of code to the end of the scope.
// eg. defer { delete ptr; };
struct DeferDummy {};
template <class F> struct Deferrer { F f; ~Deferrer() { f(); } };
template <class F> Deferrer<F> operator*(DeferDummy, F f) { return {f}; }
#define defer auto TOKEN_PASTE(deferred, __LINE__) = DeferDummy{} *[&]()

// Inherit to disallow copies.
struct NoCopy
{
	NoCopy()                         = default;
	~NoCopy()                        = default;
	NoCopy(NoCopy&&)                 = default;
	NoCopy& operator=(NoCopy&&)      = default;

	NoCopy(const NoCopy&)            = delete;
	NoCopy& operator=(const NoCopy&) = delete;
};

// Basic types.
using int8   = signed char;
using uint8  = unsigned char;
using int16  = signed short;
using uint16 = unsigned short;
using int32  = signed long;
using uint32 = unsigned long;
using int64  = signed long long;
using uint64 = unsigned long long;

// Litterals for memory sizes.
constexpr size_t operator ""_B(size_t inValue)	 { return inValue; }
constexpr size_t operator ""_KiB(size_t inValue) { return inValue * 1024; }
constexpr size_t operator ""_MiB(size_t inValue) { return inValue * 1024 * 1024; }
constexpr size_t operator ""_GiB(size_t inValue) { return inValue * 1024 * 1024 * 1024; }

// Basic functions.
template <typename T> constexpr T gMin(T inA, T inB)				{ return inA < inB ? inA : inB; }
template <typename T> constexpr T gMax(T inA, T inB)				{ return inB < inA ? inA : inB; }
template <typename T> constexpr T gClamp(T inV, T inLow, T inHigh)	{ return (inV < inLow) ? inLow : (inHigh < inV) ? inHigh : inV; }

template<typename taType, size_t taArraySize>
constexpr size_t gElemCount(const taType (&inArray)[taArraySize]) { return taArraySize; }

// Basic containers.
#include "ankerl/unordered_dense.h"

// These namespaces are too long, add shortcuts.
template <  class Key,
			class T,
			class Hash =  ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<std::pair<Key, T>>,
			class Bucket =  ankerl::unordered_dense::bucket_type::standard>
using HashMap = ankerl::unordered_dense::map<Key, T, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <  class Key,
			class T,
			class Hash = ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<std::pair<Key, T>>,
			class Bucket = ankerl::unordered_dense::bucket_type::standard>
using SegmentedHashMap = ankerl::unordered_dense::segmented_map<Key, T, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <  class Key,
			class Hash = ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<Key>,
			class Bucket = ankerl::unordered_dense::bucket_type::standard>
using HashSet = ankerl::unordered_dense::set<Key, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <  class Key,
			class Hash = ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<Key>,
			class Bucket = ankerl::unordered_dense::bucket_type::standard>
using SegmentedHashSet = ankerl::unordered_dense::segmented_set<Key, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <	typename T,
			size_t MaxSegmentSizeElements = 1024,
			typename Allocator = std::allocator<T>>
using SegmentedVector = ankerl::unordered_dense::segmented_vector<T, Allocator, MaxSegmentSizeElements * sizeof(T)>;

// Hash helper to hash entire structs.
// Only use on structs/classes that don't contain any padding.
template <typename taType>
struct MemoryHasher
{
	using is_avalanching = void; // mark class as high quality avalanching hash

    uint64 operator()(const taType& inValue) const noexcept
	{
		static_assert(std::has_unique_object_representations_v<taType>);
        return ankerl::unordered_dense::detail::wyhash::hash(&inValue, sizeof(inValue));
    }
};

