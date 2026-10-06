// Runs the Convert scenarios against the real tribe/conversion/effect .cpp files (via shim/UEShim.h).
#include "TribeComponent.h"
#include "TribeMemberComponent.h"
#include "TribeRegistrySubsystem.h"
#include "SpellEffect_ConvertUnits.h"
#include "SpellComponent.h"
#include <cstdio>

static int Fails = 0, Checks = 0;
#define CHECK(c) do { ++Checks; if (!(c)) { ++Fails; printf("  FAIL %s:%d  %s\n", __func__, __LINE__, #c); } } while (0)

struct FWorldFx
{
	UWorld W; UTribeRegistrySubsystem R;
	FWorldFx() { W.Subsystem = &R; }
	AActor* Bare(FVector L) { AActor* A = new AActor(); A->World = &W; A->Location = L; return A; }
	UTribeMemberComponent* Unit(FVector L, EUnitKind K, int32 T)
	{
		AActor* A = Bare(L); auto* M = A->AddComponent<UTribeMemberComponent>(); M->UnitKind = K; M->TribeId = T; A->BeginPlayAll(); return M;
	}
	struct FCaster { AActor* A; UTribeComponent* T; USpellComponent* S; };
	FCaster Caster(int32 Id, int32 Cap)
	{
		FCaster C; C.A = Bare(FVector()); C.S = C.A->AddComponent<USpellComponent>(); C.T = C.A->AddComponent<UTribeComponent>();
		C.T->TribeId = Id; C.T->PopulationCapacity = Cap; C.A->BeginPlayAll(); return C;
	}
	FSpellEffectResult Cast(FCaster& C, FVector Aim, float Radius = 500.f)
	{
		USpellEffect_ConvertUnits E; FSpellEffectContext X; X.Caster = C.A; X.Target = Aim; X.Radius = Radius;
		X.SpellId = "Convert"; X.EffectId = "ConvertWildmen"; return E.Execute(X);
	}
};
static const FVector Aim(2000, 0, 0);

