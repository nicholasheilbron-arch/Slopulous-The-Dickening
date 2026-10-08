// Minimal Unreal shim so engine-independent SHAMAN terrain code can be compiled and tested with g++.
// NOT used by the game build.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
typedef int32_t int32; typedef uint32_t uint32; typedef uint8_t uint8; typedef uint64_t uint64; typedef int64_t int64;
#define SHAMAN_API
#define UENUM(...)
#define USTRUCT(...)
#define UPROPERTY(...)
#define GENERATED_BODY()
#define TEXT(x) x
struct FName { std::string S; FName() {} FName(const char* s) : S(s) {} bool IsNone() const { return S.empty(); } };
struct FMath {
	template<class T> static T Abs(T v) { return v < 0 ? -v : v; }
	template<class T> static T Clamp(T v, T a, T b) { return v < a ? a : (v > b ? b : v); }
	template<class T> static T Max(T a, T b) { return a > b ? a : b; }
	template<class T> static T Min(T a, T b) { return a < b ? a : b; }
	template<class T, class U> static T Lerp(T a, T b, U t) { return (T)(a + (b - a) * t); }
	static float Cos(float v) { return std::cos(v); }
	static float Sin(float v) { return std::sin(v); }
	static float Acos(float v) { return std::acos(v); }
	static float Atan2(float y, float x) { return std::atan2(y, x); }
	static float Sqrt(float v) { return std::sqrt(v); }
	static int32 FloorToInt(float v) { return (int32)std::floor(v); }
	static bool IsFinite(float v) { return std::isfinite(v); }
	static float DegreesToRadians(float d) { return d * 3.14159265358979f / 180.f; }
};
#ifndef PI
#define PI 3.14159265358979f
#endif
struct FVector {
	float X = 0, Y = 0, Z = 0;
	FVector() {} FVector(float x, float y, float z) : X(x), Y(y), Z(z) {} explicit FVector(float v) : X(v), Y(v), Z(v) {}
	FVector operator+(const FVector& o) const { return {X + o.X, Y + o.Y, Z + o.Z}; }
	FVector operator-(const FVector& o) const { return {X - o.X, Y - o.Y, Z - o.Z}; }
	FVector operator-() const { return {-X, -Y, -Z}; }
	FVector operator*(float s) const { return {X * s, Y * s, Z * s}; }
	FVector operator/(float s) const { return {X / s, Y / s, Z / s}; }
	FVector& operator+=(const FVector& o) { X += o.X; Y += o.Y; Z += o.Z; return *this; }
	FVector& operator-=(const FVector& o) { X -= o.X; Y -= o.Y; Z -= o.Z; return *this; }
	FVector& operator*=(float s) { X *= s; Y *= s; Z *= s; return *this; }
	bool operator==(const FVector& o) const { return X == o.X && Y == o.Y && Z == o.Z; }
	float Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
	bool IsZero() const { return X == 0 && Y == 0 && Z == 0; }
	float SizeSquared() const { return X * X + Y * Y + Z * Z; }
	FVector GetSafeNormal() const { float s = Size(); return s > 1e-8f ? *this / s : FVector(); }
	static float DotProduct(const FVector& a, const FVector& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
	static FVector CrossProduct(const FVector& a, const FVector& b) { return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X}; }
	static float Dist(const FVector& a, const FVector& b) { return (a - b).Size(); }
};
inline FVector operator*(float s, const FVector& v) { return v * s; }
template<int N> struct TInlineAllocator {};
template<class T, class Alloc = void> struct TArray {
	std::vector<T> V;
	void SetNum(int32 n) { V.resize(n); }
	int32 Num() const { return (int32)V.size(); }
	T& operator[](int32 i) { return V[i]; }
	const T& operator[](int32 i) const { return V[i]; }
	void Add(const T& t) { V.push_back(t); }
	void Reset() { V.clear(); }
};
