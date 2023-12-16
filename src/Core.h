#pragma once


#define breakpoint __debugbreak()


#ifdef ASSERTS_ENABLED
#define gAssert(condition) do { if (!(condition)) breakpoint; } while(0)
#else
#define gAssert(condition) do { (void)sizeof(condition); } while(0)
#endif


template <typename T> constexpr T gMin(T inA, T inB)				{ return inA < inB ? inA : inB; }
template <typename T> constexpr T gMax(T inA, T inB)				{ return inB < inA ? inA : inB; }
template <typename T> constexpr T gClamp(T inV, T inLow, T inHigh)	{ return (inV < inLow) ? inLow : (inHigh < inV) ? inHigh : inV; }

using int8   = signed char;
using uint8  = unsigned char;
using int16  = signed short;
using uint16 = unsigned short;
using int32  = signed long;
using uint32 = unsigned long;
using int64  = signed long long;
using uint64 = unsigned long long;


#include "ankerl/unordered_dense.h"

// These namespaces are too long, add shortcuts.
template <  class Key,
			class T,
			class Hash =  ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<std::pair<Key, T>>,
			class Bucket =  ankerl::unordered_dense::bucket_type::standard>
using hash_map = ankerl::unordered_dense::map<Key, T, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <  class Key,
			class T,
			class Hash = ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<std::pair<Key, T>>,
			class Bucket = ankerl::unordered_dense::bucket_type::standard>
using segmented_hash_map = ankerl::unordered_dense::segmented_map<Key, T, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <  class Key,
			class Hash = ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<Key>,
			class Bucket = ankerl::unordered_dense::bucket_type::standard>
using set = ankerl::unordered_dense::set<Key, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <  class Key,
			class Hash = ankerl::unordered_dense::hash<Key>,
			class KeyEqual = std::equal_to<Key>,
			class AllocatorOrContainer = std::allocator<Key>,
			class Bucket = ankerl::unordered_dense::bucket_type::standard>
using segmented_hash_set = ankerl::unordered_dense::segmented_set<Key, Hash, KeyEqual, AllocatorOrContainer, Bucket>;

template <	typename T,
			typename Allocator = std::allocator<T>,
			size_t MaxSegmentSizeBytes = 4096>
using segmented_vector = ankerl::unordered_dense::segmented_vector<T, Allocator, MaxSegmentSizeBytes>;