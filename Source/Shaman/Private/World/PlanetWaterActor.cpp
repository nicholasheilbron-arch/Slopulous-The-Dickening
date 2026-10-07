#include "World/PlanetWaterActor.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

APlanetWaterActor::APlanetWaterActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Sea"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->CastShadow = false;
}

void APlanetWaterActor::Build(float SeaRadius, UMaterialInterface* Material, const FLinearColor& Color)
{
	// Icosahedron, then subdivide (shared midpoints) and push to the sphere.
	const float T = (1.f + FMath::Sqrt(5.f)) * 0.5f;
	TArray<FVector> V = {
		FVector(-1, T, 0), FVector(1, T, 0), FVector(-1, -T, 0), FVector(1, -T, 0),
		FVector(0, -1, T), FVector(0, 1, T), FVector(0, -1, -T), FVector(0, 1, -T),
		FVector(T, 0, -1), FVector(T, 0, 1), FVector(-T, 0, -1), FVector(-T, 0, 1) };
	for (FVector& P : V) P.Normalize();
	TArray<int32> F = { 0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11, 1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
		3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9, 4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1 };

	for (int32 S = 0; S < FMath::Clamp(Subdivisions, 0, 6); ++S)
	{
		TMap<uint64, int32> Mid;
		auto Midpoint = [&V, &Mid](int32 A, int32 B)
		{
			const uint64 Key = ((uint64)FMath::Min(A, B) << 32) | (uint64)FMath::Max(A, B);
			if (const int32* Found = Mid.Find(Key)) return *Found;
			const int32 I = V.Add(((V[A] + V[B]) * 0.5f).GetSafeNormal());
			Mid.Add(Key, I);
			return I;
		};
		TArray<int32> NF; NF.Reserve(F.Num() * 4);
		for (int32 i = 0; i < F.Num(); i += 3)
		{
			const int32 A = F[i], B = F[i + 1], C = F[i + 2];
			const int32 AB = Midpoint(A, B), BC = Midpoint(B, C), CA = Midpoint(C, A);
			NF.Append({ A, AB, CA, B, BC, AB, C, CA, BC, AB, BC, CA });
		}
		F = MoveTemp(NF);
	}

	// Unreal front faces: cross(V1 - V0, V2 - V0) points away from the viewer (same convention as AShamanTerrainActor).
	for (int32 i = 0; i < F.Num(); i += 3)
	{
		const FVector N = FVector::CrossProduct(V[F[i + 1]] - V[F[i]], V[F[i + 2]] - V[F[i]]);
		const bool bPointsOut = FVector::DotProduct(N, V[F[i]]) > 0.f;
		if (bPointsOut != bFlipWinding) Swap(F[i + 1], F[i + 2]);
	}

	TArray<FVector> Verts, Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Verts.Reserve(V.Num()); Normals.Reserve(V.Num()); UVs.Reserve(V.Num()); Colors.Reserve(V.Num()); Tangents.Reserve(V.Num());
	for (const FVector& P : V)
	{
		Verts.Add(P * SeaRadius);
		Normals.Add(P);
		UVs.Add(FVector2D(P.X, P.Y));
		Colors.Add(Color);
		FVector Tan = FVector::CrossProduct(FVector::UpVector, P);
		if (Tan.SizeSquared() < 1e-6f) Tan = FVector::ForwardVector;
		Tangents.Add(FProcMeshTangent(Tan.GetSafeNormal(), false));
	}
	Mesh->CreateMeshSection_LinearColor(0, Verts, F, Normals, UVs, Colors, Tangents, /*bCreateCollision*/ false);
	UMaterialInterface* Mat = Material ? Material
		: LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	if (Mat) Mesh->SetMaterial(0, Mat);
}
