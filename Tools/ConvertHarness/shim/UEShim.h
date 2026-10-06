// Minimal single-threaded stand-in for the Unreal types used by the Convert logic (tribe registry, tribe roster,
// unit identity, ConvertUnits effect). Lets that logic run outside the engine. Not a substitute for compiling in UE4.
#pragma once
#include <cstdint>
#include <cmath>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <functional>
#include <type_traits>

typedef int32_t int32; typedef uint8_t uint8; typedef uint32_t uint32;
#define SHAMAN_API
#define UCLASS(...)
#define USTRUCT(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define UMETA(...)
#define GENERATED_BODY()
#define TEXT(x) x
#define UE_LOG(...) ((void)0)
#define DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(N, ...) struct N { template<class... A> void Broadcast(A&&...) const { ++Calls; } mutable int Calls = 0; };
#define DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(N, ...) struct N { template<class... A> void Broadcast(A&&...) const { ++Calls; } mutable int Calls = 0; };

struct FMath { template<class T> static T Max(T a, T b) { return a > b ? a : b; } template<class T> static T Min(T a, T b) { return a < b ? a : b; } };
struct FName { std::string S; FName() {} FName(const char* s) : S(s) {} bool operator==(const FName& o) const { return S == o.S; } std::string ToString() const { return S; } };
struct FText {};
struct FString { std::string S; const char* operator*() const { return S.c_str(); } };
struct FVector
{
	float X = 0, Y = 0, Z = 0;
	FVector() {} FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}
	FVector operator+(const FVector& o) const { return FVector(X + o.X, Y + o.Y, Z + o.Z); }
	static float DistSquared(const FVector& a, const FVector& b) { float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z; return dx * dx + dy * dy + dz * dz; }
	static const FVector ZeroVector;
};
inline const FVector FVector::ZeroVector = FVector();
struct FTransform { FVector L; FTransform() {} explicit FTransform(const FVector& v) : L(v) {} };

class UWorld;
class UObject
{
public:
	bool bAlive = true;
	virtual ~UObject() {}
	virtual UWorld* GetWorld() const { return nullptr; }
};
template<class T> FString GetNameSafe(const T*) { return FString(); }

template<class T> struct TWeakObjectPtr
{
	T* P = nullptr;
	TWeakObjectPtr() {}
	TWeakObjectPtr(T* p) : P(p) {}
	T* Get() const { return (P && P->bAlive) ? P : nullptr; }
	bool IsValid() const { return Get() != nullptr; }
	T* operator->() const { return Get(); }
	bool operator==(const TWeakObjectPtr& o) const { return P == o.P; }
};

template<class T> struct TArray
{
	std::vector<T> V;
	int32 Num() const { return (int32)V.size(); }
	void Add(const T& x) { V.push_back(x); }
	void AddUnique(const T& x) { if (std::find(V.begin(), V.end(), x) == V.end()) V.push_back(x); }
	int32 Remove(const T& x) { auto n = V.size(); V.erase(std::remove(V.begin(), V.end(), x), V.end()); return (int32)(n - V.size()); }
	template<class P> int32 RemoveAll(P p) { auto n = V.size(); V.erase(std::remove_if(V.begin(), V.end(), p), V.end()); return (int32)(n - V.size()); }
	void Reset() { V.clear(); }
	T& operator[](int32 i) { return V[i]; }
	template<class P> void StableSort(P p)
	{
		if constexpr (std::is_pointer<T>::value) std::stable_sort(V.begin(), V.end(), [&](T a, T b) { return p(*a, *b); }); // UE dereferences pointer elements
		else std::stable_sort(V.begin(), V.end(), p);
	}
	typename std::vector<T>::iterator begin() { return V.begin(); }
	typename std::vector<T>::iterator end() { return V.end(); }
	typename std::vector<T>::const_iterator begin() const { return V.begin(); }
	typename std::vector<T>::const_iterator end() const { return V.end(); }
};
template<class K, class V> struct TMap
{
	std::map<K, V> M;
	void Add(const K& k, const V& v) { M[k] = v; }
	const V* Find(const K& k) const { auto it = M.find(k); return it == M.end() ? nullptr : &it->second; }
	void Remove(const K& k) { M.erase(k); }
};

namespace EEndPlayReason { enum Type { Destroyed, RemovedFromWorld }; }
class AActor;
class UActorComponent : public UObject
{
public:
	struct { bool bCanEverTick = true; } PrimaryComponentTick;
	AActor* Owner = nullptr;
	bool bBegun = false;
	bool bWantsInitializeComponent = false;
	virtual void InitializeComponent() {}
	AActor* GetOwner() const { return Owner; }
	UWorld* GetWorld() const override;
	bool HasBegunPlay() const { return bBegun; }
	virtual void BeginPlay() { bBegun = true; }
	virtual void EndPlay(const EEndPlayReason::Type) {}
};
typedef UActorComponent Super; // every shimmed class that calls Super:: is a component

class UClass { public: std::function<AActor*(UWorld*, const FTransform&)> Factory; };
template<class T> struct TSubclassOf { UClass* C = nullptr; UClass* Get() const { return C; } };
template<class T> struct TSoftObjectPtr { bool IsNull() const { return true; } T* LoadSynchronous() const { return nullptr; } };

class AActor : public UObject
{
public:
	UWorld* World = nullptr;
	FVector Location;
	bool bPendingKill = false;
	bool bCollision = true;
	bool GetActorEnableCollision() const { return bCollision; }
	void SetActorEnableCollision(bool b) { bCollision = b; }
	std::vector<UActorComponent*> Components;
	UWorld* GetWorld() const override { return World; }
	FVector GetActorLocation() const { return Location; }
	FTransform GetActorTransform() const { return FTransform(Location); }
	bool IsPendingKillPending() const { return bPendingKill; }
	template<class T> T* FindComponentByClass() const { for (auto* C : Components) if (auto* X = dynamic_cast<T*>(C)) if (X->bAlive) return X; return nullptr; }
	template<class T> T* AddComponent() { T* C = new T(); C->Owner = this; Components.push_back(C); return C; }
	void BeginPlayAll() { for (auto* C : Components) if (C->bWantsInitializeComponent) C->InitializeComponent(); for (auto* C : Components) C->BeginPlay(); }
	bool Destroy() { if (bPendingKill) return true; bPendingKill = true; for (auto* C : Components) { C->EndPlay(EEndPlayReason::Destroyed); C->bAlive = false; } bAlive = false; return true; }
};
inline UWorld* UActorComponent::GetWorld() const { return Owner ? Owner->GetWorld() : nullptr; }

enum class ESpawnActorCollisionHandlingMethod { AdjustIfPossibleButAlwaysSpawn };
struct FActorSpawnParameters { ESpawnActorCollisionHandlingMethod SpawnCollisionHandlingOverride; };
class UWorldSubsystem : public UObject {};
class UWorld : public UObject
{
public:
	UWorldSubsystem* Subsystem = nullptr;
	int32 Spawned = 0;
	template<class T> T* GetSubsystem() const { return static_cast<T*>(Subsystem); }
	template<class T> T* SpawnActor(UClass* C, const FTransform& T0, const FActorSpawnParameters&) { ++Spawned; return C && C->Factory ? (T*)C->Factory(this, T0) : nullptr; }
};
class UParticleSystem {}; class USoundBase {};
struct UGameplayStatics
{
	static void SpawnEmitterAtLocation(const UObject*, UParticleSystem*, const FVector&) {}
	static void PlaySoundAtLocation(const UObject*, USoundBase*, const FVector&) {}
};
