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