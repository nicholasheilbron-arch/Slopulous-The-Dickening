#include "World/ShamanTerrainActor.h"
#include "World/WorldGenerator.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"

AShamanTerrainActor::AShamanTerrainActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->bUseAsyncCooking = false; // collision must exist before units are spawned on it
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCanEverAffectNavigation(true);
	Mesh->SetMobility(EComponentMobility::Movable);
}

FLinearColor AShamanTerrainActor::ColorFor(float H, float Slope, float MaxH)
{
	const FLinearColor Sand(0.76f, 0.70f, 0.50f), Grass(0.22f, 0.45f, 0.16f), Rock(0.42f, 0.39f, 0.36f),
		Snow(0.92f, 0.93f, 0.95f), Seabed(0.35f, 0.32f, 0.22f);
	if (H < -20.f) return Seabed;
	if (H < 60.f) return Sand;
	FLinearColor C = Grass;
	if (Slope > 110.f) C = FLinearColor::LerpUsingHSV(Grass, Rock, FMath::Clamp((Slope - 110.f) / 80.f, 0.f, 1.f));
	if (H > 1300.f) C = FMath::Lerp(C, Rock, FMath::Clamp((H - 1300.f) / 400.f, 0.f, 1.f));
	if (H > 1900.f) C = FMath::Lerp(C, Snow, FMath::Clamp((H - 1900.f) / 300.f, 0.f, 1.f));
	return C;
}

void AShamanTerrainActor::Build(const FWorldLayout& L, UMaterialInterface* Material)
{
	const int32 N = L.GridVertices;
	TArray<FVector> Verts; Verts.Reserve(N * N);
	TArray<FVector> Normals; Normals.Reserve(N * N);
	TArray<FVector2D> UVs; UVs.Reserve(N * N);
	TArray<FLinearColor> Colors; Colors.Reserve(N * N);
	TArray<FProcMeshTangent> Tangents; Tangents.Reserve(N * N);

	auto Hh = [&L, N](int32 X, int32 Y) { return L.Heights[FMath::Clamp(X, 0, N - 1) * N + FMath::Clamp(Y, 0, N - 1)]; };
	for (int32 X = 0; X < N; ++X)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			const FVector2D P = FShamanWorldGenerator::VertexToWorld(L, X, Y);
			const float H = Hh(X, Y);
			Verts.Add(FVector(P.X, P.Y, H));
			const float DX = (Hh(X + 1, Y) - Hh(X - 1, Y)) / (2.f * L.CellSize);
			const float DY = (Hh(X, Y + 1) - Hh(X, Y - 1)) / (2.f * L.CellSize);
			Normals.Add(FVector(-DX, -DY, 1.f).GetSafeNormal());
			UVs.Add(FVector2D(X * 0.25f, Y * 0.25f));
			const float Slope = FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) * L.CellSize;
			Colors.Add(ColorFor(H, Slope, L.MaxHeight));
			Tangents.Add(FProcMeshTangent(1.f, 0.f, 0.f));
		}
	}

	// Vertex index = X * N + Y. Seen from above (+Z toward the viewer) with X up / Y right,
	// (I0 -> I1 -> I2) runs clockwise, which Unreal treats as front-facing.
	TArray<int32> Tris; Tris.Reserve((N - 1) * (N - 1) * 6);
	for (int32 X = 0; X < N - 1; ++X)
	{
		for (int32 Y = 0; Y < N - 1; ++Y)
		{
			const int32 I0 = X * N + Y, I1 = (X + 1) * N + Y, I2 = (X + 1) * N + Y + 1, I3 = X * N + Y + 1;
			if (!bFlipWinding) { Tris.Append({ I0, I1, I2, I0, I2, I3 }); }
			else               { Tris.Append({ I0, I2, I1, I0, I3, I2 }); }
		}
	}

	Mesh->ClearAllMeshSections();
	Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, /*bCreateCollision*/ true);
	if (Material) Mesh->SetMaterial(0, Material);
}
