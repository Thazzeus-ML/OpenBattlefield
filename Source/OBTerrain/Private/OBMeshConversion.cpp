// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
//
// STATUS: not yet compiled against Unreal Engine. See Docs/status.md.
#include "OBMeshConversion.h"

void OBConvertChunkMesh(const ob::MeshData& Mesh, double VoxelSizeCm, bool bFlipWinding, FOBMeshBuffers& Out)
{
    Out.Reset();
    const int32 VertexCount = static_cast<int32>(Mesh.VertexCount());
    Out.Vertices.Reserve(VertexCount);
    Out.Normals.Reserve(VertexCount);
    Out.UV0.Reserve(VertexCount);
    Out.VertexColors.Reserve(VertexCount);
    Out.Tangents.Reserve(VertexCount);

    for (int32 i = 0; i < VertexCount; ++i)
    {
        const float* P = &Mesh.positions[static_cast<size_t>(i) * 3];
        const float* N = &Mesh.normals[static_cast<size_t>(i) * 3];
        Out.Vertices.Add(FVector(P[0] * VoxelSizeCm, P[1] * VoxelSizeCm, P[2] * VoxelSizeCm));
        const FVector Normal(N[0], N[1], N[2]);
        Out.Normals.Add(Normal);

        // Planar UVs from the two axes orthogonal to the face normal, one voxel = one UV unit.
        FVector2D UV;
        FVector Tangent;
        if (FMath::Abs(N[2]) > 0.5f)
        {
            UV = FVector2D(P[0], P[1]);
            Tangent = FVector(1, 0, 0);
        }
        else if (FMath::Abs(N[0]) > 0.5f)
        {
            UV = FVector2D(P[1], P[2]);
            Tangent = FVector(0, 1, 0);
        }
        else
        {
            UV = FVector2D(P[0], P[2]);
            Tangent = FVector(1, 0, 0);
        }
        Out.UV0.Add(UV);
        Out.Tangents.Add(FProcMeshTangent(Tangent, false));

        const float MaterialId = static_cast<float>(Mesh.vertexMaterial[static_cast<size_t>(i)]) / 255.0f;
        Out.VertexColors.Add(FLinearColor(MaterialId, 0.0f, 0.0f, 1.0f));
    }

    const int32 IndexCount = static_cast<int32>(Mesh.indices.size());
    Out.Triangles.Reserve(IndexCount);
    for (int32 t = 0; t + 2 < IndexCount; t += 3)
    {
        const int32 A = static_cast<int32>(Mesh.indices[static_cast<size_t>(t)]);
        const int32 B = static_cast<int32>(Mesh.indices[static_cast<size_t>(t) + 1]);
        const int32 C = static_cast<int32>(Mesh.indices[static_cast<size_t>(t) + 2]);
        if (bFlipWinding)
        {
            Out.Triangles.Add(A);
            Out.Triangles.Add(C);
            Out.Triangles.Add(B);
        }
        else
        {
            Out.Triangles.Add(A);
            Out.Triangles.Add(B);
            Out.Triangles.Add(C);
        }
    }
}
