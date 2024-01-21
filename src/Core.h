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

// Helper to get the size of C arrays.
template<typename taType, size_t taArraySize>
constexpr size_t gElemCount(const taType (&inArray)[taArraySize]) { return taArraySize; }

// Helper to check if a value is present in a vector-like container.
template<typename taValue, typename taContainer>
constexpr bool gContains(const taContainer& ioContainer, const taValue& inElem)
{
	for (auto& elem : ioContainer)
		if (elem == inElem)
			return true;

	return false;
}

// Helper to add a value to a vector-like container only if it's not already in it.
template<typename taValue, typename taContainer>
constexpr bool gPushBackUnique(taContainer& ioContainer, const taValue& inElem)
{
	if (gContains(ioContainer, inElem))
		return false;

	ioContainer.push_back(inElem);
	return true;
}


// Lower bound implementation to avoid <algorithm>
template<typename taIterator, typename taValue>
constexpr taIterator gLowerBound(taIterator inFirst, taIterator inLast, const taValue& inElem)
{
	auto first = inFirst;
	auto count = inLast - first;

    while (count > 0) 
	{
		auto count2 = count / 2;
		auto mid    = first + count2;

		if (*mid < inElem)
		{
			first = mid + 1;
			count -= count2 + 1;
		}
		else
		{
			count = count2;
		}
    }

	return first;
}


// Helper to find a value in a sorted vector-like container.
template<typename taValue, typename taContainer>
constexpr auto gFindSorted(taContainer& inContainer, const taValue& inElem)
{
	auto end = inContainer.end();
	auto it = gLowerBound(inContainer.begin(), end, inElem);

	if (it != end && *it == inElem)
		return it;
	else
		return end;
}


// Helper to insert a value in a sorted vector-like container.
template<typename taValue, typename taContainer>
constexpr auto gEmplaceSorted(taContainer& ioContainer, const taValue& inElem)
{
	auto end = ioContainer.end();
	auto it = gLowerBound(ioContainer.begin(), end, inElem);

	if (it != end && *it == inElem)
		return it;
	else
		return ioContainer.emplace(it, inElem);
}

// Helper to find a value in a vector-like container.
template<typename taValue, typename taContainer>
constexpr auto gFind(taContainer& inContainer, const taValue& inElem)
{
	auto end = inContainer.end();
	auto begin = inContainer.begin();

	for (auto it = begin; it != end; ++it)
	{
		if (*it == inElem)
			return it;
	}

	return end;
}


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

// TODO replace all uses of SegmentedVector by a virtual mem array to make it safe to access with multiple thread while it grows
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

// Forward declaration from Ticks.h
int64  gGetTickCount();

// Simple random function.
inline uint32 gRand32(uint32 inSeed = 0)
{
	if (inSeed == 0)
		inSeed = gGetTickCount();

	// Equivalent to std::minstd_rand
	constexpr uint32 cMul = 48271;
	constexpr uint32 cMod = 2147483647;
	return inSeed * cMul % cMod;
}

constexpr bool   gIsPow2(uint64 inValue)							{ return inValue != 0 && (inValue & (inValue - 1)) == 0; }
constexpr uint64 gAlignUp(uint64 inValue, uint64 inPow2Alignment)	{ return (inValue + (inPow2Alignment - 1)) & ~(inPow2Alignment - 1); }
constexpr uint64 gAlignDown(uint64 inValue, uint64 inPow2Alignment) { return inValue & ~(inPow2Alignment - 1); }

// Forward declarations of std types we don't want to include here.
namespace std
{
template <class T, size_t Extent> class span;
template <class T> class optional;
template <class T> class reference_wrapper;
}

// Typedef for Span, until we have a custom version.
template <typename taType>
using Span = std::span<taType, -1>;

// Typedef for Optional, until we have a custom version.
template <typename taType>
using Optional = std::optional<taType>;
template <typename taType>
using OptionalRef = std::optional<std::reference_wrapper<taType>>;