static void OneWildman()
{
	FWorldFx F; auto C = F.Caster(0, 10);
	auto* W = F.Unit(Aim + FVector(100, 0, 0), EUnitKind::Wildman, -1);
	auto R = F.Cast(C, Aim);
	CHECK(R.Affected == 1); CHECK(W->IsFollower() && W->GetTribeId() == 0); CHECK(C.T->GetFollowerCount() == 1);
	CHECK(C.T->IsFollower(W)); CHECK(C.S->FollowerCount == 1);
}
static void Multiple()
{
	FWorldFx F; auto C = F.Caster(0, 10); std::vector<UTribeMemberComponent*> V;
	for (int i = 0; i < 4; ++i) V.push_back(F.Unit(Aim + FVector(0, -300.f + i * 200.f, 0), EUnitKind::Wildman, -1));
	F.Cast(C, Aim);
	for (auto* W : V) CHECK(W->IsFollower() && W->GetTribeId() == 0);
	CHECK(C.T->GetFollowerCount() == 4);
}
static void Capacity()
{
	FWorldFx F; auto C = F.Caster(0, 3);
	F.Unit(FVector(-3000, 0, 0), EUnitKind::Follower, 0);
	std::vector<UTribeMemberComponent*> V;
	for (int i = 0; i < 4; ++i) V.push_back(F.Unit(Aim + FVector(50.f * (i + 1), 0, 0), EUnitKind::Wildman, -1));
	CHECK(C.T->GetFollowerCount() == 1);
	auto R = F.Cast(C, Aim);
	CHECK(C.T->GetFollowerCount() == 3); CHECK(R.Affected == 2); CHECK(R.SkippedForCapacity == 2); CHECK(R.Candidates == 4);
	CHECK(V[0]->IsFollower() && V[1]->IsFollower()); CHECK(V[2]->IsWildman() && V[3]->IsWildman() && V[2]->GetTribeId() == -1);
	R = F.Cast(C, Aim);
	CHECK(C.T->GetFollowerCount() == 3); CHECK(R.Affected == 0); CHECK(V[3]->IsWildman());
	C.T->SetPopulationCapacity(4); R = F.Cast(C, Aim);
	CHECK(R.Affected == 1 && C.T->GetFollowerCount() == 4 && V[2]->IsFollower() && V[3]->IsWildman());
}
static void Enemy()
{
	FWorldFx F; auto C = F.Caster(0, 10); auto E = F.Caster(1, 10);
	auto* X = F.Unit(Aim, EUnitKind::Follower, 1);
	CHECK(E.T->GetFollowerCount() == 1);
	F.Cast(C, Aim);
	CHECK(X->GetTribeId() == 1 && E.T->IsFollower(X)); CHECK(C.T->GetFollowerCount() == 0); CHECK(E.T->GetFollowerCount() == 1);
}
static void OwnAndSpecial()
{
	FWorldFx F; auto C = F.Caster(0, 10);
	auto* B = F.Unit(Aim, EUnitKind::Follower, 0);
	auto* S = F.Unit(Aim + FVector(50, 0, 0), EUnitKind::Shaman, 1);
	auto* H = F.Unit(Aim + FVector(-50, 0, 0), EUnitKind::Building, -1);
	auto R = F.Cast(C, Aim);
	CHECK(B->IsFollower() && B->GetTribeId() == 0 && C.T->GetFollowerCount() == 1);
	CHECK(S->GetUnitKind() == EUnitKind::Shaman && S->GetTribeId() == 1); CHECK(H->GetUnitKind() == EUnitKind::Building);
	CHECK(R.Candidates == 0);
	// The caster itself is never a target even if it carried a Wildman identity.
	auto* Self = C.A->AddComponent<UTribeMemberComponent>(); Self->UnitKind = EUnitKind::Wildman; static_cast<UActorComponent*>(Self)->BeginPlay();
	C.A->Location = Aim; R = F.Cast(C, Aim); CHECK(Self->IsWildman());
}
static void Radius()
{
	FWorldFx F; auto C = F.Caster(0, 10);
	auto* In = F.Unit(Aim + FVector(499, 0, 0), EUnitKind::Wildman, -1);
	auto* Out = F.Unit(Aim + FVector(0, 501, 0), EUnitKind::Wildman, -1);
	F.Cast(C, Aim);
	CHECK(In->IsFollower()); CHECK(Out->IsWildman() && Out->GetTribeId() == -1); CHECK(C.T->GetFollowerCount() == 1);
}
static void NoTargets()
{
	FWorldFx F; auto C = F.Caster(0, 10);
	auto R = F.Cast(C, Aim);
	CHECK(R.bHandled && R.Affected == 0 && C.T->GetFollowerCount() == 0);
	AActor* NoTribe = F.Bare(FVector()); F.Unit(Aim, EUnitKind::Wildman, -1);
	USpellEffect_ConvertUnits E; FSpellEffectContext X; X.Caster = NoTribe; X.Target = Aim; X.Radius = 500;
	CHECK(E.Execute(X).Affected == 0); // caster without a tribe: no crash, no conversion
}
static void ReplaceWithBraveClass()
{
	FWorldFx F; auto C = F.Caster(0, 10);
	UClass Brave; int Made = 0; int SeenAtBeginPlay = -99;
	Brave.Factory = [&](UWorld* W, const FTransform& T) { AActor* A = new AActor(); A->World = W; A->Location = T.L; auto* M = A->AddComponent<UTribeMemberComponent>();
		M->UnitKind = EUnitKind::Follower; M->TribeId = 0; /* Blueprint default already player tribe */ A->BeginPlayAll(); ++Made; return A; };
	C.T->ConvertedFollowerClass.C = &Brave;
	auto* W = F.Unit(Aim, EUnitKind::Wildman, -1); AActor* WildActor = W->GetOwner();
	auto R = F.Cast(C, Aim);
	CHECK(R.Affected == 1); CHECK(Made == 1); CHECK(C.T->GetFollowerCount() == 1); CHECK(WildActor->IsPendingKillPending());
	CHECK(F.R.GetNumMembers() == 1); // only the new Brave: the Wildman unregistered on destroy
	// Blueprint with default tribe -1: still counted exactly once.
	Brave.Factory = [&](UWorld* W2, const FTransform& T) { AActor* A = new AActor(); A->World = W2; A->Location = T.L; auto* M = A->AddComponent<UTribeMemberComponent>();
		M->UnitKind = EUnitKind::Wildman; M->TribeId = -1; A->BeginPlayAll(); SeenAtBeginPlay = M->TribeId; ++Made; return A; };
	F.Unit(Aim, EUnitKind::Wildman, -1); F.Cast(C, Aim);
	CHECK(C.T->GetFollowerCount() == 2); CHECK(SeenAtBeginPlay == 0); // identity applied before BeginPlay
	CHECK(F.R.GetPendingConversion().bActive == false);
	// Class without identity: conversion aborted, Wildman unchanged, no population change.
	Brave.Factory = [&](UWorld* W3, const FTransform& T) { AActor* A = new AActor(); A->World = W3; A->Location = T.L; ++Made; return A; };
	auto* W3 = F.Unit(Aim, EUnitKind::Wildman, -1); R = F.Cast(C, Aim);
	CHECK(R.Affected == 0 && W3->IsWildman() && !W3->GetOwner()->IsPendingKillPending() && C.T->GetFollowerCount() == 2);
	CHECK(W3->GetOwner()->GetActorEnableCollision()); // collision restored after the aborted conversion
}
static void Roster()
{
	FWorldFx F; auto C = F.Caster(0, 10);
	auto* B = F.Unit(FVector(100, 0, 0), EUnitKind::Follower, 0);
	CHECK(C.T->RegisterFollower(B) && C.T->RegisterFollower(B)); CHECK(C.T->GetFollowerCount() == 1);
	auto* W = F.Unit(FVector(200, 0, 0), EUnitKind::Wildman, -1);
	CHECK(W->ConvertToTribe(C.T, EConversionKind::Recruit) == W); CHECK(W->ConvertToTribe(C.T, EConversionKind::Recruit) == nullptr);
	auto* E = F.Unit(FVector(300, 0, 0), EUnitKind::Follower, 1);
	CHECK(E->ConvertToTribe(C.T, EConversionKind::Hypnotize) == nullptr && E->ConvertToTribe(C.T, EConversionKind::Permanent) == nullptr);
	CHECK(C.T->GetFollowerCount() == 2);
	B->GetOwner()->Destroy(); CHECK(C.T->GetFollowerCount() == 1);
	// Followers that begin play before their tribe are adopted when the tribe registers.
	// Duplicate tribe ids are rejected (the first tribe keeps its roster).
	auto Dup = F.Caster(0, 10); CHECK(Dup.T->GetFollowerCount() == 0 && C.T->GetFollowerCount() == 1);
	FWorldFx G; auto* Early = G.Unit(FVector(), EUnitKind::Follower, 5); auto Late = G.Caster(5, 10);
	CHECK(Late.T->IsFollower(Early) && Late.T->GetFollowerCount() == 1);
	// Changing tribe moves the follower between rosters exactly once.
	auto Other = G.Caster(6, 10); Early->SetTribeId(6);
	CHECK(Late.T->GetFollowerCount() == 0 && Other.T->GetFollowerCount() == 1);
}

int main()
{
	OneWildman(); Multiple(); Capacity(); Enemy(); OwnAndSpecial(); Radius(); NoTargets(); ReplaceWithBraveClass(); Roster();
	printf("Convert logic harness: %d checks, %d failures\n", Checks, Fails);
	return Fails ? 1 : 0;
}
