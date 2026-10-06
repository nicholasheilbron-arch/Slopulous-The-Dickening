// Minimal stand-in for Unreal's CoreMinimal.h so the engine-independent world generator
// can be compiled and tested outside the engine. Only the API surface the generator uses.
#pragma once
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>
#include <utility>

typedef int32_t int32; typedef uint32_t uint32; typedef uint8_t uint8; typedef uint64_t uint64; typedef int64_t int64;
#define SHAMAN_API
#define USTRUCT(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UMETA(...)
#define GENERATED_BODY()
#ifndef PI
#define PI (3.1415926535897932f)
#endif

template<typename T> struct TArray
{
	std::vector<T> V;
	int32 Num() const { return (int32)V.size(); }
	int32 Add(const T& X) { V.push_back(X); return Num() - 1; }
	T& operator[](int32 I) { return V[(size_t)I]; }
	const T& operator[](int32 I) const { return V[(size_t)I]; }
	void Init(const T& X, int32 N) { V.assign((size_t)N, X); }
	void Reset() { V.clear(); }
	void RemoveAt(int32 I) { V.erase(V.begin() + I); }
	template<typename P> void Sort(P Pred) { std::sort(V.begin(), V.end(), Pred); }
	bool operator==(const TArray& O) const { return V == O.V; }
};

struct FVector2D
{
	float X, Y;
	FVector2D() : X(0), Y(0) {}
	FVector2D(float InX, float InY) : X(InX), Y(InY) {}
	FVector2D operator+(const FVector2D& O) const { return FVector2D(X + O.X, Y + O.Y); }
	FVector2D operator-(const FVector2D& O) const { return FVector2D(X - O.X, Y - O.Y); }
	FVector2D operator*(float S) const { return FVector2D(X * S, Y * S); }
	bool operator==(const FVector2D& O) const { return X == O.X && Y == O.Y; }
	float Size() const { return std::sqrt(X * X + Y * Y); }
	FVector2D GetSafeNormal() const { float S = Size(); return S > 1e-8f ? FVector2D(X / S, Y / S) : FVector2D(); }
	static float Distance(const FVector2D& A, const FVector2D& B) { return (A - B).Size(); }
	static float DistSquared(const FVector2D& A, const FVector2D& B) { FVector2D D = A - B; return D.X * D.X + D.Y * D.Y; }
};

struct FMath
{
	template<typename T> static T Clamp(T X, T A, T B) { return X < A ? A : (X > B ? B : X); }
	template<typename T> static T Min(T A, T B) { return A < B ? A : B; }
	template<typename T> static T Max(T A, T B) { return A > B ? A : B; }
	static float Abs(float X) { return std::fabs(X); }
	static float Sqrt(float X) { return std::sqrt(X); }
	static float Sin(float X) { return std::sin(X); }
	static float Cos(float X) { return std::cos(X); }
	static float Atan2(float Y, float X) { return std::atan2(Y, X); }
	static int32 FloorToInt(float X) { return (int32)std::floor(X); }
	static float Lerp(float A, float B, float T) { return A + (B - A) * T; }
	template<typename T> static T Square(T X) { return X * X; }
};
